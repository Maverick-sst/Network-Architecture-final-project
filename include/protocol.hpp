#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <utility>

namespace cn {

// -----------------------------------------------------------------------------
// Protocol constants
// -----------------------------------------------------------------------------

constexpr std::uint8_t kVersion = 1;
constexpr std::size_t kFrameHeaderSize = 16;
constexpr std::uint32_t kMaxPayload = 16U * 1024U * 1024U; // 16 MiB safety cap.

// Frame types. Unknown values are deliberately legal on the wire: receivers
// must skip the corresponding frame using payload_length.
enum class FrameType : std::uint8_t {
    Request  = 1,
    Response = 2,
};

// Ten numbered header names, as required by the assignment.
enum class HeaderId : std::uint8_t {
    ContentType   = 1,
    ContentLength = 2,
    Server        = 3,
    Connection    = 4,
    Date          = 5,
    LastModified  = 6,
    Accept        = 7,
    Host          = 8,
    UserAgent     = 9,
    CacheControl  = 10,
};

struct Header {
    HeaderId id;
    std::string value;
};

struct FrameHeader {
    std::uint8_t version = kVersion;
    std::uint8_t type = 0;
    std::uint8_t flags = 0;
    std::uint8_t reserved = 0;
    std::uint16_t header_count = 0;
    std::uint32_t payload_length = 0;
    std::uint32_t stream_id = 0;
};

struct Request {
    std::uint32_t stream_id = 1;
    std::string path;
    std::vector<Header> headers;
};

struct Response {
    std::uint32_t stream_id = 1;
    std::uint16_t status = 500;
    std::vector<Header> headers;
    std::vector<std::uint8_t> body;
};

// Serialise a complete frame, including the fixed 16-byte header.
std::vector<std::uint8_t> encode_request(const Request& request, std::uint32_t stream_id = 1);
std::vector<std::uint8_t> encode_response(const Response& response, std::uint32_t stream_id = 1);

// Decode exactly one complete frame already held in memory. The parser checks
// every boundary before reading, so malformed network input becomes an error
// instead of an out-of-bounds read.
Request decode_request(const std::vector<std::uint8_t>& frame);
Response decode_response(const std::vector<std::uint8_t>& frame);

// Read/write helpers for TCP. TCP is a byte stream, so send() and recv() are
// allowed to transfer fewer bytes than requested.
void write_all(int fd, const std::vector<std::uint8_t>& bytes);
void read_exact(int fd, std::uint8_t* buffer, std::size_t length);
std::vector<std::uint8_t> read_frame(int fd);

// Returns a human-readable header name for documentation/debugging.
std::string header_name(HeaderId id);

} // namespace cn
