# gRPC Libraries for C

This document provides a comprehensive overview of gRPC libraries and RPC frameworks available for the C programming language, along with a detailed feature matrix.

## Overview

Unlike many other languages, C does not have official first-class gRPC support from Google. The official gRPC repository provides a low-level C-core library that serves as the foundation for other language bindings (Python, Ruby, PHP, C#, etc.), but it's not designed for direct C consumption. Several third-party projects have emerged to fill this gap.

---

## Available Libraries

### 1. gRPC Core (Low-Level C API)

**Repository:** [https://github.com/grpc/grpc](https://github.com/grpc/grpc)
**Documentation:** [https://grpc.github.io/grpc/core/](https://grpc.github.io/grpc/core/)
**License:** Apache 2.0

The official gRPC C-core library provides low-level APIs for building gRPC functionality.

#### Components
- **grpc.h**: Top-level API for channels, calls, and completion queues
- **grpc_security.h**: TLS/SSL and authentication functionality
- **credentials.h**: Credential management for secure connections

#### Key Characteristics
- Extremely low-level API ("pretty rough" per gRPC team)
- No built-in protobuf serialization helpers
- Requires manual management of completion queues
- Public headers maintain C89 compatibility
- Serves as foundation for all other gRPC language implementations

#### Key Functions
- `grpc_channel_create()` - Create a channel to a server
- `grpc_call_start_batch()` - Start a batch of operations
- `grpc_completion_queue_next()` - Wait for completion events
- `grpc_channel_check_connectivity_state()` - Check connection status

#### Limitations
- No examples for direct C usage provided by gRPC team
- Missing conveniences for method invocation and protobuf serialization
- Steep learning curve
- Primarily intended to be wrapped by higher-level libraries

---

### 2. grpc-c (Juniper Networks)

**Repository:** [https://github.com/Juniper/grpc-c](https://github.com/Juniper/grpc-c)
**License:** BSD-2-Clause
**Status:** Pre-alpha (APIs may change)

A C implementation of gRPC layered on top of the core libgrpc library, developed by Juniper Networks.

#### Features
- Higher-level C API wrapping gRPC Core
- Code generator (`protoc-gen-grpc-c`) for service stubs
- Callback-based RPC handling
- Support for all four RPC types (see streaming examples)

#### Dependencies
- gRPC v1.3.0
- Protocol Buffers 3.0
- protobuf-c 1.2.1

#### Streaming Examples Provided
- `bidi_streaming_server.c` / `bidi_streaming.proto`
- `client_streaming_server.c` / `client_streaming.proto`
- `server_streaming_client.c`
- Basic unary `foo_client.c` / `foo_server.c`

#### Build
```bash
autoreconf --install
git submodule update --init
./builddeps.sh
mkdir build && cd build
../configure
make && sudo make install
```

#### Limitations
- Pre-alpha status - APIs unstable
- Limited documentation
- Unix-like systems only (no Windows support documented)
- Development activity has slowed

---

### 3. grpc-c (linimbus fork)

**Repository:** [https://github.com/linimbus/grpc-c](https://github.com/linimbus/grpc-c)
**License:** BSD-2-Clause
**Status:** Limited activity

A fork of grpc-c with vcpkg + CMake build system and multi-platform support.

#### Features
- Cross-platform support (Windows, Linux, macOS)
- Modern CMake build system
- vcpkg dependency management
- Similar API to Juniper's grpc-c

#### Dependencies
- vcpkg
- CMake
- Protobuf
- protobuf-c
- gRPC

#### Build
```bash
# Linux/macOS
./build_release.sh

# Windows
./build_release.bat
```

#### Build Output
- `libgrpc-c.dylib` / `libgrpc-c.a`
- `grpc-c.h`
- `protoc-gen-grpc-c`

#### Limitations
- Limited documentation
- Only 3 commits on master branch
- No releases published
- Unclear maintenance status

---

### 4. protobuf-c-rpc

**Repository:** [https://github.com/protobuf-c/protobuf-c-rpc](https://github.com/protobuf-c/protobuf-c-rpc)
**License:** BSD-2-Clause
**Status:** Maintenance mode

A simple RPC library for protobuf-c. Note: This is **not** gRPC-compatible.

#### Features
- Simple unary RPC support
- Integrates with protobuf-c
- Lightweight implementation
- Custom protocol (not HTTP/2)

#### Dependencies
- C compiler
- protobuf-c
- pkg-config
- Google Protocol Buffers

#### Limitations
- **Not gRPC compatible** (custom protocol)
- No streaming support
- No HTTP/2
- No TLS/authentication built-in
- Limited documentation on features

---

### 5. Pigweed pw_rpc

**Repository:** [https://pigweed.dev/pw_rpc/](https://pigweed.dev/pw_rpc/)
**Source:** [https://github.com/google/pigweed](https://github.com/google/pigweed)
**License:** Apache 2.0
**Status:** Actively maintained by Google

An efficient, low-code-size RPC system designed for embedded devices. Note: This uses gRPC-like semantics but a custom protocol.

#### Features
- All four RPC types (unary, server streaming, client streaming, bidirectional)
- Multiple protobuf backends (nanopb, pw_protobuf)
- Transport-agnostic (UART, HDLC, custom)
- Extremely small code size (~4KB for core server)
- Static memory allocation friendly
- C++ API with C compatibility layer

#### Supported Languages
- C++ (primary)
- Python
- TypeScript/JavaScript
- Java

#### Key Differences from gRPC
| Aspect | gRPC | pw_rpc |
|--------|------|--------|
| Transport | HTTP/2 required | Any transport (UART, HDLC, etc.) |
| Protocol | gRPC over HTTP/2 | Custom packet protocol |
| Target | Servers, microservices | Embedded devices, MCUs |
| Code size | Large | ~4KB core |
| Memory | Dynamic allocation | Static allocation friendly |

#### Limitations
- Not wire-compatible with standard gRPC
- Primarily C++ (C usage requires wrappers)
- Custom protocol means no interop with gRPC clients/servers
- Documentation still under construction for some streaming types

---

### 6. nanogrpc (Abandoned)

**Repository:** [https://github.com/d21d3q/nanogrpc](https://github.com/d21d3q/nanogrpc)
**License:** MIT
**Status:** Abandoned

An attempt to build gRPC-like functionality on top of nanopb for embedded systems.

#### Features (When Active)
- Unary RPC support
- Server streaming support
- Method identification via CRC32 hash (bandwidth optimization)
- Serial interface transport (UART, RS232, RS485)
- nanopb integration

#### Design Goals
- Firmata-like communication for embedded devices
- Low-bandwidth serial communication
- Control sensors and ADCs via RPC

#### Limitations
- **Project abandoned**
- Not wire-compatible with gRPC
- No HTTP/2 support
- Limited streaming support
- No security/authentication

---

## Feature Support Matrix

### RPC Types

| Feature | gRPC Core | grpc-c (Juniper) | grpc-c (linimbus) | protobuf-c-rpc | pw_rpc | nanogrpc |
|---------|:---------:|:----------------:|:-----------------:|:--------------:|:------:|:--------:|
| **Unary RPC** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| **Server streaming** | ✅ | ✅ | ✅¹ | ❌ | ✅ | ✅ |
| **Client streaming** | ✅ | ✅ | ✅¹ | ❌ | ✅ | ❌ |
| **Bidirectional streaming** | ✅ | ✅ | ✅¹ | ❌ | ✅ | ❌ |

¹ Assumed based on Juniper fork compatibility

### Protocol & Transport

| Feature | gRPC Core | grpc-c (Juniper) | grpc-c (linimbus) | protobuf-c-rpc | pw_rpc | nanogrpc |
|---------|:---------:|:----------------:|:-----------------:|:--------------:|:------:|:--------:|
| **HTTP/2 transport** | ✅ | ✅ | ✅ | ❌ | ❌ | ❌ |
| **gRPC wire protocol** | ✅ | ✅ | ✅ | ❌ | ❌ | ❌ |
| **Custom transport** | ⚠️¹ | ❌ | ❌ | ✅ | ✅ | ✅ |
| **Serial/UART** | ❌ | ❌ | ❌ | ⚠️ | ✅ | ✅ |
| **HDLC framing** | ❌ | ❌ | ❌ | ❌ | ✅ | ⚠️ |

¹ gRPC Core supports custom transports but requires significant implementation effort

### Security & Authentication

| Feature | gRPC Core | grpc-c (Juniper) | grpc-c (linimbus) | protobuf-c-rpc | pw_rpc | nanogrpc |
|---------|:---------:|:----------------:|:-----------------:|:--------------:|:------:|:--------:|
| **TLS/SSL** | ✅ | ✅¹ | ✅¹ | ❌ | ❌² | ❌ |
| **mTLS (mutual TLS)** | ✅ | ⚠️ | ⚠️ | ❌ | ❌ | ❌ |
| **Token authentication** | ✅ | ⚠️ | ⚠️ | ❌ | ❌ | ❌ |
| **ALTS** | ✅ | ❌ | ❌ | ❌ | ❌ | ❌ |
| **Custom credentials** | ✅ | ⚠️ | ⚠️ | ❌ | ⚠️ | ❌ |
| **Call credentials** | ✅ | ⚠️ | ⚠️ | ❌ | ❌ | ❌ |

¹ Inherits from gRPC Core
² Security handled at transport layer, not RPC layer

### Call Features

| Feature | gRPC Core | grpc-c (Juniper) | grpc-c (linimbus) | protobuf-c-rpc | pw_rpc | nanogrpc |
|---------|:---------:|:----------------:|:-----------------:|:--------------:|:------:|:--------:|
| **Deadlines/timeouts** | ✅ | ✅ | ✅ | ❌ | ⚠️ | ❌ |
| **Cancellation** | ✅ | ✅ | ✅ | ❌ | ✅ | ❌ |
| **Metadata** | ✅ | ✅ | ✅ | ❌ | ⚠️ | ❌ |
| **Compression** | ✅ | ✅ | ✅ | ❌ | ❌ | ❌ |
| **Flow control** | ✅ | ✅ | ✅ | ❌ | ⚠️ | ❌ |

### Service Features

| Feature | gRPC Core | grpc-c (Juniper) | grpc-c (linimbus) | protobuf-c-rpc | pw_rpc | nanogrpc |
|---------|:---------:|:----------------:|:-----------------:|:--------------:|:------:|:--------:|
| **Health checking** | ✅¹ | ⚠️ | ⚠️ | ❌ | ❌ | ❌ |
| **Server reflection** | ❌² | ❌ | ❌ | ❌ | ❌ | ❌ |
| **Load balancing** | ✅ | ✅³ | ✅³ | ❌ | ❌ | ❌ |
| **Service discovery** | ✅ | ✅³ | ✅³ | ❌ | ❌ | ❌ |
| **Interceptors** | ✅ | ⚠️ | ⚠️ | ❌ | ⚠️ | ❌ |
| **Retry policies** | ✅ | ⚠️ | ⚠️ | ❌ | ❌ | ❌ |

¹ Health checking protocol defined, but C implementation requires manual work
² Reflection requires C++ library (`libgrpc++_reflection`)
³ Inherits from gRPC Core capabilities

### Code Generation & Protobuf

| Feature | gRPC Core | grpc-c (Juniper) | grpc-c (linimbus) | protobuf-c-rpc | pw_rpc | nanogrpc |
|---------|:---------:|:----------------:|:-----------------:|:--------------:|:------:|:--------:|
| **protoc plugin** | ❌¹ | ✅ | ✅ | ✅ | ✅ | ✅ |
| **protobuf-c support** | ❌ | ✅ | ✅ | ✅ | ❌ | ❌ |
| **nanopb support** | ❌ | ❌ | ❌ | ❌ | ✅ | ✅ |
| **pw_protobuf support** | ❌ | ❌ | ❌ | ❌ | ✅ | ❌ |

¹ gRPC Core has no C-specific protoc plugin; uses C++ plugin

### Platform Support

| Feature | gRPC Core | grpc-c (Juniper) | grpc-c (linimbus) | protobuf-c-rpc | pw_rpc | nanogrpc |
|---------|:---------:|:----------------:|:-----------------:|:--------------:|:------:|:--------:|
| **Linux** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| **macOS** | ✅ | ✅ | ✅ | ✅ | ✅ | ⚠️ |
| **Windows** | ✅ | ❌ | ✅ | ⚠️ | ✅ | ❌ |
| **Embedded/MCU** | ❌ | ❌ | ❌ | ⚠️ | ✅ | ✅ |
| **Bare metal** | ❌ | ❌ | ❌ | ⚠️ | ✅ | ✅ |

### Memory & Performance

| Feature | gRPC Core | grpc-c (Juniper) | grpc-c (linimbus) | protobuf-c-rpc | pw_rpc | nanogrpc |
|---------|:---------:|:----------------:|:-----------------:|:--------------:|:------:|:--------:|
| **Static allocation** | ❌ | ❌ | ❌ | ⚠️ | ✅ | ✅ |
| **Dynamic allocation** | ✅ | ✅ | ✅ | ✅ | ⚠️ | ⚠️ |
| **Small code size** | ❌ | ❌ | ❌ | ⚠️ | ✅ | ✅ |
| **Low RAM usage** | ❌ | ❌ | ❌ | ⚠️ | ✅ | ✅ |

### Project Status

| Aspect | gRPC Core | grpc-c (Juniper) | grpc-c (linimbus) | protobuf-c-rpc | pw_rpc | nanogrpc |
|--------|:---------:|:----------------:|:-----------------:|:--------------:|:------:|:--------:|
| **Maintenance** | ✅ Active | ⚠️ Low | ⚠️ Low | ⚠️ Low | ✅ Active | ❌ Abandoned |
| **Documentation** | ✅ Good | ⚠️ Limited | ⚠️ Limited | ⚠️ Limited | ✅ Good | ⚠️ Limited |
| **Production ready** | ✅¹ | ❌ Pre-alpha | ❌ | ⚠️ | ✅ | ❌ |
| **Community** | ✅ Large | ⚠️ Small | ⚠️ Small | ⚠️ Small | ✅ Growing | ❌ None |

¹ Production ready when used via supported language bindings (C++, Python, etc.)

---

### Legend

| Symbol | Meaning |
|--------|---------|
| ✅ | Fully supported |
| ⚠️ | Partial support / requires work / inherited |
| ❌ | Not supported |

---

## Recommendations

### For Standard gRPC Compatibility
**Use grpc-c (Juniper)** if you need wire-compatible gRPC in C. Accept that it's pre-alpha and may require debugging. Alternatively, consider writing a thin C wrapper around gRPC C++ if stability is critical.

### For Embedded Systems
**Use Pigweed pw_rpc** - It's actively maintained by Google, designed for resource-constrained devices, and supports all streaming types. Note that it's not wire-compatible with standard gRPC.

### For Simple RPC Needs
**Use protobuf-c-rpc** if you only need basic unary RPC and don't require gRPC compatibility. It's lightweight and integrates well with protobuf-c.

### For Production Systems
Consider using the **gRPC C++ library** with a C wrapper layer if you need production-grade reliability. The pure C options are either pre-alpha (grpc-c) or not gRPC-compatible (pw_rpc, protobuf-c-rpc).

---

## Key Considerations

### gRPC in C is Challenging

1. **No official support**: Google doesn't provide a first-class C API for gRPC
2. **C-core is internal**: The C-core library is designed as infrastructure for other languages
3. **Pre-alpha wrappers**: The available C wrappers (grpc-c) are not production-ready
4. **Alternative protocols**: For embedded use cases, consider pw_rpc which provides similar semantics without HTTP/2 overhead

### When to Use Each Option

| Use Case | Recommended Library |
|----------|---------------------|
| Microservices needing gRPC interop | grpc-c (Juniper) or C++ wrapper |
| Embedded device communication | pw_rpc with nanopb |
| Simple internal RPC | protobuf-c-rpc |
| Cross-platform with vcpkg/CMake | grpc-c (linimbus) |
| Production systems | gRPC C++ with C wrapper |

---

## References

- [gRPC Official Documentation](https://grpc.io/docs/)
- [gRPC Core API Reference](https://grpc.github.io/grpc/core/)
- [grpc-c (Juniper) GitHub](https://github.com/Juniper/grpc-c)
- [grpc-c (linimbus) GitHub](https://github.com/linimbus/grpc-c)
- [protobuf-c-rpc GitHub](https://github.com/protobuf-c/protobuf-c-rpc)
- [Pigweed pw_rpc Documentation](https://pigweed.dev/pw_rpc/)
- [gRPC Health Checking Protocol](https://github.com/grpc/grpc/blob/master/doc/health-checking.md)
- [gRPC Load Balancing](https://grpc.io/blog/grpc-load-balancing/)
- [gRPC Authentication](https://grpc.io/docs/guides/auth/)
