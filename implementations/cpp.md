# C++ Implementation

C++ has OpenTelemetry support via the `opentelemetry-cpp` SDK, which provides APIs for tracing, metrics, and logging with OTLP export capabilities.

## Key Libraries

- `opentelemetry-cpp` - Core OpenTelemetry SDK (tracing, metrics, logs)
- `opentelemetry-cpp` OTLP exporter - Export via OTLP/gRPC or OTLP/HTTP
- `grpc++` - gRPC C++ implementation
- `spdlog` - Fast C++ logging library (optional, for structured logging)

## Code Sample

```cpp
#include <iostream>
#include <memory>
#include <string>
#include <chrono>

#include <grpcpp/grpcpp.h>
#include <grpcpp/server_context.h>

// OpenTelemetry headers
#include <opentelemetry/exporters/otlp/otlp_grpc_exporter_factory.h>
#include <opentelemetry/exporters/otlp/otlp_grpc_metric_exporter_factory.h>
#include <opentelemetry/exporters/otlp/otlp_grpc_log_record_exporter_factory.h>
#include <opentelemetry/sdk/trace/tracer_provider_factory.h>
#include <opentelemetry/sdk/trace/batch_span_processor_factory.h>
#include <opentelemetry/sdk/metrics/meter_provider_factory.h>
#include <opentelemetry/sdk/metrics/export/periodic_exporting_metric_reader_factory.h>
#include <opentelemetry/sdk/logs/logger_provider_factory.h>
#include <opentelemetry/sdk/logs/batch_log_record_processor_factory.h>
#include <opentelemetry/sdk/resource/resource.h>
#include <opentelemetry/trace/provider.h>
#include <opentelemetry/metrics/provider.h>
#include <opentelemetry/logs/provider.h>
#include <opentelemetry/context/propagation/global_propagator.h>
#include <opentelemetry/trace/propagation/http_trace_context.h>

#include "myservice.grpc.pb.h"

namespace trace_api = opentelemetry::trace;
namespace metrics_api = opentelemetry::metrics;
namespace logs_api = opentelemetry::logs;
namespace resource = opentelemetry::sdk::resource;
namespace otlp = opentelemetry::exporter::otlp;

// ============================================================
// GLOBAL TELEMETRY OBJECTS
// ============================================================
static opentelemetry::nostd::shared_ptr<trace_api::Tracer> g_tracer;
static opentelemetry::nostd::shared_ptr<metrics_api::Meter> g_meter;
static opentelemetry::nostd::shared_ptr<logs_api::Logger> g_logger;

// Custom metrics instruments
static std::unique_ptr<metrics_api::Counter<uint64_t>> g_request_counter;
static std::unique_ptr<metrics_api::Histogram<double>> g_request_duration;
static std::unique_ptr<metrics_api::UpDownCounter<int64_t>> g_active_requests;

// ============================================================
// TELEMETRY INITIALIZATION
// ============================================================
void InitTelemetry() {
    // Create resource with service information
    auto resource_attributes = resource::ResourceAttributes{
        {"service.name", "my-cpp-service"},
        {"service.version", "1.0.0"},
    };
    auto resource = resource::Resource::Create(resource_attributes);

    // ============================================================
    // TRACING SETUP
    // ============================================================
    otlp::OtlpGrpcExporterOptions trace_opts;
    trace_opts.endpoint = "otel-collector:4317";
    trace_opts.use_ssl_credentials = false;

    auto trace_exporter = otlp::OtlpGrpcExporterFactory::Create(trace_opts);

    opentelemetry::sdk::trace::BatchSpanProcessorOptions processor_opts;
    processor_opts.max_queue_size = 2048;
    processor_opts.max_export_batch_size = 512;

    auto trace_processor = opentelemetry::sdk::trace::BatchSpanProcessorFactory::Create(
        std::move(trace_exporter), processor_opts);

    auto trace_provider = opentelemetry::sdk::trace::TracerProviderFactory::Create(
        std::move(trace_processor), resource);

    trace_api::Provider::SetTracerProvider(std::move(trace_provider));

    // Set up W3C Trace Context propagator
    opentelemetry::context::propagation::GlobalTextMapPropagator::SetGlobalPropagator(
        opentelemetry::nostd::shared_ptr<opentelemetry::context::propagation::TextMapPropagator>(
            new opentelemetry::trace::propagation::HttpTraceContext()));

    g_tracer = trace_api::Provider::GetTracerProvider()->GetTracer("my-cpp-service", "1.0.0");

    // ============================================================
    // METRICS SETUP
    // ============================================================
    otlp::OtlpGrpcMetricExporterOptions metric_opts;
    metric_opts.endpoint = "otel-collector:4317";
    metric_opts.use_ssl_credentials = false;

    auto metric_exporter = otlp::OtlpGrpcMetricExporterFactory::Create(metric_opts);

    opentelemetry::sdk::metrics::PeriodicExportingMetricReaderOptions reader_opts;
    reader_opts.export_interval_millis = std::chrono::milliseconds(10000);
    reader_opts.export_timeout_millis = std::chrono::milliseconds(5000);

    auto metric_reader = opentelemetry::sdk::metrics::PeriodicExportingMetricReaderFactory::Create(
        std::move(metric_exporter), reader_opts);

    auto meter_provider = opentelemetry::sdk::metrics::MeterProviderFactory::Create();
    auto* meter_provider_ptr = static_cast<opentelemetry::sdk::metrics::MeterProvider*>(
        meter_provider.get());
    meter_provider_ptr->AddMetricReader(std::move(metric_reader));

    metrics_api::Provider::SetMeterProvider(std::move(meter_provider));
    g_meter = metrics_api::Provider::GetMeterProvider()->GetMeter("my-cpp-service", "1.0.0");

    // Create custom metrics instruments
    g_request_counter = g_meter->CreateUInt64Counter(
        "myservice.requests.total",
        "Total number of requests processed",
        "{requests}");

    g_request_duration = g_meter->CreateDoubleHistogram(
        "myservice.request.duration",
        "Request processing duration",
        "s");

    g_active_requests = g_meter->CreateInt64UpDownCounter(
        "myservice.requests.active",
        "Number of requests currently being processed",
        "{requests}");

    // ============================================================
    // LOGGING SETUP
    // ============================================================
    otlp::OtlpGrpcLogRecordExporterOptions log_opts;
    log_opts.endpoint = "otel-collector:4317";
    log_opts.use_ssl_credentials = false;

    auto log_exporter = otlp::OtlpGrpcLogRecordExporterFactory::Create(log_opts);

    opentelemetry::sdk::logs::BatchLogRecordProcessorOptions log_processor_opts;
    auto log_processor = opentelemetry::sdk::logs::BatchLogRecordProcessorFactory::Create(
        std::move(log_exporter), log_processor_opts);

    auto logger_provider = opentelemetry::sdk::logs::LoggerProviderFactory::Create(
        std::move(log_processor), resource);

    logs_api::Provider::SetLoggerProvider(std::move(logger_provider));
    g_logger = logs_api::Provider::GetLoggerProvider()->GetLogger("my-cpp-service", "1.0.0");
}

void ShutdownTelemetry() {
    auto trace_provider = trace_api::Provider::GetTracerProvider();
    if (auto* sdk_provider = dynamic_cast<opentelemetry::sdk::trace::TracerProvider*>(
            trace_provider.get())) {
        sdk_provider->Shutdown();
    }

    auto meter_provider = metrics_api::Provider::GetMeterProvider();
    if (auto* sdk_provider = dynamic_cast<opentelemetry::sdk::metrics::MeterProvider*>(
            meter_provider.get())) {
        sdk_provider->Shutdown();
    }

    auto logger_provider = logs_api::Provider::GetLoggerProvider();
    if (auto* sdk_provider = dynamic_cast<opentelemetry::sdk::logs::LoggerProvider*>(
            logger_provider.get())) {
        sdk_provider->Shutdown();
    }
}

// ============================================================
// CONTEXTUAL LOGGING HELPER
// ============================================================
void LogWithContext(logs_api::Severity severity, const std::string& message,
                    trace_api::SpanContext* span_ctx = nullptr) {
    auto log_record = g_logger->CreateLogRecord();
    log_record->SetSeverity(severity);
    log_record->SetBody(message);
    log_record->SetTimestamp(std::chrono::system_clock::now());

    if (span_ctx && span_ctx->IsValid()) {
        log_record->SetTraceId(span_ctx->trace_id());
        log_record->SetSpanId(span_ctx->span_id());
        log_record->SetTraceFlags(span_ctx->trace_flags());
    }

    g_logger->EmitLogRecord(std::move(log_record));

    // Also print to stdout for local debugging
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    char timestamp[32];
    std::strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&time_t_now));

    std::string trace_id = span_ctx && span_ctx->IsValid()
        ? std::string(reinterpret_cast<const char*>(span_ctx->trace_id().Id().data()), 16)
        : "0";
    std::string span_id = span_ctx && span_ctx->IsValid()
        ? std::string(reinterpret_cast<const char*>(span_ctx->span_id().Id().data()), 8)
        : "0";

    std::cout << "{\"timestamp\":\"" << timestamp
              << "\",\"level\":\"" << static_cast<int>(severity)
              << "\",\"message\":\"" << message
              << "\",\"service\":\"my-cpp-service\"}" << std::endl;
}

// ============================================================
// GRPC METADATA CARRIER FOR CONTEXT PROPAGATION
// ============================================================
class GrpcServerCarrier : public opentelemetry::context::propagation::TextMapCarrier {
public:
    explicit GrpcServerCarrier(const grpc::ServerContext* context) : context_(context) {}

    opentelemetry::nostd::string_view Get(
        opentelemetry::nostd::string_view key) const noexcept override {
        auto metadata = context_->client_metadata();
        auto it = metadata.find(std::string(key));
        if (it != metadata.end()) {
            return opentelemetry::nostd::string_view(it->second.data(), it->second.size());
        }
        return "";
    }

    void Set(opentelemetry::nostd::string_view key,
             opentelemetry::nostd::string_view value) noexcept override {
        // Server carrier is read-only
    }

private:
    const grpc::ServerContext* context_;
};

class GrpcClientCarrier : public opentelemetry::context::propagation::TextMapCarrier {
public:
    explicit GrpcClientCarrier(grpc::ClientContext* context) : context_(context) {}

    opentelemetry::nostd::string_view Get(
        opentelemetry::nostd::string_view key) const noexcept override {
        return "";  // Client carrier is write-only
    }

    void Set(opentelemetry::nostd::string_view key,
             opentelemetry::nostd::string_view value) noexcept override {
        context_->AddMetadata(std::string(key), std::string(value));
    }

private:
    grpc::ClientContext* context_;
};

// ============================================================
// SERVICE IMPLEMENTATION
// ============================================================
class MyServiceImpl final : public myservice::MyService::Service {
public:
    grpc::Status ProcessRequest(grpc::ServerContext* context,
                                const myservice::MyRequest* request,
                                myservice::MyResponse* response) override {
        auto start_time = std::chrono::steady_clock::now();

        // Track active requests (metric)
        g_active_requests->Add(1, {{"method", "ProcessRequest"}});

        // Extract trace context from incoming metadata
        GrpcServerCarrier carrier(context);
        auto propagator = opentelemetry::context::propagation::GlobalTextMapPropagator::
            GetGlobalPropagator();
        auto current_ctx = opentelemetry::context::RuntimeContext::GetCurrent();
        auto new_ctx = propagator->Extract(carrier, current_ctx);

        // Start server span with extracted context
        opentelemetry::trace::StartSpanOptions span_opts;
        span_opts.kind = opentelemetry::trace::SpanKind::kServer;
        span_opts.parent = opentelemetry::trace::GetSpan(new_ctx)->GetContext();

        auto span = g_tracer->StartSpan("grpc.server.ProcessRequest", {
            {"rpc.system", "grpc"},
            {"rpc.service", "MyService"},
            {"rpc.method", "ProcessRequest"},
            {"request.id", request->id()},
        }, span_opts);

        auto scope = g_tracer->WithActiveSpan(span);
        auto span_ctx = span->GetContext();

        bool is_error = false;

        try {
            LogWithContext(logs_api::Severity::kInfo,
                          "Processing request: " + request->id(), &span_ctx);

            // Manual span for business logic
            auto biz_span = g_tracer->StartSpan("process-business-logic", {
                {"request.id", request->id()},
            });
            auto biz_scope = g_tracer->WithActiveSpan(biz_span);

            LogWithContext(logs_api::Severity::kInfo, "Executing business logic", &span_ctx);

            // Nested span for database query
            auto db_span = g_tracer->StartSpan("database-query", {
                {"db.system", "postgresql"},
                {"db.operation", "SELECT"},
            });

            LogWithContext(logs_api::Severity::kInfo, "Executing database query", &span_ctx);

            // Simulate database operation
            std::this_thread::sleep_for(std::chrono::milliseconds(10));

            db_span->SetStatus(opentelemetry::trace::StatusCode::kOk);
            db_span->End();

            biz_span->SetStatus(opentelemetry::trace::StatusCode::kOk);
            biz_span->End();

            response->set_result("success");
            span->SetStatus(opentelemetry::trace::StatusCode::kOk);

            LogWithContext(logs_api::Severity::kInfo, "Request processed successfully", &span_ctx);

        } catch (const std::exception& e) {
            is_error = true;
            span->SetStatus(opentelemetry::trace::StatusCode::kError, e.what());
            span->AddEvent("exception", {
                {"exception.type", "std::exception"},
                {"exception.message", e.what()},
            });

            LogWithContext(logs_api::Severity::kError,
                          std::string("Business logic failed: ") + e.what(), &span_ctx);

            span->End();
            g_active_requests->Add(-1, {{"method", "ProcessRequest"}});

            return grpc::Status(grpc::StatusCode::INTERNAL, e.what());
        }

        span->End();

        // Record metrics
        auto end_time = std::chrono::steady_clock::now();
        double duration = std::chrono::duration<double>(end_time - start_time).count();

        g_request_counter->Add(1, {
            {"method", "ProcessRequest"},
            {"error", is_error ? "true" : "false"},
        });
        g_request_duration->Record(duration, {
            {"method", "ProcessRequest"},
            {"error", is_error ? "true" : "false"},
        });
        g_active_requests->Add(-1, {{"method", "ProcessRequest"}});

        return grpc::Status::OK;
    }
};

// ============================================================
// GRPC CLIENT WITH TRACING
// ============================================================
class MyServiceClient {
public:
    explicit MyServiceClient(std::shared_ptr<grpc::Channel> channel)
        : stub_(myservice::MyService::NewStub(channel)) {}

    std::string CallService(const std::string& request_id) {
        // Start client span
        opentelemetry::trace::StartSpanOptions span_opts;
        span_opts.kind = opentelemetry::trace::SpanKind::kClient;

        auto span = g_tracer->StartSpan("grpc.client.ProcessRequest", {
            {"rpc.system", "grpc"},
            {"rpc.service", "MyService"},
            {"rpc.method", "ProcessRequest"},
            {"request.id", request_id},
        }, span_opts);

        auto scope = g_tracer->WithActiveSpan(span);
        auto span_ctx = span->GetContext();

        LogWithContext(logs_api::Severity::kInfo,
                      "Calling service with request: " + request_id, &span_ctx);

        grpc::ClientContext context;

        // Inject trace context into outgoing metadata
        GrpcClientCarrier carrier(&context);
        auto propagator = opentelemetry::context::propagation::GlobalTextMapPropagator::
            GetGlobalPropagator();
        auto current_ctx = opentelemetry::context::RuntimeContext::GetCurrent();
        propagator->Inject(carrier, current_ctx);

        myservice::MyRequest request;
        request.set_id(request_id);

        myservice::MyResponse response;
        grpc::Status status = stub_->ProcessRequest(&context, request, &response);

        if (status.ok()) {
            span->SetStatus(opentelemetry::trace::StatusCode::kOk);
            LogWithContext(logs_api::Severity::kInfo, "Service call succeeded", &span_ctx);
        } else {
            span->SetStatus(opentelemetry::trace::StatusCode::kError, status.error_message());
            span->AddEvent("grpc_error", {
                {"grpc.status_code", static_cast<int>(status.error_code())},
                {"grpc.error_message", status.error_message()},
            });
            LogWithContext(logs_api::Severity::kError,
                          "Service call failed: " + status.error_message(), &span_ctx);
        }

        span->End();
        return response.result();
    }

private:
    std::unique_ptr<myservice::MyService::Stub> stub_;
};

// ============================================================
// MAIN
// ============================================================
int main(int argc, char** argv) {
    InitTelemetry();

    std::string server_address("0.0.0.0:50051");
    MyServiceImpl service;

    grpc::ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);

    std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
    std::cout << "Server listening on " << server_address << std::endl;

    server->Wait();

    ShutdownTelemetry();
    return 0;
}
```

