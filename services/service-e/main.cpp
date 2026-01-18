#include <iostream>
#include <memory>
#include <string>
#include <random>
#include <thread>
#include <chrono>
#include <cstdlib>
#include <numeric>

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

class ServiceEImpl final : public grpcarch::ServiceE::Service {
public:
    ServiceEImpl(const std::string& service_d_addr)
        : service_d_addr_(service_d_addr) {
        auto provider = trace_api::Provider::GetTracerProvider();
        tracer_ = provider->GetTracer("service-e", "1.0.0");

        auto meter_provider = metrics_api::Provider::GetMeterProvider();
        auto meter = meter_provider->GetMeter("service-e", "1.0.0");
        request_counter_ = meter->CreateUInt64Counter("service_e_requests_total");
        latency_histogram_ = meter->CreateDoubleHistogram("service_e_request_duration_ms");

        // Create gRPC channel to Service D
        service_d_stub_ = grpcarch::ServiceD::NewStub(
            grpc::CreateChannel(service_d_addr, grpc::InsecureChannelCredentials()));
    }

    grpc::Status Compute(
        grpc::ServerContext* context,
        const grpcarch::ComputeRequest* request,
        grpcarch::ComputeResponse* response) override {

        auto start = std::chrono::high_resolution_clock::now();

        auto span = tracer_->StartSpan("Compute",
            {{trace_api::SemanticConventions::kRpcSystem, "grpc"},
             {trace_api::SemanticConventions::kRpcService, "ServiceE"},
             {trace_api::SemanticConventions::kRpcMethod, "Compute"}});
        auto scope = tracer_->WithActiveSpan(span);

        span->SetAttribute("operation", request->operation());
        span->SetAttribute("input_count", static_cast<int>(request->input_values_size()));

        std::cout << "[Service E] Compute called - operation: " << request->operation()
                  << ", inputs: " << request->input_values_size() << std::endl;

        // Simulate computation (8-12ms)
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> delay_dist(8, 12);
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_dist(gen)));

        // Perform computation based on operation
        std::vector<double> results;
        std::string operation = request->operation();

        if (operation == "sum") {
            double sum = std::accumulate(request->input_values().begin(),
                                         request->input_values().end(), 0.0);
            results.push_back(sum);
        } else if (operation == "average") {
            if (request->input_values_size() > 0) {
                double sum = std::accumulate(request->input_values().begin(),
                                             request->input_values().end(), 0.0);
                results.push_back(sum / request->input_values_size());
            }
        } else if (operation == "transform") {
            for (const auto& val : request->input_values()) {
                results.push_back(val * 2.0 + 1.0);
            }
        } else {
            // Default: echo values
            for (const auto& val : request->input_values()) {
                results.push_back(val);
            }
        }

        // Call Service D to validate results
        auto validation_span = tracer_->StartSpan("CallServiceD");
        {
            auto validation_scope = tracer_->WithActiveSpan(validation_span);

            grpcarch::ValidationRequest validation_req;
            validation_req.mutable_metadata()->set_caller_service("service-e");
            validation_req.mutable_data()->set_id("compute-result");
            validation_req.mutable_data()->set_content(
                "Computed " + std::to_string(results.size()) + " values");

            grpcarch::ValidationResponse validation_resp;
            grpc::ClientContext client_ctx;

            std::cout << "[Service E] Calling Service D for validation..." << std::endl;
            auto validation_status = service_d_stub_->ValidateData(
                &client_ctx, validation_req, &validation_resp);

            if (!validation_status.ok()) {
                validation_span->SetStatus(trace_api::StatusCode::kError,
                    validation_status.error_message());
                std::cout << "[Service E] Service D validation failed: "
                          << validation_status.error_message() << std::endl;

                // Still return our results, but note the validation failure
                response->mutable_status()->set_success(false);
                response->mutable_status()->set_message(
                    "Computation complete but validation failed: " +
                    validation_status.error_message());
            } else {
                validation_span->SetStatus(trace_api::StatusCode::kOk);
                response->mutable_status()->set_success(true);
                response->mutable_status()->set_message("Computation and validation successful");
            }
        }
        validation_span->End();

        // Set output values
        for (const auto& result : results) {
            response->add_output_values(result);
        }

        // Set metrics
        auto end = std::chrono::high_resolution_clock::now();
        double duration_ms = std::chrono::duration<double, std::milli>(end - start).count();

        auto* metrics = response->mutable_metrics();
        metrics->set_compute_time_ms(static_cast<int64_t>(duration_ms));
        metrics->set_operations_performed(results.size());
        metrics->set_memory_used_mb(0.5);

        // Record telemetry
        request_counter_->Add(1, {{"method", "Compute"}, {"status", "ok"}});
        latency_histogram_->Record(duration_ms, {{"method", "Compute"}});

        span->SetAttribute("duration_ms", duration_ms);
        span->SetAttribute("output_count", static_cast<int>(results.size()));
        span->SetStatus(trace_api::StatusCode::kOk);
        span->End();

        std::cout << "[Service E] Computation complete (duration: " << duration_ms << "ms)"
                  << std::endl;

        return grpc::Status::OK;
    }

private:
    std::string service_d_addr_;
    std::shared_ptr<trace_api::Tracer> tracer_;
    std::unique_ptr<metrics_api::Counter<uint64_t>> request_counter_;
    std::unique_ptr<metrics_api::Histogram<double>> latency_histogram_;
    std::unique_ptr<grpcarch::ServiceD::Stub> service_d_stub_;
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
        {resource::SemanticConventions::kServiceName, "service-e"},
        {resource::SemanticConventions::kServiceVersion, "1.0.0"},
    });

    std::shared_ptr<trace_api::TracerProvider> provider =
        trace_sdk::TracerProviderFactory::Create(std::move(processor), resource_attrs);

    trace_api::Provider::SetTracerProvider(provider);

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
        {resource::SemanticConventions::kServiceName, "service-e"},
        {resource::SemanticConventions::kServiceVersion, "1.0.0"},
    });

    auto provider = metrics_sdk::MeterProviderFactory::Create(resource_attrs);
    auto* p = static_cast<metrics_sdk::MeterProvider*>(provider.get());
    p->AddMetricReader(std::move(reader));

    metrics_api::Provider::SetMeterProvider(std::move(provider));
}

void RunServer() {
    const char* port_env = std::getenv("GRPC_PORT");
    std::string port = port_env ? port_env : "50055";
    std::string server_address = "0.0.0.0:" + port;

    const char* service_d_env = std::getenv("SERVICE_D_ADDR");
    std::string service_d_addr = service_d_env ? service_d_env : "localhost:50054";

    ServiceEImpl service(service_d_addr);

    grpc::EnableDefaultHealthCheckService(true);
    grpc::ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);

    std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
    std::cout << "[Service E] Server listening on " << server_address << std::endl;
    std::cout << "[Service E] Computation service (C++) ready" << std::endl;
    std::cout << "[Service E] Service D address: " << service_d_addr << std::endl;

    server->Wait();
}

int main(int argc, char** argv) {
    std::cout << "[Service E] Initializing OpenTelemetry..." << std::endl;
    InitTracer();
    InitMetrics();

    std::cout << "[Service E] Starting gRPC server..." << std::endl;
    RunServer();

    return 0;
}
