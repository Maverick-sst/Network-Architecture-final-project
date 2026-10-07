# CN/1 Binary Protocol Specification

Version 1 of a deliberately small HTTP-like binary protocol over TCP.

The protocol is implementation-independent: a conforming client and conforming server only need this document to communicate.

## 1. Transport

The protocol runs over a reliable TCP byte stream.

A TCP `recv()` call does **not** correspond to one application frame. A receiver must first read the fixed header, learn the payload length, and then read exactly that many payload bytes.

All multi-byte integers on the wire are **network byte order (big-endian)**.

## 2. Frame format

Every frame begins with exactly 16 bytes:

```text
Offset  Size  Field
------  ----  ----------------
0       2     Magic = ASCII "CN"
2       1     Version = 1
3       1     Type
4       1     Flags
5       1     Reserved = 0
6       2     Header count
8       4     Payload length
12      4     Stream ID
```

All integer fields are unsigned and big-endian.

### Why these fields?

- **Magic (2 bytes):** lets a receiver reject unrelated/corrupt data early.
- **Version (1 byte):** reserves a clean place for a future incompatible protocol version.
- **Type (1 byte):** distinguishes request, response and future frame types.
- **Flags (1 byte):** reserved for future behaviour without changing the fixed header.
- **Reserved bytes:** keep the layout aligned and provide explicit future space; version 1 requires zero.
- **Header count (2 bytes):** tells the parser how many compact header entries follow.
- **Payload length (4 bytes):** the key field for TCP framing and unknown-frame skipping.
- **Stream ID (4 bytes):** identifies the logical exchange. Version 1 uses `1`; the field leaves room for multiplexing later.

A receiver MUST reject a frame with the wrong magic, unsupported version, non-zero reserved fields, or a payload length above the implementation safety limit.

## 3. Frame types

```text
1  REQUEST
2  RESPONSE
```

Other values are reserved for future protocol versions/features.

A receiver that sees an unknown type MUST NOT attempt to interpret its payload. It MUST skip exactly `payload_length` bytes and continue reading the next frame.

This is the forward-compatibility rule.

## 4. Request payload

The payload is:

```text
+--------------------+----------------+-------------------------+
| path length (u16)  | path bytes     | header entries...       |
+--------------------+----------------+-------------------------+
```

The path is UTF-8 bytes and must begin with `/`.

Example logical request:

```text
path = /index.html
Host = localhost
User-Agent = cn-bcurl/1
```

The path is not NUL-terminated.

## 5. Response payload

The payload is:

```text
+----------------+-------------------------+------------------+
| status (u16)   | header entries...       | body bytes       |
+----------------+-------------------------+------------------+
```

The body occupies all remaining payload bytes after the declared header entries have been parsed.

Examples:

```text
200  successful file response
400  malformed/unsafe request
404  requested file does not exist
500  unexpected server failure
```

## 6. Header encoding

Version 1 defines ten numeric header names:

```text
ID   Name
---  ----------------
1    Content-Type
2    Content-Length
3    Server
4    Connection
5    Date
6    Last-Modified
7    Accept
8    Host
9    User-Agent
10   Cache-Control
```

Each header entry is:

```text
+------+-----------------+----------------------+
| ID   | value length    | value bytes           |
| u8   | u16             | exactly N bytes       |
+------+-----------------+----------------------+
```

Header values are not NUL-terminated.

A version-1 implementation rejects an unknown header ID because the header count and lengths must be unambiguous. Future protocol versions can add IDs while retaining the frame structure.

## 7. Persistence

The server keeps the TCP connection open after a response.

A client may send another request frame on the same connection. The supplied `bcurl` performs one request and exits, deliberately using exactly one TCP connection.

## 8. Error handling

Malformed frame:

```text
bad magic
unsupported version
invalid reserved field
payload length too large
payload length does not match bytes received
invalid header count/entry
invalid request path
```

The server returns a `400` response where it can identify the malformed request as an application-level error.

## 9. Example frame

For a request with path `/index.html`, the first bytes are conceptually:

```text
43 4e             "CN"
01                version 1
01                REQUEST
00                flags
00                reserved
00 02             two headers
00 00 00 19       payload length
00 00 00 01       stream id 1
```

The exact payload bytes depend on the header values.

## 10. Interoperability rule

The server must not depend on implementation-specific C++ object layouts. The wire representation is defined byte-for-byte by this document.

A different implementation written in C, Rust, Python or Java could interoperate if it emits and consumes the same bytes.