## How It Works

### Tracing

**OpenTelemetry C++ SDK:**
The `opentelemetry-cpp` library provides the standard OpenTelemetry API for C++. Key components:
- `TracerProvider`: Factory for creating tracers
- `Tracer`: Creates spans
- `Span`: Represents a unit of work with timing and attributes

**Span Creation:**
```cpp
auto span = g_tracer->StartSpan("operation-name", {
    {"attribute.key", "value"},
});
auto scope = g_tracer->WithActiveSpan(span);
// ... operation code ...
span->End();
```

The `WithActiveSpan` scope guard makes the span the current active span, enabling child spans to automatically parent to it.

**Span Options:**
Use `StartSpanOptions` to configure span behavior:
```cpp
opentelemetry::trace::StartSpanOptions opts;
opts.kind = opentelemetry::trace::SpanKind::kServer;  // or kClient, kInternal
opts.parent = parent_span_context;  // For distributed tracing
```

**Context Propagation:**
The `TextMapCarrier` interface enables injecting/extracting trace context from gRPC metadata:
- `GrpcServerCarrier`: Extracts `traceparent` header from incoming requests
- `GrpcClientCarrier`: Injects `traceparent` header into outgoing requests

This enables distributed tracing across service boundaries using W3C Trace Context.

**Exception Recording:**
Record exceptions as span events:
```cpp
span->AddEvent("exception", {
    {"exception.type", "std::exception"},
    {"exception.message", e.what()},
});
span->SetStatus(opentelemetry::trace::StatusCode::kError, e.what());
```

