cmake_minimum_required(VERSION 3.16)
project(service-e CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find packages
find_package(Protobuf REQUIRED)
find_package(gRPC CONFIG REQUIRED)
find_package(opentelemetry-cpp CONFIG REQUIRED)

# Proto files
set(PROTO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../proto")
set(PROTO_FILES
    "${PROTO_DIR}/common.proto"
    "${PROTO_DIR}/services.proto"
)

# Generate protobuf and gRPC files
set(PROTO_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY ${PROTO_BINARY_DIR})

set(PROTO_SRCS
    "${PROTO_BINARY_DIR}/common.pb.cc"
    "${PROTO_BINARY_DIR}/services.pb.cc"
)
set(PROTO_HDRS
    "${PROTO_BINARY_DIR}/common.pb.h"
    "${PROTO_BINARY_DIR}/services.pb.h"
)
set(GRPC_SRCS
    "${PROTO_BINARY_DIR}/services.grpc.pb.cc"
)
set(GRPC_HDRS
    "${PROTO_BINARY_DIR}/services.grpc.pb.h"
)

# Get protoc and grpc_cpp_plugin
find_program(_PROTOBUF_PROTOC protoc)
find_program(_GRPC_CPP_PLUGIN_EXECUTABLE grpc_cpp_plugin)

# Generate proto files
add_custom_command(
    OUTPUT ${PROTO_SRCS} ${PROTO_HDRS} ${GRPC_SRCS} ${GRPC_HDRS}
    COMMAND ${_PROTOBUF_PROTOC}
    ARGS --proto_path=${PROTO_DIR}
         --cpp_out=${PROTO_BINARY_DIR}
         --grpc_out=${PROTO_BINARY_DIR}
         --plugin=protoc-gen-grpc=${_GRPC_CPP_PLUGIN_EXECUTABLE}
         ${PROTO_FILES}
    DEPENDS ${PROTO_FILES}
    COMMENT "Generating protobuf and gRPC files"
)

# Add executable
add_executable(service-e
    main.cpp
    ${PROTO_SRCS}
    ${GRPC_SRCS}
)

target_include_directories(service-e PRIVATE ${PROTO_BINARY_DIR})

target_link_libraries(service-e PRIVATE
    gRPC::grpc++
    protobuf::libprotobuf
    opentelemetry-cpp::trace
    opentelemetry-cpp::metrics
    opentelemetry-cpp::logs
    opentelemetry-cpp::otlp_grpc_exporter
    opentelemetry-cpp::otlp_grpc_metrics_exporter
    opentelemetry-cpp::otlp_grpc_log_record_exporter
    opentelemetry-cpp::ostream_span_exporter
)
