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

#include "services.grpc.pb.h"

class ServiceEImpl final : public grpcarch::ServiceE::Service {
public:
    ServiceEImpl(const std::string& service_d_addr)
        : service_d_addr_(service_d_addr) {
        service_d_stub_ = grpcarch::ServiceD::NewStub(
            grpc::CreateChannel(service_d_addr, grpc::InsecureChannelCredentials()));
    }

    grpc::Status Compute(
        grpc::ServerContext* context,
        const grpcarch::ComputeRequest* request,
        grpcarch::ComputeResponse* response) override {

        auto start = std::chrono::high_resolution_clock::now();

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
            for (const auto& val : request->input_values()) {
                results.push_back(val);
            }
        }

        // Call Service D to validate results
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
            std::cout << "[Service E] Service D validation failed: "
                      << validation_status.error_message() << std::endl;
            response->mutable_status()->set_success(false);
            response->mutable_status()->set_message(
                "Computation complete but validation failed: " +
                validation_status.error_message());
        } else {
            response->mutable_status()->set_success(true);
            response->mutable_status()->set_message("Computation and validation successful");
        }

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

        std::cout << "[Service E] Computation complete (duration: " << duration_ms << "ms)"
                  << std::endl;

        return grpc::Status::OK;
    }

private:
    std::string service_d_addr_;
    std::unique_ptr<grpcarch::ServiceD::Stub> service_d_stub_;
};

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
    std::cout << "[Service E] Starting gRPC server..." << std::endl;
    RunServer();
    return 0;
}
