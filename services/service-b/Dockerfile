FROM rust:1.82-bookworm AS builder

# Install protobuf compiler
RUN apt-get update && apt-get install -y protobuf-compiler libprotobuf-dev && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy proto files
COPY proto/ ./proto/

# Copy Cargo files first for dependency caching
COPY services/service-b/Cargo.toml ./services/service-b/
COPY services/service-b/build.rs ./services/service-b/

# Create dummy main to build dependencies
RUN mkdir -p services/service-b/src && \
    echo 'fn main() {}' > services/service-b/src/main.rs

WORKDIR /app/services/service-b
RUN cargo build --release && rm -rf src

# Copy actual source and rebuild
COPY services/service-b/src ./src
RUN touch src/main.rs && cargo build --release

# Runtime image
FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y ca-certificates && rm -rf /var/lib/apt/lists/*

COPY --from=builder /app/services/service-b/target/release/service-b /usr/local/bin/

ENV GRPC_PORT=50052
ENV SERVICE_D_ADDR=service-d:50054
ENV SERVICE_E_ADDR=service-e:50055
ENV OTEL_EXPORTER_OTLP_ENDPOINT=http://otel-collector:4317

EXPOSE 50052

CMD ["service-b"]