### Metrics

**Metric Instruments:**
The OpenTelemetry C++ SDK provides several instrument types:
- **Counter** (`CreateUInt64Counter`): Monotonically increasing values
- **Histogram** (`CreateDoubleHistogram`): Distribution of values with automatic bucketing
- **UpDownCounter** (`CreateInt64UpDownCounter`): Values that can increase or decrease

**Recording Metrics:**
```cpp
g_request_counter->Add(1, {
    {"method", "ProcessRequest"},
    {"error", "false"},
});

g_request_duration->Record(duration_seconds, {
    {"method", "ProcessRequest"},
});
```

**Metric Attributes:**
Attributes are passed as initializer lists of key-value pairs. These become labels in Prometheus:
```promql
myservice_requests_total{method="ProcessRequest", error="false"}
```

**Periodic Export:**
The `PeriodicExportingMetricReader` collects and exports metrics at configurable intervals:
```cpp
reader_opts.export_interval_millis = std::chrono::milliseconds(10000);  // 10 seconds
```

**Thread Safety:**
Metric instruments in OpenTelemetry C++ are thread-safe. Multiple threads can call `Add()` or `Record()` concurrently without external synchronization.

### Logging

**OpenTelemetry Logs:**
The OpenTelemetry C++ SDK includes a logging API that exports logs via OTLP:
```cpp
auto log_record = g_logger->CreateLogRecord();
log_record->SetSeverity(logs_api::Severity::kInfo);
log_record->SetBody("Log message");
log_record->SetTimestamp(std::chrono::system_clock::now());
g_logger->EmitLogRecord(std::move(log_record));
```

