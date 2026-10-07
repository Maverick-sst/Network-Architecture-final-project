#include "protocol.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>

namespace cn {
namespace {

void append_u16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    const std::uint16_t network = htons(value);
    const auto* p = reinterpret_cast<const std::uint8_t*>(&network);
    out.insert(out.end(), p, p + 2);
}

void append_u32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    const std::uint32_t network = htonl(value);
    const auto* p = reinterpret_cast<const std::uint8_t*>(&network);
    out.insert(out.end(), p, p + 4);
}

std::uint16_t read_u16(const std::vector<std::uint8_t>& data, std::size_t& pos) {
    if (pos + 2 > data.size()) throw std::runtime_error("truncated uint16");
    std::uint16_t network = 0;
    std::memcpy(&network, data.data() + pos, 2);
    pos += 2;
    return ntohs(network);
}

std::uint32_t read_u32(const std::vector<std::uint8_t>& data, std::size_t& pos) {
    if (pos + 4 > data.size()) throw std::runtime_error("truncated uint32");
    std::uint32_t network = 0;
    std::memcpy(&network, data.data() + pos, 4);
    pos += 4;
    return ntohl(network);
}

void append_bytes(std::vector<std::uint8_t>& out, const std::string& value) {
    out.insert(out.end(), value.begin(), value.end());
}

void append_header(std::vector<std::uint8_t>& out, const Header& header) {
    const auto id = static_cast<std::uint8_t>(header.id);
    if (id == 0 || id > 10) throw std::runtime_error("invalid header id");
    if (header.value.size() > 65535) throw std::runtime_error("header value too large");
    out.push_back(id);
    append_u16(out, static_cast<std::uint16_t>(header.value.size()));
    append_bytes(out, header.value);
}

std::vector<Header> decode_headers(const std::vector<std::uint8_t>& data,
                                   std::size_t& pos,
                                   std::uint16_t count) {
    std::vector<Header> headers;
    headers.reserve(count);
    for (std::uint16_t i = 0; i < count; ++i) {
        if (pos + 3 > data.size()) throw std::runtime_error("truncated header entry");
        const auto id = static_cast<HeaderId>(data[pos++]);
        if (static_cast<std::uint8_t>(id) == 0 || static_cast<std::uint8_t>(id) > 10) {
            throw std::runtime_error("unknown header id");
        }
        const std::uint16_t length = read_u16(data, pos);
        if (pos + length > data.size()) throw std::runtime_error("truncated header value");
        std::string value(reinterpret_cast<const char*>(data.data() + pos), length);
        pos += length;
        headers.push_back({id, std::move(value)});
    }
    return headers;
}

std::vector<std::uint8_t> make_frame(FrameType type,
                                     std::uint8_t flags,
                                     std::uint16_t header_count,
                                     std::uint32_t stream_id,
                                     const std::vector<std::uint8_t>& payload) {
    if (payload.size() > kMaxPayload) throw std::runtime_error("payload too large");

    std::vector<std::uint8_t> frame;
    frame.reserve(kFrameHeaderSize + payload.size());

    // Fixed 16-byte header: magic(2), version(1), type(1), flags(1),
    // reserved(1), header_count(2), payload_length(4), stream_id(4).
    frame.push_back('C');
    frame.push_back('N');
    frame.push_back(kVersion);
    frame.push_back(static_cast<std::uint8_t>(type));
    frame.push_back(flags);
    frame.push_back(0);
    append_u16(frame, header_count);
    append_u32(frame, static_cast<std::uint32_t>(payload.size()));
    append_u32(frame, stream_id);

    frame.insert(frame.end(), payload.begin(), payload.end());
    return frame;
}

FrameHeader parse_fixed_header(const std::vector<std::uint8_t>& frame) {
    if (frame.size() < kFrameHeaderSize) throw std::runtime_error("frame shorter than fixed header");
    if (frame[0] != 'C' || frame[1] != 'N') throw std::runtime_error("bad magic");
    if (frame[2] != kVersion) throw std::runtime_error("unsupported protocol version");

    FrameHeader h;
    h.version = frame[2];
    h.type = frame[3];
    h.flags = frame[4];
    h.reserved = frame[5];
    std::size_t pos = 6;
    h.header_count = read_u16(frame, pos);
    h.payload_length = read_u32(frame, pos);
    h.stream_id = read_u32(frame, pos);
    if (h.payload_length > kMaxPayload) throw std::runtime_error("payload exceeds limit");
    if (frame.size() != kFrameHeaderSize + h.payload_length) {
        throw std::runtime_error("frame length does not match payload_length");
    }
    if (h.reserved != 0) throw std::runtime_error("reserved bits are non-zero");
    return h;
}

} // namespace

