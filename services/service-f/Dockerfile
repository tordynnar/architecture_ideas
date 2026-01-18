FROM ubuntu:22.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

# Install build dependencies and pre-built gRPC
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    pkg-config \
    libssl-dev \
    libgrpc++-dev \
    libprotobuf-dev \
    protobuf-compiler-grpc \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy proto files
COPY proto/ ./proto/

# Generate protobuf files
RUN mkdir -p generated && \
    protoc --proto_path=./proto \
           --cpp_out=./generated \
           --grpc_out=./generated \
           --plugin=protoc-gen-grpc=/usr/bin/grpc_cpp_plugin \
           ./proto/common.proto ./proto/services.proto

# Copy source
COPY services/service-f/main_simple.cpp ./main.cpp

# Build
RUN g++ -std=c++17 -O2 \
    -I./generated \
    main.cpp \
    generated/common.pb.cc \
    generated/services.pb.cc \
    generated/services.grpc.pb.cc \
    -o service-f \
    -lgrpc++ -lgrpc -lprotobuf -lpthread -lssl -lcrypto

# Runtime image
FROM ubuntu:22.04

RUN apt-get update && apt-get install -y \
    libgrpc++1 \
    libprotobuf23 \
    libssl3 \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /app/service-f /usr/local/bin/

ENV GRPC_PORT=50056

EXPOSE 50056

CMD ["service-f"]
