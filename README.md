# CN Binary Protocol — `observe` + `bcurl`

A small Linux/C++ networking project based on the Computer Networks course project:

> **HTTP, in binary — two tracks, one protocol.**

The goal is not to build a production web server. The goal is to understand what happens between:

```text
application
    ↓
custom binary protocol
    ↓
TCP byte stream
    ↓
POSIX sockets
```

The project deliberately uses `socket → bind → listen → accept → read/write → close` instead of a networking framework.

## What is included

```text
cn-binary-protocol/
├── CMakeLists.txt
├── README.md
├── PROTOCOL.md
├── HEX_DUMP.md
├── include/
│   ├── protocol.hpp
│   └── hex_dump.hpp
├── src/
│   ├── protocol.cpp
│   ├── hex_dump.cpp
│   ├── server.cpp
│   └── client.cpp
├── tests/
│   └── protocol_tests.cpp
├── docs/
│   └── BUILD_NOTES.md
└── www/
    └── index.html
```

## Build

Linux is the intended environment.

```bash
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

You should see:

```text
protocol_tests: all tests passed
```

## Run the server

Terminal 1:

```bash
./build/observe ./www 9000
```

## Run the client

Terminal 2:

```bash
./build/bcurl -v localhost:9000/index.html
```

The response body should be printed to stdout. The `-v` output goes to stderr so it does not corrupt the body.

Try a missing file too:

```bash
./build/bcurl -v localhost:9000/missing.txt
```

That should return a 404 response and `bcurl` should exit with a non-zero status.

## The important learning path

Do not start by memorising the code. Follow the bytes:

```text
Request object
     ↓
encode_request()
     ↓
16-byte fixed header + payload
     ↓
TCP send()
     ↓
TCP recv()
     ↓
read_frame()
     ↓
decode_request()
     ↓
filesystem lookup
     ↓
Response object
     ↓
encode_response()
     ↓
TCP
     ↓
decode_response()
     ↓
body → stdout
```

The protocol specification is the contract. The client and server are two independent implementations of that contract.

## Safety/robustness deliberately included

- Network byte order is explicit and big-endian.
- TCP partial reads are handled by `read_exact()`.
- TCP partial writes are handled by `write_all()`.
- Frame lengths are checked before payload parsing.
- Payloads have a 16 MiB safety limit.
- Unknown frame types can be skipped because their complete length is in the fixed header.
- Paths containing `..` are rejected.
- Files are opened in binary mode.
- `MSG_NOSIGNAL` prevents a broken peer from killing the process with SIGPIPE during `send()`.
- No second client connection is opened.

## One intentional simplification

The server handles one TCP connection synchronously. This is intentional: the course project is primarily about **protocol design, framing, serialization and TCP**, not building nginx.

The next learning step after this project is to connect the same protocol ideas to `select`, `epoll`, persistent HTTP, nginx and the scaling lessons from the course.