std::vector<std::uint8_t> encode_request(const Request& request, std::uint32_t stream_id) {
    if (request.path.empty() || request.path.size() > 65535) throw std::runtime_error("invalid request path length");
    if (request.headers.size() > 65535) throw std::runtime_error("too many headers");

    std::vector<std::uint8_t> payload;
    append_u16(payload, static_cast<std::uint16_t>(request.path.size()));
    append_bytes(payload, request.path);
    for (const auto& h : request.headers) append_header(payload, h);

    return make_frame(FrameType::Request, 0, static_cast<std::uint16_t>(request.headers.size()), stream_id, payload);
}

std::vector<std::uint8_t> encode_response(const Response& response, std::uint32_t stream_id) {
    if (response.headers.size() > 65535) throw std::runtime_error("too many headers");
    std::vector<std::uint8_t> payload;
    append_u16(payload, response.status);
    for (const auto& h : response.headers) append_header(payload, h);
    payload.insert(payload.end(), response.body.begin(), response.body.end());
    return make_frame(FrameType::Response, 0, static_cast<std::uint16_t>(response.headers.size()), stream_id, payload);
}

Request decode_request(const std::vector<std::uint8_t>& frame) {
    const FrameHeader h = parse_fixed_header(frame);
    if (h.type != static_cast<std::uint8_t>(FrameType::Request)) throw std::runtime_error("not a request frame");

    std::vector<std::uint8_t> payload(frame.begin() + kFrameHeaderSize, frame.end());
    std::size_t pos = 0;
    const std::uint16_t path_len = read_u16(payload, pos);
    if (path_len == 0 || pos + path_len > payload.size()) throw std::runtime_error("invalid path length");
    std::string path(reinterpret_cast<const char*>(payload.data() + pos), path_len);
    pos += path_len;

    auto headers = decode_headers(payload, pos, h.header_count);
    if (pos != payload.size()) throw std::runtime_error("unexpected bytes in request payload");
    return {h.stream_id, std::move(path), std::move(headers)};
}

Response decode_response(const std::vector<std::uint8_t>& frame) {
    const FrameHeader h = parse_fixed_header(frame);
    if (h.type != static_cast<std::uint8_t>(FrameType::Response)) throw std::runtime_error("not a response frame");

    std::vector<std::uint8_t> payload(frame.begin() + kFrameHeaderSize, frame.end());
    std::size_t pos = 0;
    const std::uint16_t status = read_u16(payload, pos);
    auto headers = decode_headers(payload, pos, h.header_count);
    std::vector<std::uint8_t> body(payload.begin() + static_cast<std::ptrdiff_t>(pos), payload.end());
    return {h.stream_id, status, std::move(headers), std::move(body)};
}

void write_all(int fd, const std::vector<std::uint8_t>& bytes) {
    std::size_t sent = 0;
    while (sent < bytes.size()) {
        const ssize_t n = ::send(fd, bytes.data() + sent, bytes.size() - sent, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) continue;
            throw std::runtime_error(std::string("send: ") + std::strerror(errno));
        }
        if (n == 0) throw std::runtime_error("send returned zero");
        sent += static_cast<std::size_t>(n);
    }
}

void read_exact(int fd, std::uint8_t* buffer, std::size_t length) {
    std::size_t received = 0;
    while (received < length) {
        const ssize_t n = ::recv(fd, buffer + received, length - received, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            throw std::runtime_error(std::string("recv: ") + std::strerror(errno));
        }
        if (n == 0) throw std::runtime_error("peer closed before frame was complete");
        received += static_cast<std::size_t>(n);
    }
}

std::vector<std::uint8_t> read_frame(int fd) {
    std::vector<std::uint8_t> header(kFrameHeaderSize);
    read_exact(fd, header.data(), header.size());

    if (header[0] != 'C' || header[1] != 'N') throw std::runtime_error("bad magic");
    if (header[2] != kVersion) throw std::runtime_error("unsupported protocol version");

    std::size_t pos = 6;
    (void)read_u16(header, pos);
    const std::uint32_t payload_length = read_u32(header, pos);
    if (payload_length > kMaxPayload) throw std::runtime_error("payload exceeds limit");

    std::vector<std::uint8_t> frame;
    frame.reserve(kFrameHeaderSize + payload_length);
    frame.insert(frame.end(), header.begin(), header.end());
    const std::size_t old_size = frame.size();
    frame.resize(old_size + payload_length);
    if (payload_length != 0) read_exact(fd, frame.data() + old_size, payload_length);
    return frame;
}

std::string header_name(HeaderId id) {
    switch (id) {
        case HeaderId::ContentType:   return "Content-Type";
        case HeaderId::ContentLength: return "Content-Length";
        case HeaderId::Server:        return "Server";
        case HeaderId::Connection:    return "Connection";
        case HeaderId::Date:          return "Date";
        case HeaderId::LastModified:  return "Last-Modified";
        case HeaderId::Accept:        return "Accept";
        case HeaderId::Host:          return "Host";
        case HeaderId::UserAgent:     return "User-Agent";
        case HeaderId::CacheControl:  return "Cache-Control";
    }
    return "Unknown";
}

} // namespace cn
