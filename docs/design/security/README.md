# Security Module

Inner-TLS encryption layer that runs over an abstract transport (e.g. a WebSocket connection).

## Overview

Provides TLS encryption over any bidirectional byte transport (`ITransport`), using OpenSSL memory BIOs. The TLS session is established *inside* an already-open transport — no TCP sockets involved.

## Components

| Class | Role |
|---|---|
| `ITransport` | Abstract interface: `send(bytes)` / `recv(bytes)` |
| `InnerTlsClient` | Client-side TLS session (initiates handshake) |
| `InnerTlsServer` | Server-side TLS session (accepts handshake, requires cert + key) |

## Usage

```cpp
// After WebSocket upgrade, wrap the WS transport:
InnerTlsClient tls(wsTransport);
tls.handshake();                          // TLS handshake over WS frames
tls.send(plaintext);                      // encrypt + send
tls.recv(plaintext);                      // receive + decrypt
```

The server side loads a PEM certificate and private key:

```cpp
InnerTlsServer tls(wsTransport, "/certs/server.crt", "/certs/server.key");
tls.accept();                             // accept TLS handshake
```

Optional mTLS: pass a CA path to `InnerTlsServer` (4th arg) or call `set_ca()` on the client.

## Design

- **Memory BIOs**: `BIO_s_mem()` is used for both read and write BIOs. SSL reads from `rbio` (fed by transport `recv`) and writes to `wbio` (drained via `flush_wbio()` → transport `send`).
- **Handshake loop**: `SSL_connect`/`SSL_accept` in a loop, flushing `wbio` on every iteration before attempting to read — SSL may have written output before returning `WANT_READ`.
- **Post-handshake flush**: `wbio` is flushed after the handshake returns success to catch any final flight (e.g. TLS 1.3 `Finished`).
- **Transport decoupling**: `ITransport` is the only dependency, making the TLS layer testable with a mock transport.

## Test certs

Self-signed test certificates are generated during the Docker build at `/src/certs/server.crt` and `/src/certs/server.key`. Tests skip if certs are not found.
