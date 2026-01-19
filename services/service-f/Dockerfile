# Service F - Pure C gRPC Implementation
# Multi-stage build for minimal final image

FROM ubuntu:22.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

# Install build dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    pkg-config \
    autoconf \
    automake \
    libtool \
    curl \
    libssl-dev \
    zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build

# Build protobuf from source with shared libraries
ARG PROTOBUF_VERSION=25.1
RUN git clone --depth 1 --branch v${PROTOBUF_VERSION} https://github.com/protocolbuffers/protobuf.git && \
    cd protobuf && \
    git submodule update --init --recursive && \
    cmake -B build -DCMAKE_BUILD_TYPE=Release \
        -Dprotobuf_BUILD_TESTS=OFF \
        -DBUILD_SHARED_LIBS=ON \
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
        -Dprotobuf_ABSL_PROVIDER=module && \
    cmake --build build -j$(nproc) && \
    cmake --install build && \
    ldconfig

# Build gRPC from source (C core) with shared libraries
ARG GRPC_VERSION=1.60.0
ENV LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH
RUN git clone --depth 1 --branch v${GRPC_VERSION} https://github.com/grpc/grpc.git && \
    cd grpc && \
    git submodule update --init --recursive && \
    cmake -B build -DCMAKE_BUILD_TYPE=Release \
        -DgRPC_INSTALL=ON \
        -DgRPC_BUILD_TESTS=OFF \
        -DgRPC_PROTOBUF_PROVIDER=package \
        -DgRPC_SSL_PROVIDER=package \
        -DgRPC_ZLIB_PROVIDER=package \
        -DBUILD_SHARED_LIBS=ON \
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON && \
    cmake --build build -j$(nproc) && \
    cmake --install build && \
    ldconfig

# Build protobuf-c with proto3 optional field support
ARG PROTOBUF_C_VERSION=1.5.0
RUN git clone --depth 1 --branch v${PROTOBUF_C_VERSION} https://github.com/protobuf-c/protobuf-c.git && \
    cd protobuf-c && \
    # Patch to add proto3 optional field support (see https://github.com/protobuf-c/protobuf-c/issues/476)
    # Replace the class definition with one that includes GetSupportedFeatures
    sed -i 's/class PROTOC_C_EXPORT CGenerator : public CodeGenerator {/class PROTOC_C_EXPORT CGenerator : public CodeGenerator {\n public:\n  uint64_t GetSupportedFeatures() const override { return FEATURE_PROTO3_OPTIONAL; }/' protoc-c/c_generator.h && \
    grep -A 10 "class PROTOC_C_EXPORT CGenerator" protoc-c/c_generator.h && \
    ./autogen.sh && \
    ./configure && \
    make -j$(nproc) && \
    make install

# Update library cache
RUN ldconfig

WORKDIR /app

# Copy proto files
COPY proto/ ./proto/

# Download OpenTelemetry proto files (v1.0.0 - with patched protobuf-c for proto3 optional support)
RUN mkdir -p otlp-proto && \
    cd otlp-proto && \
    curl -L -o opentelemetry-proto.tar.gz \
        https://github.com/open-telemetry/opentelemetry-proto/archive/refs/tags/v1.0.0.tar.gz && \
    tar -xzf opentelemetry-proto.tar.gz --strip-components=1 && \
    rm opentelemetry-proto.tar.gz

# Generate protobuf-c files for our services
RUN mkdir -p generated && \
    protoc-c --proto_path=./proto \
             --c_out=./generated \
             ./proto/common.proto ./proto/services.proto

# Generate protobuf-c files for OTLP (traces, logs, and metrics)
RUN protoc-c --proto_path=./otlp-proto \
             --c_out=./generated \
             ./otlp-proto/opentelemetry/proto/common/v1/common.proto \
             ./otlp-proto/opentelemetry/proto/resource/v1/resource.proto \
             ./otlp-proto/opentelemetry/proto/trace/v1/trace.proto \
             ./otlp-proto/opentelemetry/proto/collector/trace/v1/trace_service.proto \
             ./otlp-proto/opentelemetry/proto/logs/v1/logs.proto \
             ./otlp-proto/opentelemetry/proto/collector/logs/v1/logs_service.proto \
             ./otlp-proto/opentelemetry/proto/metrics/v1/metrics.proto \
             ./otlp-proto/opentelemetry/proto/collector/metrics/v1/metrics_service.proto

# Copy source files
COPY services/service-f/src/ ./src/

# List generated files for debugging
RUN find generated -name "*.pb-c.*" | head -50

# List available libraries for debugging
RUN ls -la /usr/local/lib/*.so 2>/dev/null | head -20 || echo "No .so files"
RUN ls -la /usr/local/lib/*.a 2>/dev/null | head -20 || echo "No .a files"

# Find all absl libraries
RUN find /usr/local/lib -name "libabsl*" -type f 2>/dev/null | head -30 || echo "No absl libs"

# Compile the service using shared libraries
# Build gRPC with shared libs enabled, link against them
# Metrics exporter now included with patched protobuf-c proto3 optional support
RUN g++ -O2 -Wall \
    -I./generated \
    -I./src \
    -I/usr/local/include \
    -x c src/main.c \
    -x c src/otlp_exporter.c \
    -x c src/otlp_log_exporter.c \
    -x c src/otlp_metrics_exporter.c \
    -x c generated/common.pb-c.c \
    -x c generated/services.pb-c.c \
    -x c generated/opentelemetry/proto/common/v1/common.pb-c.c \
    -x c generated/opentelemetry/proto/resource/v1/resource.pb-c.c \
    -x c generated/opentelemetry/proto/trace/v1/trace.pb-c.c \
    -x c generated/opentelemetry/proto/collector/trace/v1/trace_service.pb-c.c \
    -x c generated/opentelemetry/proto/logs/v1/logs.pb-c.c \
    -x c generated/opentelemetry/proto/collector/logs/v1/logs_service.pb-c.c \
    -x c generated/opentelemetry/proto/metrics/v1/metrics.pb-c.c \
    -x c generated/opentelemetry/proto/collector/metrics/v1/metrics_service.pb-c.c \
    -o service-f \
    -L/usr/local/lib \
    -lgrpc -lgpr -lprotobuf-c \
    -lssl -lcrypto -lz -lpthread -lm -ldl \
    -Wl,-rpath,/usr/local/lib

# Verify binary
RUN ldd service-f

# Runtime image
FROM ubuntu:22.04

RUN apt-get update && apt-get install -y \
    libssl3 \
    zlib1g \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Copy libraries from builder
COPY --from=builder /usr/local/lib/libgrpc*.so* /usr/local/lib/
COPY --from=builder /usr/local/lib/libgpr*.so* /usr/local/lib/
COPY --from=builder /usr/local/lib/libprotobuf-c*.so* /usr/local/lib/
COPY --from=builder /usr/local/lib/libaddress_sorting*.so* /usr/local/lib/
COPY --from=builder /usr/local/lib/libupb*.so* /usr/local/lib/
COPY --from=builder /usr/local/lib/libre2*.so* /usr/local/lib/
COPY --from=builder /usr/local/lib/libcares*.so* /usr/local/lib/
COPY --from=builder /usr/local/lib/libabsl*.so* /usr/local/lib/
COPY --from=builder /usr/local/lib/libprotobuf*.so* /usr/local/lib/
COPY --from=builder /usr/local/lib/libutf8*.so* /usr/local/lib/

# Update library cache
RUN ldconfig

# Copy binary
COPY --from=builder /app/service-f /usr/local/bin/

ENV GRPC_PORT=50056

EXPOSE 50056

CMD ["service-f"]