**Trace Correlation:**
Each log record can include trace context for correlation:
```cpp
log_record->SetTraceId(span_ctx->trace_id());
log_record->SetSpanId(span_ctx->span_id());
log_record->SetTraceFlags(span_ctx->trace_flags());
```

This enables:
- Clicking a trace ID in Grafana to see all related logs
- Filtering logs by span ID to see logs from a specific operation
- Understanding the context of errors in distributed systems

**Severity Levels:**
```cpp
logs_api::Severity::kTrace
logs_api::Severity::kDebug
logs_api::Severity::kInfo
logs_api::Severity::kWarn
logs_api::Severity::kError
logs_api::Severity::kFatal
```

**Dual Output:**
The example writes logs to both:
1. OpenTelemetry Collector via OTLP (for centralized aggregation)
2. stdout as JSON (for local debugging and `docker logs`)

### Build Configuration

**CMakeLists.txt Example:**
```cmake
cmake_minimum_required(VERSION 3.12)
project(my-cpp-service)

set(CMAKE_CXX_STANDARD 17)

find_package(opentelemetry-cpp CONFIG REQUIRED)
find_package(gRPC CONFIG REQUIRED)
find_package(Protobuf CONFIG REQUIRED)

add_executable(server main.cpp myservice.grpc.pb.cc myservice.pb.cc)

target_link_libraries(server
    opentelemetry-cpp::trace
    opentelemetry-cpp::metrics
    opentelemetry-cpp::logs
    opentelemetry-cpp::otlp_grpc_exporter
    opentelemetry-cpp::otlp_grpc_metrics_exporter
    opentelemetry-cpp::otlp_grpc_log_record_exporter
    gRPC::grpc++
    protobuf::libprotobuf
)
```

