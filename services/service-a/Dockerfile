FROM golang:1.21-bookworm AS builder

# Install protobuf compiler
RUN apt-get update && apt-get install -y protobuf-compiler && rm -rf /var/lib/apt/lists/*

# Install protoc-gen-go and protoc-gen-go-grpc
RUN go install google.golang.org/protobuf/cmd/protoc-gen-go@v1.32.0 && \
    go install google.golang.org/grpc/cmd/protoc-gen-go-grpc@v1.3.0

WORKDIR /app

# Copy Go module files first
COPY services/service-a/go.mod ./services/service-a/
WORKDIR /app/services/service-a

# Create proto directory and download dependencies
RUN mkdir -p proto && go mod download || true

# Copy proto files and generate Go code
WORKDIR /app
COPY proto/ ./proto/
RUN protoc --proto_path=./proto \
           --go_out=./services/service-a/proto --go_opt=paths=source_relative \
           --go-grpc_out=./services/service-a/proto --go-grpc_opt=paths=source_relative \
           ./proto/common.proto ./proto/services.proto

# Copy source and build
WORKDIR /app/services/service-a
COPY services/service-a/*.go ./
RUN go mod tidy && CGO_ENABLED=0 GOOS=linux go build -o service-a .

# Runtime image
FROM alpine:3.19

RUN apk --no-cache add ca-certificates

COPY --from=builder /app/services/service-a/service-a /usr/local/bin/

ENV GRPC_PORT=50051
ENV SERVICE_B_ADDR=service-b:50052
ENV SERVICE_C_ADDR=service-c:50053
ENV OTEL_EXPORTER_OTLP_ENDPOINT=otel-collector:4317

EXPOSE 50051

CMD ["service-a"]
