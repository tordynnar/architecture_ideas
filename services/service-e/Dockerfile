# Service E - C++ gRPC with OpenTelemetry
# Multi-stage build with incremental build support via cache mounts

# =============================================================================
# Stage 1: Build dependencies (cached layer - rarely changes)
# =============================================================================
FROM ubuntu:22.04 AS deps-builder

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
    libcurl4-openssl-dev \
    ccache \
    ninja-build \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /deps

# Build abseil-cpp (required by OTel and gRPC)
ARG ABSEIL_VERSION=20230802.1
RUN --mount=type=cache,target=/root/.cache/ccache \
    git clone --depth 1 --branch ${ABSEIL_VERSION} https://github.com/abseil/abseil-cpp.git && \
    cd abseil-cpp && \
    cmake -B build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_CXX_STANDARD=17 \
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
        -DBUILD_SHARED_LIBS=OFF \
        -DABSL_PROPAGATE_CXX_STD=ON \
        -DCMAKE_CXX_COMPILER_LAUNCHER=ccache && \
    cmake --build build -j$(nproc) && \
    cmake --install build && \
    cd .. && rm -rf abseil-cpp

# Build protobuf
ARG PROTOBUF_VERSION=25.1
RUN --mount=type=cache,target=/root/.cache/ccache \
    git clone --depth 1 --branch v${PROTOBUF_VERSION} https://github.com/protocolbuffers/protobuf.git && \
    cd protobuf && \
    git submodule update --init --recursive && \
    cmake -B build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -Dprotobuf_BUILD_TESTS=OFF \
        -DBUILD_SHARED_LIBS=OFF \
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
        -Dprotobuf_ABSL_PROVIDER=package \
        -DCMAKE_CXX_COMPILER_LAUNCHER=ccache && \
    cmake --build build -j$(nproc) && \
    cmake --install build && \
    cd .. && rm -rf protobuf

# Build gRPC
ARG GRPC_VERSION=1.60.0
RUN --mount=type=cache,target=/root/.cache/ccache \
    git clone --depth 1 --branch v${GRPC_VERSION} https://github.com/grpc/grpc.git && \
    cd grpc && \
    git submodule update --init --recursive && \
    cmake -B build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DgRPC_INSTALL=ON \
        -DgRPC_BUILD_TESTS=OFF \
        -DgRPC_PROTOBUF_PROVIDER=package \
        -DgRPC_SSL_PROVIDER=package \
        -DgRPC_ZLIB_PROVIDER=package \
        -DgRPC_ABSL_PROVIDER=package \
        -DBUILD_SHARED_LIBS=OFF \
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
        -DCMAKE_CXX_COMPILER_LAUNCHER=ccache && \
    cmake --build build -j$(nproc) && \
    cmake --install build && \
    cd .. && rm -rf grpc

# Build OpenTelemetry C++ SDK
ARG OTEL_CPP_VERSION=1.13.0
RUN --mount=type=cache,target=/root/.cache/ccache \
    git clone --depth 1 --branch v${OTEL_CPP_VERSION} https://github.com/open-telemetry/opentelemetry-cpp.git && \
    cd opentelemetry-cpp && \
    cmake -B build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_CXX_STANDARD=17 \
        -DCMAKE_PREFIX_PATH=/usr/local \
        -DBUILD_TESTING=OFF \
        -DWITH_EXAMPLES=OFF \
        -DWITH_BENCHMARK=OFF \
        -DWITH_OTLP_GRPC=ON \
        -DWITH_OTLP_HTTP=OFF \
        -DWITH_ABSEIL=ON \
        -DBUILD_SHARED_LIBS=OFF \
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
        -DCMAKE_CXX_COMPILER_LAUNCHER=ccache && \
    cmake --build build -j$(nproc) && \
    cmake --install build && \
    cd .. && rm -rf opentelemetry-cpp

# =============================================================================
# Stage 2: Build application (changes frequently)
# =============================================================================
FROM deps-builder AS app-builder

WORKDIR /app

# Copy proto files first (changes less frequently)
COPY proto/ ./proto/

# Generate protobuf/gRPC files
RUN mkdir -p generated && \
    protoc --proto_path=./proto \
           --cpp_out=./generated \
           --grpc_out=./generated \
           --plugin=protoc-gen-grpc=$(which grpc_cpp_plugin) \
           ./proto/common.proto ./proto/services.proto

# Copy application source (changes most frequently - separate layer)
COPY services/service-e/main.cpp ./main.cpp

# Create CMakeLists.txt for the application
RUN cat > CMakeLists.txt << 'EOF'
cmake_minimum_required(VERSION 3.16)
project(service-e CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find packages
find_package(Protobuf REQUIRED CONFIG)
find_package(gRPC CONFIG REQUIRED)
find_package(opentelemetry-cpp CONFIG REQUIRED)

# Add executable
add_executable(service-e
    main.cpp
    generated/common.pb.cc
    generated/services.pb.cc
    generated/services.grpc.pb.cc
)

target_include_directories(service-e PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/generated)

target_link_libraries(service-e PRIVATE
    gRPC::grpc++
    protobuf::libprotobuf
    opentelemetry-cpp::trace
    opentelemetry-cpp::metrics
    opentelemetry-cpp::logs
    opentelemetry-cpp::otlp_grpc_exporter
    opentelemetry-cpp::otlp_grpc_metrics_exporter
    opentelemetry-cpp::otlp_grpc_log_record_exporter
)
EOF

# Build the application with cmake
RUN --mount=type=cache,target=/root/.cache/ccache \
    cmake -B build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_CXX_COMPILER_LAUNCHER=ccache && \
    cmake --build build -j$(nproc)

# Verify binary
RUN ldd build/service-e || true
RUN ls -la build/service-e && echo "Binary built successfully"

# =============================================================================
# Stage 3: Runtime image (minimal)
# =============================================================================
FROM ubuntu:22.04

RUN apt-get update && apt-get install -y \
    libssl3 \
    zlib1g \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

COPY --from=app-builder /app/build/service-e /usr/local/bin/

ENV GRPC_PORT=50055
ENV SERVICE_D_ADDR=service-d:50054
ENV OTEL_EXPORTER_OTLP_ENDPOINT=http://otel-collector:4317

EXPOSE 50055

CMD ["service-e"]