**vcpkg Installation:**
```bash
vcpkg install opentelemetry-cpp[otlp-grpc] grpc
```

### Production Considerations

**Resource Management:**
Use RAII patterns for spans:
```cpp
class ScopedSpan {
public:
    ScopedSpan(const std::string& name)
        : span_(g_tracer->StartSpan(name))
        , scope_(g_tracer->WithActiveSpan(span_)) {}

    ~ScopedSpan() { span_->End(); }

    trace_api::Span* operator->() { return span_.get(); }

private:
    opentelemetry::nostd::shared_ptr<trace_api::Span> span_;
    trace_api::Scope scope_;
};

// Usage:
void DoWork() {
    ScopedSpan span("do-work");
    span->SetAttribute("key", "value");
    // span automatically ends when scope exits
}
```

**Error Handling:**
Always set span status on errors and record exceptions:
```cpp
try {
    // operation
    span->SetStatus(trace_api::StatusCode::kOk);
} catch (const std::exception& e) {
    span->SetStatus(trace_api::StatusCode::kError, e.what());
    span->RecordException(e);
    throw;
}
```

**Graceful Shutdown:**
Call `Shutdown()` on all providers to flush pending telemetry:
```cpp
void ShutdownTelemetry() {
    // Flush and shutdown all providers
    // This ensures all buffered data is exported
}
```

