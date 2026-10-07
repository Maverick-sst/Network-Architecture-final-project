#include "hex_dump.hpp"
#include "protocol.hpp"

#include <cerrno>
#include <cstring>
#include <iostream>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

void usage(const char* program) {
    std::cerr << "usage: " << program << " [-v] host:port/path\n";
    std::cerr << "example: " << program << " -v localhost:9000/index.html\n";
}

struct Target {
    std::string host;
    std::string port;
    std::string path;
};

Target parse_target(const std::string& target) {
    const auto slash = target.find('/');
    const std::string authority = slash == std::string::npos ? target : target.substr(0, slash);
    const std::string path = slash == std::string::npos ? "/" : target.substr(slash);
    const auto colon = authority.rfind(':');
    if (colon == std::string::npos || colon == 0 || colon + 1 >= authority.size()) {
        throw std::runtime_error("target must be host:port/path");
    }
    return {authority.substr(0, colon), authority.substr(colon + 1), path};
}

int connect_tcp(const std::string& host, const std::string& port) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* results = nullptr;
    const int rc = ::getaddrinfo(host.c_str(), port.c_str(), &hints, &results);
    if (rc != 0) throw std::runtime_error(std::string("getaddrinfo: ") + gai_strerror(rc));

    int fd = -1;
    for (addrinfo* p = results; p != nullptr; p = p->ai_next) {
        fd = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0) continue;
        if (::connect(fd, p->ai_addr, p->ai_addrlen) == 0) break;
        ::close(fd);
        fd = -1;
    }
    ::freeaddrinfo(results);
    if (fd < 0) throw std::runtime_error("could not connect");
    return fd;
}

} // namespace

int main(int argc, char** argv) {
    bool verbose = false;
    std::string target;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-v") verbose = true;
        else if (arg == "-h" || arg == "--help") { usage(argv[0]); return 0; }
        else if (target.empty()) target = arg;
        else { usage(argv[0]); return 2; }
    }

    if (target.empty()) { usage(argv[0]); return 2; }

    try {
        const Target t = parse_target(target);
        const int fd = connect_tcp(t.host, t.port);

        // This is deliberately one connection for the whole request/response.
        const cn::Request request{1, t.path, {{cn::HeaderId::Host, t.host}, {cn::HeaderId::UserAgent, "cn-bcurl/1"}}};
        const auto encoded_request = cn::encode_request(request, 1);
        if (verbose) std::cerr << "[client] request (" << encoded_request.size() << " bytes)\n" << cn::hex_dump(encoded_request);
        cn::write_all(fd, encoded_request);

        cn::Response response;
        while (true) {
            const auto encoded_response = cn::read_frame(fd);
            if (verbose) std::cerr << "[client] frame (" << encoded_response.size() << " bytes)\n" << cn::hex_dump(encoded_response);

            // Unknown frame types are not errors. The length in the fixed
            // header allowed read_frame() to consume the whole frame safely,
            // so we can skip it and wait for the response we understand.
            if (encoded_response[3] != static_cast<std::uint8_t>(cn::FrameType::Response)) {
                if (verbose) std::cerr << "[client] skipped unknown frame type "
                                      << static_cast<unsigned>(encoded_response[3]) << "\n";
                continue;
            }
            response = cn::decode_response(encoded_response);
            break;
        }
        if (!response.body.empty()) {
            std::cout.write(reinterpret_cast<const char*>(response.body.data()), static_cast<std::streamsize>(response.body.size()));
        }

        ::close(fd);
        return response.status >= 400 ? 1 : 0;
    } catch (const std::exception& e) {
        std::cerr << "bcurl: " << e.what() << '\n';
        return 1;
    }
}
