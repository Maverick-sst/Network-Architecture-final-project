#include "hex_dump.hpp"
#include "protocol.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <netinet/in.h>
#include <optional>
#include <sstream>
#include <sys/socket.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {

constexpr int kBacklog = 128;

std::string guess_content_type(const fs::path& path) {
    const std::string ext = path.extension().string();
    if (ext == ".html" || ext == ".htm") return "text/html; charset=utf-8";
    if (ext == ".txt") return "text/plain; charset=utf-8";
    if (ext == ".css") return "text/css; charset=utf-8";
    if (ext == ".js") return "application/javascript";
    if (ext == ".json") return "application/json";
    if (ext == ".png") return "image/png";
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    return "application/octet-stream";
}

std::vector<std::uint8_t> read_file(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("cannot open file");
    file.seekg(0, std::ios::end);
    const std::streamoff size = file.tellg();
    if (size < 0) throw std::runtime_error("cannot determine file size");
    if (static_cast<std::uint64_t>(size) > cn::kMaxPayload) throw std::runtime_error("file exceeds 16 MiB protocol limit");
    file.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
    if (!data.empty()) file.read(reinterpret_cast<char*>(data.data()), size);
    return data;
}

bool safe_request_path(const std::string& path) {
    if (path.empty() || path[0] != '/' || path.find('\0') != std::string::npos) return false;
    fs::path p(path.substr(1));
    if (p.is_absolute()) return false;
    for (const auto& part : p) {
        if (part == "..") return false;
    }
    return true;
}

fs::path resolve_under_root(const fs::path& root, const std::string& request_path) {
    if (!safe_request_path(request_path)) throw std::runtime_error("unsafe request path");
    std::string relative = request_path.substr(1);
    if (relative.empty()) relative = "index.html";
    const fs::path candidate = (root / relative).lexically_normal();
    const fs::path root_norm = root.lexically_normal();
    const auto root_it = root_norm.begin();
    auto c_it = candidate.begin();
    for (auto r = root_it; r != root_norm.end(); ++r, ++c_it) {
        if (c_it == candidate.end() || *r != *c_it) throw std::runtime_error("path escapes root");
    }
    return candidate;
}

cn::Response make_error(std::uint16_t status, const std::string& message) {
    const std::string body = "error: " + message + "\n";
    std::vector<std::uint8_t> bytes(body.begin(), body.end());
    return {1, status,
            {{cn::HeaderId::ContentType, "text/plain; charset=utf-8"},
             {cn::HeaderId::ContentLength, std::to_string(bytes.size())},
             {cn::HeaderId::Server, "cn-observe/1"},
             {cn::HeaderId::Connection, "keep-alive"}},
            std::move(bytes)};
}

cn::Response handle_request(const cn::Request& request, const fs::path& root) {
    try {
        const fs::path file = resolve_under_root(root, request.path);
        if (!fs::exists(file) || !fs::is_regular_file(file)) return make_error(404, "file not found");

        auto body = read_file(file);
        return {1, 200,
                {{cn::HeaderId::ContentType, guess_content_type(file)},
                 {cn::HeaderId::ContentLength, std::to_string(body.size())},
                 {cn::HeaderId::Server, "cn-observe/1"},
                 {cn::HeaderId::Connection, "keep-alive"}},
                std::move(body)};
    } catch (const std::exception& e) {
        return make_error(400, e.what());
    }
}

void serve_client(int fd, const fs::path& root, bool verbose) {
    while (true) {
        try {
            auto frame = cn::read_frame(fd);
            if (verbose) std::cerr << "[server] received frame (" << frame.size() << " bytes)\n" << cn::hex_dump(frame);

            // We cannot decode an unknown type because we intentionally do not
            // know its payload schema. The fixed header already told us the
            // payload length, so read_frame consumed it safely. Skipping it is
            // therefore the forward-compatibility mechanism.
            if (frame[3] != static_cast<std::uint8_t>(cn::FrameType::Request)) {
                if (verbose) std::cerr << "[server] skipped unknown frame type " << static_cast<unsigned>(frame[3]) << "\n";
                continue;
            }

            cn::Response response;
            try {
                const cn::Request request = cn::decode_request(frame);
                response = handle_request(request, root);
                response.stream_id = request.stream_id;
            } catch (const std::exception& e) {
                response = make_error(400, e.what());
            }

            const auto out = cn::encode_response(response, response.stream_id);
            if (verbose) std::cerr << "[server] sending response (" << out.size() << " bytes)\n" << cn::hex_dump(out);
            cn::write_all(fd, out);
        } catch (const std::exception& e) {
            // A clean peer close is represented by the same read error because
            // read_frame needs a complete frame. For this teaching project we
            // simply close the connection and let the client reconnect later.
            if (verbose) std::cerr << "[server] connection ended: " << e.what() << "\n";
            break;
        }
    }
    ::close(fd);
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: " << argv[0] << " <www-root> <port>\n";
        return 2;
    }

    const fs::path root = fs::absolute(argv[1]).lexically_normal();
    const int port = std::stoi(argv[2]);
    if (!fs::exists(root) || !fs::is_directory(root)) {
        std::cerr << "error: root is not a directory: " << root << '\n';
        return 2;
    }
    if (port < 1 || port > 65535) {
        std::cerr << "error: invalid port\n";
        return 2;
    }

    const int server_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { std::perror("socket"); return 1; }
    int yes = 1;
    if (::setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
        std::perror("setsockopt"); ::close(server_fd); return 1;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(static_cast<std::uint16_t>(port));

    if (::bind(server_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::perror("bind"); ::close(server_fd); return 1;
    }
    if (::listen(server_fd, kBacklog) < 0) {
        std::perror("listen"); ::close(server_fd); return 1;
    }

    std::cout << "observe listening on 0.0.0.0:" << port << "\n";
    std::cout << "serving root: " << root << "\n";

    while (true) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        const int client_fd = ::accept(server_fd, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            std::perror("accept");
            break;
        }
        char ip[INET_ADDRSTRLEN]{};
        ::inet_ntop(AF_INET, &client_addr.sin_addr, ip, sizeof(ip));
        std::cout << "accepted connection from " << ip << ':' << ntohs(client_addr.sin_port) << '\n';

        // The course project focuses on the protocol, not a production
        // concurrency architecture. One connection is handled synchronously.
        serve_client(client_fd, root, false);
    }

    ::close(server_fd);
    return 0;
}
