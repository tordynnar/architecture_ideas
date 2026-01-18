#include <iostream>
#include <memory>
#include <string>
#include <random>
#include <thread>
#include <chrono>
#include <cstdlib>
#include <cstring>

#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>

#include "opentelemetry/exporters/otlp/otlp_grpc_exporter_factory.h"
#include "opentelemetry/exporters/otlp/otlp_grpc_metric_exporter_factory.h"
#include "opentelemetry/sdk/trace/processor.h"
#include "opentelemetry/sdk/trace/batch_span_processor_factory.h"
#include "opentelemetry/sdk/trace/tracer_provider_factory.h"
#include "opentelemetry/sdk/metrics/meter_provider_factory.h"
#include "opentelemetry/sdk/metrics/export/periodic_exporting_metric_reader_factory.h"
#include "opentelemetry/trace/provider.h"
#include "opentelemetry/metrics/provider.h"
#include "opentelemetry/sdk/resource/semantic_conventions.h"
#include "opentelemetry/sdk/resource/resource.h"
#include "opentelemetry/context/propagation/global_propagator.h"
#include "opentelemetry/trace/propagation/http_trace_context.h"

#include "services.grpc.pb.h"

namespace trace_api = opentelemetry::trace;
namespace metrics_api = opentelemetry::metrics;
namespace resource = opentelemetry::sdk::resource;
namespace otlp = opentelemetry::exporter::otlp;
namespace trace_sdk = opentelemetry::sdk::trace;
namespace metrics_sdk = opentelemetry::sdk::metrics;

// C-style data structures and functions
extern "C" {

typedef struct {
    char id[64];
    char raw_data[1024];
    int64_t created_at;
    int64_t updated_at;
} legacy_record_t;

typedef struct {
    int success;
    char message[256];
    legacy_record_t record;
} fetch_result_t;

// Simulated legacy database lookup
fetch_result_t fetch_legacy_record(const char* record_id, const char* table_name) {
    fetch_result_t result;
    memset(&result, 0, sizeof(result));

    // Simulate DB lookup delay (3-8ms)
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> delay_dist(3, 8);
    std::this_thread::sleep_for(std::chrono::milliseconds(delay_dist(gen)));

    // Simulate successful lookup
    result.success = 1;
    snprintf(result.message, sizeof(result.message), "Record fetched successfully from %s", table_name);

    strncpy(result.record.id, record_id, sizeof(result.record.id) - 1);
    snprintf(result.record.raw_data, sizeof(result.record.raw_data),
             "{\"source\": \"%s\", \"data\": \"legacy_value_%s\"}", table_name, record_id);

    auto now = std::chrono::system_clock::now();
    auto epoch = now.time_since_epoch();
    result.record.created_at = std::chrono::duration_cast<std::chrono::seconds>(epoch).count() - 86400;
    result.record.updated_at = std::chrono::duration_cast<std::chrono::seconds>(epoch).count();

    return result;
}

} // extern "C"

// gRPC Service Implementation
class ServiceFImpl final : public grpcarch::ServiceF::Service {
public:
    ServiceFImpl() : request_counter_(0) {
        auto provider = trace_api::Provider::GetTracerProvider();
        tracer_ = provider->GetTracer("service-f", "1.0.0");

        auto meter_provider = metrics_api::Provider::GetMeterProvider();
        auto meter = meter_provider->GetMeter("service-f", "1.0.0");
        request_counter_metric_ = meter->CreateUInt64Counter("service_f_requests_total");
        latency_histogram_ = meter->CreateDoubleHistogram("service_f_request_duration_ms");
    }

