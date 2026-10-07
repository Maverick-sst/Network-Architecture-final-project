# Build Notes / Learning Map

This repository is intentionally organised around the networking concepts from the course.

## 1. `protocol.cpp`

This is the most important file to study first.

It answers:

> How does a C++ integer/string become bytes on the wire, and how do those bytes become values again?

The serializer uses `htons()` / `htonl()` so the wire format is explicitly big-endian.

## 2. `read_exact()` and `write_all()`

Do not skip these.

TCP is a byte stream. A call asking for 1000 bytes may return 300 bytes. The rest arrives later.

Therefore:

```text
read_exact(fd, 1000)
    ↓
recv 300
recv 500
recv 200
    ↓
exactly 1000 bytes
```

Likewise, `send()` is allowed to write only part of a buffer.

## 3. `read_frame()`

This is application-level framing on top of TCP:

```text
TCP stream
   ↓
read fixed 16-byte header
   ↓
extract payload_length
   ↓
read exactly payload_length bytes
   ↓
one complete application frame
```

This is the same fundamental problem HTTP/1.1 had to solve when persistent connections made EOF no longer sufficient as the message boundary.

## 4. `server.cpp`

The server follows the classic sequence:

```text
socket
  ↓
bind
  ↓
listen
  ↓
accept
  ↓
read_frame
  ↓
decode request
  ↓
filesystem
  ↓
encode response
  ↓
write_all
```

## 5. Why the project is not “just a web server”

The course assignment explicitly makes the protocol itself the interesting part. The server and client are separate implementations of the same specification.

That distinction is important:

```text
implementation detail  ≠  protocol contract
```

## 6. Suggested study order

1. Read `PROTOCOL.md` without reading the implementation.
2. Draw the 16-byte header yourself.
3. Read `append_u16`, `append_u32`, `read_u16`, `read_u32`.
4. Read `encode_request()`.
5. Read `decode_request()`.
6. Understand `read_exact()`.
7. Run the server/client.
8. Inspect the `-v` bytes.
9. Manually annotate them.
10. Only then read the filesystem/server loop.