**Memory Management:**
OpenTelemetry C++ uses `nostd::shared_ptr` for reference counting. Be careful with:
- Storing spans beyond their intended scope
- Circular references between spans
- Large numbers of concurrent spans (consider sampling)

## Quick Reference: gRPC Server

Minimal gRPC server method implementation with metrics, tracing, and logging:

```cpp
grpc::Status ProcessRequest(grpc::ServerContext* context,
                            const myservice::MyRequest* request,
                            myservice::MyResponse* response) override {
    auto start_time = std::chrono::steady_clock::now();

    // Metrics: track active requests
    g_active_requests->Add(1, {{"method", "ProcessRequest"}});

    // Tracing: extract context and create server span
    GrpcServerCarrier carrier(context);
    auto propagator = opentelemetry::context::propagation::GlobalTextMapPropagator::GetGlobalPropagator();
    auto parent_ctx = propagator->Extract(carrier, opentelemetry::context::RuntimeContext::GetCurrent());

    opentelemetry::trace::StartSpanOptions span_opts;
    span_opts.kind = opentelemetry::trace::SpanKind::kServer;
    span_opts.parent = opentelemetry::trace::GetSpan(parent_ctx)->GetContext();

    auto span = g_tracer->StartSpan("grpc.server.ProcessRequest", {
        {"rpc.system", "grpc"},
        {"rpc.method", "ProcessRequest"},
        {"request.id", request->id()},
    }, span_opts);
    auto scope = g_tracer->WithActiveSpan(span);
    auto span_ctx = span->GetContext();

    bool is_error = false;

    // Logging: with trace context
    LogWithContext(logs_api::Severity::kInfo, "Processing request: " + request->id(), &span_ctx);

    // Tracing: manual span for business logic
    auto biz_span = g_tracer->StartSpan("process-business-logic");
    LogWithContext(logs_api::Severity::kInfo, "Executing business logic", &span_ctx);

    // Execute business logic
    try {
        auto result = DoBusinessLogic(request);
        response->set_result(result);
        biz_span->SetStatus(opentelemetry::trace::StatusCode::kOk);
    } catch (const std::exception& e) {
        is_error = true;
        biz_span->SetStatus(opentelemetry::trace::StatusCode::kError, e.what());
        span->AddEvent("exception", {{"exception.message", e.what()}});
        LogWithContext(logs_api::Severity::kError, std::string("Business logic failed: ") + e.what(), &span_ctx);
    }
    biz_span->End();

    // Metrics: record request outcome
    auto duration = std::chrono::duration<double>(std::chrono::steady_clock::now() - start_time).count();
    g_request_counter->Add(1, {{"method", "ProcessRequest"}, {"error", is_error ? "true" : "false"}});
    g_request_duration->Record(duration, {{"method", "ProcessRequest"}, {"error", is_error ? "true" : "false"}});
    g_active_requests->Add(-1, {{"method", "ProcessRequest"}});

    span->SetStatus(is_error ? opentelemetry::trace::StatusCode::kError : opentelemetry::trace::StatusCode::kOk);
    span->End();

    LogWithContext(logs_api::Severity::kInfo, "Request completed", &span_ctx);
    return is_error ? grpc::Status(grpc::StatusCode::INTERNAL, "Error") : grpc::Status::OK;
}
```

