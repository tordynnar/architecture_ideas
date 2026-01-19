# C Protocol Buffer Libraries

This document provides a comprehensive overview of Protocol Buffer libraries available for the C programming language (excluding C++ libraries).

## Overview

Google's official Protocol Buffers implementation does not include native C support. However, several third-party libraries have been developed to fill this gap, ranging from full-featured implementations to specialized embedded-system variants.

---

## Primary Libraries (Actively Maintained)

### 1. protobuf-c

**Repository:** [https://github.com/protobuf-c/protobuf-c](https://github.com/protobuf-c/protobuf-c)
**Documentation:** [https://protobuf-c.github.io/protobuf-c/](https://protobuf-c.github.io/protobuf-c/)
**License:** BSD-2-Clause

The most widely-used pure C implementation of Protocol Buffers.

#### Components
- **libprotobuf-c**: Pure C library for encoding and decoding protobuf messages
- **protoc-gen-c**: Code generator plugin for the `protoc` compiler

#### Features
- Full proto2 support
- Message types, nested messages, scalar types, default values
- Enumerations, packages, packed options
- Extensions support
- Oneofs support
- Follows Semantic Versioning (since v1.0.0)

#### Limitations
- Requires code generation (not dynamic)
- Uses dynamic memory allocation (but customizable with allocators)
- Binary size: ~20+ KB ROM

#### Build Requirements
- C compiler
- C++ compiler (for building protoc plugin)
- protobuf (Google's protobuf library)
- pkg-config
- Autotools (if building from git)

#### Usage
```bash
# Generate C code from .proto file
protoc --c_out=. myprotocol.proto

# This creates myprotocol.pb-c.c and myprotocol.pb-c.h
```

---

### 2. Nanopb

**Repository:** [https://github.com/nanopb/nanopb](https://github.com/nanopb/nanopb)
**Documentation:** [https://jpa.kapsi.fi/nanopb/](https://jpa.kapsi.fi/nanopb/)
**License:** Zlib

A lightweight Protocol Buffers implementation specifically designed for embedded systems and microcontrollers.

#### Target Use Cases
- 32-bit microcontrollers (ARM Cortex-M series)
- Resource-constrained systems (<10 kB ROM, <1 kB RAM)
- Real-time systems requiring static memory allocation

#### Features
- Fully static by default (no dynamic memory allocation)
- Extremely small footprint: ~2-10 KB ROM, ~300 bytes RAM
- Callbacks for variable-length fields
- Field size customization (IS_8, IS_16, etc.)
- Compatible with standard protobuf ecosystem
- Supports most proto2 features

#### Limitations
- Does not support deprecated fields annotations
- Oneofs not fully supported
- Cyclic message handling can be inconvenient
- Favors small size over serialization speed

#### Build Requirements
- Python
- protobuf Python package
- grpcio-tools

#### Usage
```bash
# Install dependencies
pip install --upgrade protobuf grpcio-tools

# Generate C code
python generator/nanopb_generator.py myprotocol.proto

# This creates myprotocol.pb.c and myprotocol.pb.h
```

#### Supported Platforms
- ARM Cortex-M series (32-bit)
- Texas Instruments MSP430 (16-bit)
- Atmel ATmega (8-bit)
- Any ANSI C compatible platform

---

### 3. upb (μpb - Micro Protocol Buffers)

**Repository:** [https://github.com/protocolbuffers/protobuf/tree/main/upb](https://github.com/protocolbuffers/protobuf/tree/main/upb)
**Legacy Repository:** [https://github.com/protocolbuffers/upb](https://github.com/protocolbuffers/upb) (archived)
**License:** BSD-3-Clause

A small, fast protobuf implementation in C maintained by Google as part of the official protobuf repository.

#### Purpose
- Serves as the C kernel for other language bindings (PHP, Ruby)
- Designed to be wrapped by higher-level language implementations
- Provides a minimal, orthogonal API surface

#### Features
- Comparable speed to protobuf C++
- Extremely lightweight: <100 KB object code
- Generated API and reflection support
- Binary and JSON wire formats
- Text format serialization
- Full conformance with protobuf spec (oneofs, maps, unknown fields, extensions)
- Optional reflection (agnostic to reflection linkage)
- No global state (no pre-main registration)
- Fast reflection-based parsing (runtime-loaded messages parse as fast as compiled ones)
- Arena allocator optimizations

#### Limitations
- **C API/ABI not stable** - Not recommended for direct C consumption
- No official releases
- Primarily intended as a building block for language bindings

#### Design Philosophy
upb prioritizes simplicity and small code size over advanced features like lazy fields. It uses a table-driven parser (VM-style interpretation) for efficient message processing.

---

## Secondary Libraries

### 4. pbc (Protocol Buffers for C)

**Repository:** [https://github.com/cloudwu/pbc](https://github.com/cloudwu/pbc)
**License:** MIT

A dynamic Protocol Buffers library that works without code generation.

#### Features
- No code generation required (dynamic/reflection-based)
- Message API (wmessage/rmessage) for easy encoding/decoding
- Pattern API for high-performance native struct access
- Extension support with field name prefixes
- Lua bindings included
- Can parse .proto files directly (via LPeg in Lua)

#### APIs
1. **Message API**: Flexible, uses string or integer for enums
2. **Pattern API**: Faster, less memory, works with native C structs

#### Limitations
- Does not support oneofs
- Does not support proto3
- May not be as actively maintained

#### Installation
Available via vcpkg package manager.

---

### 5. protobluff

**Repository:** [https://github.com/squidfunk/protobluff](https://github.com/squidfunk/protobluff)
**Documentation:** [https://squidfunk.github.io/protobluff/](https://squidfunk.github.io/protobluff/)
**License:** MIT

A modular Protocol Buffers implementation with a unique direct-operation approach.

#### Unique Approach
Unlike other libraries that decode entire messages into memory structures, protobluff operates directly on the encoded data, skipping decoding/encoding steps when reading or writing values.

#### Features
- Full proto2 support (except deprecated groups and generic services)
- Proto3 basic compatibility
- No runtime dependencies (self-contained)
- Adheres to C99 standard
- Compiles on all UNIX systems (Linux, macOS)

#### Build
```bash
./autogen.sh && ./configure && make && make test && make install
```

#### Linking
```bash
# Compiler flags
pkg-config --cflags protobluff

# Linker flags
pkg-config --libs protobluff
```

---

### 6. lwpb (Lightweight Protocol Buffers)

**Repository:** [https://github.com/acg/lwpb](https://github.com/acg/lwpb)
**Original:** [https://code.google.com/archive/p/lwpb](https://code.google.com/archive/p/lwpb)
**License:** Apache 2.0

A lightweight implementation with streaming interface.

#### Features
- Simple streaming interface for encoding and decoding
- Very small: ~31 KB stripped binary
- Python bindings included
- Supports most proto2 features including cyclic messages
- Extension support
- No dependency on Google's C++ API

#### Limitations
- Does not support oneofs
- No proto3 support
- **Project appears inactive**

---

### 7. protobuf-embedded-c

**Repository:** [https://github.com/berezovskyi/protobuf-embedded-c](https://github.com/berezovskyi/protobuf-embedded-c)
**Original:** [https://code.google.com/archive/p/protobuf-embedded-c](https://code.google.com/archive/p/protobuf-embedded-c)

A code generator targeting low-power embedded controllers.

#### Design Goals
- Generated code runs on low-power, low-memory embedded controllers
- Static memory allocation for real-time systems
- Self-contained generated code (no runtime libraries needed)
- API close to Google's original protobuf concepts

#### Supported Platforms
- ARM Cortex M3
- TI MSP430F5438
- IA32 (for in-the-loop testing)

#### Requirements
- Eclipse with ANTLR 3.4
- Google Protocol Buffers 2.4.1

#### Limitations
- Reduced functionality compared to full implementations
- **Project appears inactive**

---

## Quick Comparison Table

| Library | License | Dynamic | Size | Embedded | Proto3 | Oneofs | Active |
|---------|---------|---------|------|----------|--------|--------|--------|
| protobuf-c | BSD-2 | No | ~20KB+ | Partial | Yes | Yes | ✅ |
| nanopb | Zlib | No | 2-10KB | ✅ | Partial | Yes | ✅ |
| upb | BSD-3 | Yes | <100KB | No | Yes | Yes | ✅ |
| pbc | MIT | Yes | Medium | No | No | No | ⚠️ |
| protobluff | MIT | No | Small | No | Partial | Yes | ⚠️ |
| lwpb | Apache 2 | No | ~31KB | Partial | No | No | ❌ |
| protobuf-embedded-c | - | No | Small | ✅ | No | No | ❌ |

**Legend:**
- ✅ Active/Supported
- ⚠️ Limited activity
- ❌ Inactive/Abandoned

---

## Detailed Feature Support Matrix

### Proto Syntax & Version Support

| Feature | protobuf-c | nanopb | upb | pbc | protobluff | lwpb |
|---------|:----------:|:------:|:---:|:---:|:----------:|:----:|
| **Proto2 syntax** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| **Proto3 syntax** | ✅ | ✅¹ | ✅ | ❌ | ⚠️² | ❌ |
| **Editions support** | 🔄³ | ❌ | ✅ | ❌ | ❌ | ❌ |

¹ Nanopb supports proto3 singular fields since v0.3.7, but not all proto3 features
² Protobluff is wire-compatible with proto3 but not optimized for proto3 syntax
³ Editions support is in progress for protobuf-c (as of Jan 2025)

### Message & Field Types

| Feature | protobuf-c | nanopb | upb | pbc | protobluff | lwpb |
|---------|:----------:|:------:|:---:|:---:|:----------:|:----:|
| **Scalar types** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| **Nested messages** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| **Enumerations** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| **Repeated fields** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| **Packed repeated** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| **Default values** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| **Bytes fields** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| **String fields** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |

### Advanced Field Types

| Feature | protobuf-c | nanopb | upb | pbc | protobluff | lwpb |
|---------|:----------:|:------:|:---:|:---:|:----------:|:----:|
| **Oneof** | ✅ | ✅ | ✅ | ❌ | ✅ | ❌ |
| **Map<K,V>** | ✅¹ | ⚠️² | ✅ | ❌ | ⚠️³ | ❌ |
| **Extensions** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| **Groups (deprecated)** | ⚠️ | ❌ | ⚠️ | ❌ | ❌ | ❌ |
| **Cyclic messages** | ✅ | ⚠️⁴ | ✅ | ✅ | ✅ | ✅ |

¹ Maps are wire-compatible (treated as repeated key-value messages)
² Nanopb has basic map support via callbacks; nanopb_cpp provides full support
³ Protobluff notes maps need to be implemented
⁴ Nanopb can handle cyclic messages but it's inconvenient

### Proto3-Specific Features

| Feature | protobuf-c | nanopb | upb | pbc | protobluff | lwpb |
|---------|:----------:|:------:|:---:|:---:|:----------:|:----:|
| **optional keyword** | ✅ | ✅ | ✅ | ❌ | ⚠️ | ❌ |
| **Explicit presence (has_)** | ✅ | ✅¹ | ✅ | ❌ | ⚠️ | ❌ |
| **Implicit presence** | ✅ | ✅ | ✅ | ❌ | ⚠️ | ❌ |
| **No required fields** | ✅ | ✅ | ✅ | ❌ | ⚠️ | ❌ |

¹ Nanopb has `proto3` and `proto3_singular_msgs` options for controlling has_ field generation

### Well-Known Types (google.protobuf.*)

| Feature | protobuf-c | nanopb | upb | pbc | protobluff | lwpb |
|---------|:----------:|:------:|:---:|:---:|:----------:|:----:|
| **Any** | ⚠️¹ | ⚠️² | ✅ | ❌ | ⚠️³ | ❌ |
| **Timestamp** | ⚠️¹ | ⚠️² | ✅ | ❌ | ⚠️ | ❌ |
| **Duration** | ⚠️¹ | ⚠️² | ✅ | ❌ | ⚠️ | ❌ |
| **Struct** | ⚠️¹ | ⚠️² | ✅ | ❌ | ⚠️ | ❌ |
| **Wrappers** | ⚠️¹ | ⚠️² | ✅ | ❌ | ⚠️ | ❌ |
| **FieldMask** | ⚠️¹ | ⚠️² | ✅ | ❌ | ⚠️ | ❌ |

¹ protobuf-c requires manual compilation of google/protobuf/*.proto files with protoc-c; no built-in pack/unpack helpers
² nanopb requires explicit compilation of well-known type protos; can copy-paste message definitions as workaround
³ protobluff notes Any type needs implementation

### Wire Format Support

| Feature | protobuf-c | nanopb | upb | pbc | protobluff | lwpb |
|---------|:----------:|:------:|:---:|:---:|:----------:|:----:|
| **Binary encoding** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| **Binary decoding** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| **JSON encoding** | ❌¹ | ❌ | ✅ | ❌ | ❌ | ❌ |
| **JSON decoding** | ❌¹ | ❌ | ✅ | ❌ | ❌ | ❌ |
| **Text format encode** | ⚠️² | ❌ | ✅ | ❌ | ❌ | ❌ |
| **Text format decode** | ⚠️² | ❌ | ✅ | ❌ | ❌ | ❌ |
| **Unknown fields** | ✅ | ⚠️ | ✅ | ⚠️ | ⚠️ | ⚠️ |

¹ protobuf-c does not include JSON support in core library
² Text format available via separate [protobuf-c-text](https://github.com/protobuf-c/protobuf-c-text) library

### RPC & Streaming Support

| Feature | protobuf-c | nanopb | upb | pbc | protobluff | lwpb |
|---------|:----------:|:------:|:---:|:---:|:----------:|:----:|
| **Service definitions** | ⚠️¹ | ❌² | ❌³ | ❌ | ❌ | ⚠️⁴ |
| **Unary RPC** | ⚠️¹ | ⚠️⁵ | ❌ | ❌ | ❌ | ⚠️⁴ |
| **Server streaming** | ❌ | ⚠️⁵ | ❌ | ❌ | ❌ | ❌ |
| **Client streaming** | ❌ | ⚠️⁵ | ❌ | ❌ | ❌ | ❌ |
| **Bidirectional streaming** | ❌ | ⚠️⁵ | ❌ | ❌ | ❌ | ❌ |
| **gRPC integration** | ❌⁶ | ❌⁶ | ❌⁶ | ❌ | ❌ | ❌ |

¹ RPC split out to separate [protobuf-c-rpc](https://github.com/protobuf-c/protobuf-c-rpc) project (simple RPC, not gRPC)
² nanopb itself has no RPC; service definitions ignored
³ upb is serialization-only; designed to be wrapped by language bindings
⁴ lwpb has custom service implementation
⁵ Streaming RPC available via [Pigweed pw_rpc](https://pigweed.dev/pw_rpc/nanopb/) with nanopb (supports all 4 RPC types)
⁶ Full gRPC in C requires [gRPC Core](https://github.com/grpc/grpc) library or [grpc-c](https://github.com/Juniper/grpc-c) wrapper

### Memory & Runtime Characteristics

| Feature | protobuf-c | nanopb | upb | pbc | protobluff | lwpb |
|---------|:----------:|:------:|:---:|:---:|:----------:|:----:|
| **Static allocation** | ❌¹ | ✅ | ❌ | ❌ | ❌ | ❌ |
| **Dynamic allocation** | ✅ | ⚠️² | ✅ | ✅ | ✅ | ✅ |
| **Custom allocators** | ✅ | ✅ | ✅ | ❌ | ❌ | ❌ |
| **Arena allocation** | ❌ | ❌ | ✅ | ❌ | ❌ | ❌ |
| **Callback interface** | ❌ | ✅ | ❌ | ❌ | ❌ | ❌ |
| **Reflection** | ✅ | ❌ | ✅ | ✅ | ⚠️ | ❌ |
| **Code generation** | ✅ | ✅ | ✅ | ❌³ | ✅ | ✅ |

¹ protobuf-c uses dynamic allocation by default but supports custom allocators
² nanopb is static by default; optional dynamic allocation available
³ pbc works without code generation (dynamic/reflection-based)

### Code Generation & Tooling

| Feature | protobuf-c | nanopb | upb | pbc | protobluff | lwpb |
|---------|:----------:|:------:|:---:|:---:|:----------:|:----:|
| **protoc plugin** | ✅ | ✅ | ✅ | N/A | ✅ | ✅ |
| **Standalone generator** | ❌ | ✅¹ | ❌ | N/A | ❌ | ❌ |
| **Parse .proto at runtime** | ❌ | ❌ | ❌ | ✅² | ❌ | ❌ |

¹ nanopb has both protoc plugin and standalone Python generator
² pbc can parse .proto files directly via Lua bindings with LPeg

### Platform & Standard Compliance

| Feature | protobuf-c | nanopb | upb | pbc | protobluff | lwpb |
|---------|:----------:|:------:|:---:|:---:|:----------:|:----:|
| **C standard** | C89+ | ANSI C | C99 | C99 | C99 | C99 |
| **POSIX required** | No | No | No | No | Yes¹ | No |
| **Embedded friendly** | ⚠️ | ✅ | ❌ | ❌ | ❌ | ⚠️ |
| **Bare metal** | ⚠️ | ✅ | ❌ | ❌ | ❌ | ⚠️ |

¹ protobluff uses POSIX for some functionality

---

### Feature Matrix Legend

| Symbol | Meaning |
|--------|---------|
| ✅ | Fully supported |
| ⚠️ | Partial support / requires workaround / separate library |
| ❌ | Not supported |
| 🔄 | In development |
| N/A | Not applicable |

---

### Notes on gRPC Support in C

None of the pure C protobuf libraries provide built-in gRPC support. For gRPC functionality in C, consider:

1. **gRPC Core Library** ([github.com/grpc/grpc](https://github.com/grpc/grpc)) - The official low-level C core, but with a complex API
2. **grpc-c** ([github.com/Juniper/grpc-c](https://github.com/Juniper/grpc-c)) - A C wrapper around gRPC Core (pre-alpha)
3. **Pigweed pw_rpc** ([pigweed.dev/pw_rpc](https://pigweed.dev/pw_rpc/nanopb/)) - Mature RPC framework supporting nanopb with all streaming types

### Notes on Any Type Support

The `google.protobuf.Any` well-known type allows embedding arbitrary messages. In official implementations (C++, Java, Python), helper methods like `Pack()` and `Unpack()` are provided. C libraries generally:
- Can serialize/deserialize `Any` as a regular message (type_url + value bytes)
- Do **not** provide convenience pack/unpack helpers
- Require manual handling of the inner message type resolution

---

## Recommendations

### For General-Purpose Applications
**Use protobuf-c** - The most mature and widely-used C implementation with full feature support and active maintenance.

### For Embedded Systems / Microcontrollers
**Use nanopb** - Purpose-built for constrained environments with excellent memory efficiency and static allocation.

### For Language Binding Development
**Consider upb** - Google's official C kernel, but note the unstable API. Best used as a foundation for building higher-level bindings.

### For Dynamic/Reflection-Based Needs
**Consider pbc** - Works without code generation, useful for applications needing runtime schema flexibility.

### For Direct Data Manipulation
**Consider protobluff** - Unique approach that operates directly on encoded data without full decode/encode cycles.

---

## Benchmark Notes

A benchmark comparison of embedded Protocol Buffers implementations exists at [https://jpa.kapsi.fi/nanopb/benchmark/](https://jpa.kapsi.fi/nanopb/benchmark/), measuring:
- Execution speed
- RAM usage
- Compiled code size

Tests were performed on ARM Cortex-M3 and Atmel ATmega128 platforms. Note that this benchmark is from 2015 and may not reflect current library performance.

---

## References

- [Protocol Buffers Documentation](https://protobuf.dev)
- [protobuf-c GitHub](https://github.com/protobuf-c/protobuf-c)
- [nanopb GitHub](https://github.com/nanopb/nanopb)
- [upb in protobuf repo](https://github.com/protocolbuffers/protobuf/tree/main/upb)
- [pbc GitHub](https://github.com/cloudwu/pbc)
- [protobluff Documentation](https://squidfunk.github.io/protobluff/)
- [Embedded Protocol Buffers Benchmark](https://jpa.kapsi.fi/nanopb/benchmark/)