    grpc::Status FetchLegacyData(
        grpc::ServerContext* context,
        const grpcarch::LegacyDataRequest* request,
        grpcarch::LegacyDataResponse* response) override {

        auto start = std::chrono::high_resolution_clock::now();

        // Extract trace context from gRPC metadata
        auto span = tracer_->StartSpan("FetchLegacyData",
            {{trace_api::SemanticConventions::kRpcSystem, "grpc"},
             {trace_api::SemanticConventions::kRpcService, "ServiceF"},
             {trace_api::SemanticConventions::kRpcMethod, "FetchLegacyData"}});
        auto scope = tracer_->WithActiveSpan(span);

        span->SetAttribute("record_id", request->record_id());
        span->SetAttribute("table_name", request->table_name());

        std::cout << "[Service F] FetchLegacyData called - record_id: "
                  << request->record_id() << ", table: " << request->table_name() << std::endl;

        // Call C function for legacy data lookup
        fetch_result_t result = fetch_legacy_record(
            request->record_id().c_str(),
            request->table_name().c_str()
        );

        // Build response
        auto* status = response->mutable_status();
        status->set_success(result.success == 1);
        status->set_message(result.message);

        auto* record = response->mutable_record();
        record->set_id(result.record.id);
        record->set_raw_data(result.record.raw_data, strlen(result.record.raw_data));
        record->set_created_at(result.record.created_at);
        record->set_updated_at(result.record.updated_at);
        (*record->mutable_fields())["source"] = request->table_name();
        (*record->mutable_fields())["fetched_by"] = "service-f";

        auto end = std::chrono::high_resolution_clock::now();
        double duration_ms = std::chrono::duration<double, std::milli>(end - start).count();

        // Record metrics
        request_counter_metric_->Add(1, {{"method", "FetchLegacyData"}, {"status", "ok"}});
        latency_histogram_->Record(duration_ms, {{"method", "FetchLegacyData"}});

        span->SetAttribute("duration_ms", duration_ms);
        span->SetStatus(trace_api::StatusCode::kOk);
        span->End();

        return grpc::Status::OK;
    }

private:
    std::shared_ptr<trace_api::Tracer> tracer_;
    std::unique_ptr<metrics_api::Counter<uint64_t>> request_counter_metric_;
    std::unique_ptr<metrics_api::Histogram<double>> latency_histogram_;
    std::atomic<uint64_t> request_counter_;
};

void InitTracer() {
    const char* endpoint = std::getenv("OTEL_EXPORTER_OTLP_ENDPOINT");
    std::string otlp_endpoint = endpoint ? endpoint : "http://localhost:4317";

    otlp::OtlpGrpcExporterOptions opts;
    opts.endpoint = otlp_endpoint;

    auto exporter = otlp::OtlpGrpcExporterFactory::Create(opts);

    trace_sdk::BatchSpanProcessorOptions bsp_opts;
    auto processor = trace_sdk::BatchSpanProcessorFactory::Create(std::move(exporter), bsp_opts);

    auto resource_attrs = resource::Resource::Create({
        {resource::SemanticConventions::kServiceName, "service-f"},
        {resource::SemanticConventions::kServiceVersion, "1.0.0"},
    });

    std::shared_ptr<trace_api::TracerProvider> provider =
        trace_sdk::TracerProviderFactory::Create(std::move(processor), resource_attrs);

    trace_api::Provider::SetTracerProvider(provider);

    // Set up context propagation
    opentelemetry::context::propagation::GlobalTextMapPropagator::SetGlobalPropagator(
        std::make_shared<opentelemetry::trace::propagation::HttpTraceContext>());
}

void InitMetrics() {
    const char* endpoint = std::getenv("OTEL_EXPORTER_OTLP_ENDPOINT");
    std::string otlp_endpoint = endpoint ? endpoint : "http://localhost:4317";

    otlp::OtlpGrpcMetricExporterOptions opts;
    opts.endpoint = otlp_endpoint;

    auto exporter = otlp::OtlpGrpcMetricExporterFactory::Create(opts);

    metrics_sdk::PeriodicExportingMetricReaderOptions reader_opts;
    reader_opts.export_interval_millis = std::chrono::milliseconds(10000);
    reader_opts.export_timeout_millis = std::chrono::milliseconds(5000);

    auto reader = metrics_sdk::PeriodicExportingMetricReaderFactory::Create(
        std::move(exporter), reader_opts);

    auto resource_attrs = resource::Resource::Create({
        {resource::SemanticConventions::kServiceName, "service-f"},
        {resource::SemanticConventions::kServiceVersion, "1.0.0"},
    });

    auto provider = metrics_sdk::MeterProviderFactory::Create(resource_attrs);
    auto* p = static_cast<metrics_sdk::MeterProvider*>(provider.get());
    p->AddMetricReader(std::move(reader));

    metrics_api::Provider::SetMeterProvider(std::move(provider));
}

void RunServer() {
    const char* port_env = std::getenv("GRPC_PORT");
    std::string port = port_env ? port_env : "50056";
    std::string server_address = "0.0.0.0:" + port;

    ServiceFImpl service;

    grpc::EnableDefaultHealthCheckService(true);
    grpc::ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);

    std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
    std::cout << "[Service F] Server listening on " << server_address << std::endl;
    std::cout << "[Service F] Legacy data service (C) ready" << std::endl;

    server->Wait();
}

int main(int argc, char** argv) {
    std::cout << "[Service F] Initializing OpenTelemetry..." << std::endl;
    InitTracer();
    InitMetrics();

    std::cout << "[Service F] Starting gRPC server..." << std::endl;
    RunServer();

    return 0;
}