## Quick Reference: gRPC Client Call

Minimal gRPC client call with metrics, tracing, and logging:

```cpp
std::string CallService(const std::string& request_id) {
    auto start_time = std::chrono::steady_clock::now();

    // Tracing: create client span
    opentelemetry::trace::StartSpanOptions span_opts;
    span_opts.kind = opentelemetry::trace::SpanKind::kClient;

    auto span = g_tracer->StartSpan("grpc.client.ProcessRequest", {
        {"rpc.system", "grpc"},
        {"rpc.service", "MyService"},
        {"request.id", request_id},
    }, span_opts);
    auto scope = g_tracer->WithActiveSpan(span);
    auto span_ctx = span->GetContext();

    // Logging: with trace context
    LogWithContext(logs_api::Severity::kInfo, "Calling service: " + request_id, &span_ctx);

    grpc::ClientContext context;

    // Tracing: inject context for propagation
    GrpcClientCarrier carrier(&context);
    auto propagator = opentelemetry::context::propagation::GlobalTextMapPropagator::GetGlobalPropagator();
    propagator->Inject(carrier, opentelemetry::context::RuntimeContext::GetCurrent());

    // Make gRPC call
    myservice::MyRequest request;
    request.set_id(request_id);
    myservice::MyResponse response;
    grpc::Status status = stub_->ProcessRequest(&context, request, &response);

    // Metrics: record call outcome
    auto duration = std::chrono::duration<double>(std::chrono::steady_clock::now() - start_time).count();
    bool is_error = !status.ok();
    g_client_call_counter->Add(1, {{"service", "other-service"}, {"error", is_error ? "true" : "false"}});
    g_client_call_duration->Record(duration, {{"service", "other-service"}, {"error", is_error ? "true" : "false"}});

    // Tracing and logging: completion
    if (status.ok()) {
        span->SetStatus(opentelemetry::trace::StatusCode::kOk);
        LogWithContext(logs_api::Severity::kInfo, "Service call completed", &span_ctx);
    } else {
        span->SetStatus(opentelemetry::trace::StatusCode::kError, status.error_message());
        span->AddEvent("grpc_error", {{"grpc.status_code", static_cast<int>(status.error_code())}});
        LogWithContext(logs_api::Severity::kError, "Service call failed: " + status.error_message(), &span_ctx);
    }

    span->End();
    return response.result();
}
