#include "protocol.hpp"

#include <cassert>
#include <iostream>
#include <stdexcept>

int main() {
    using namespace cn;

    Request request{42, "/index.html", {
        {HeaderId::Host, "localhost"},
        {HeaderId::UserAgent, "cn-bcurl/1"},
    }};

    const auto encoded = encode_request(request, 42);
    assert(encoded.size() >= kFrameHeaderSize);
    assert(encoded[0] == 'C' && encoded[1] == 'N');
    const auto decoded = decode_request(encoded);
    assert(decoded.stream_id == 42);
    assert(decoded.path == request.path);
    assert(decoded.headers.size() == 2);
    assert(decoded.headers[0].value == "localhost");

    Response response{42, 200,
        {{HeaderId::ContentType, "text/plain"},
         {HeaderId::ContentLength, "5"}},
        {'H', 'e', 'l', 'l', 'o'}};
    const auto encoded_response = encode_response(response, 42);
    const auto decoded_response = decode_response(encoded_response);
    assert(decoded_response.stream_id == 42);
    assert(decoded_response.status == 200);
    assert(decoded_response.body.size() == 5);
    assert(decoded_response.body[0] == 'H');

    // A corrupted magic number must be rejected.
    auto bad = encoded;
    bad[0] = 'X';
    bool rejected = false;
    try { (void)decode_request(bad); }
    catch (const std::exception&) { rejected = true; }
    assert(rejected);

    // A length mismatch must be rejected before payload parsing.
    bad = encoded;
    bad[11] = static_cast<std::uint8_t>(bad[11] + 1);
    rejected = false;
    try { (void)decode_request(bad); }
    catch (const std::exception&) { rejected = true; }
    assert(rejected);

    std::cout << "protocol_tests: all tests passed\n";
}
