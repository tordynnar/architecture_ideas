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

#include "services.grpc.pb.h"

class ServiceFImpl final : public grpcarch::ServiceF::Service {
public:
    grpc::Status FetchLegacyData(
        grpc::ServerContext* context,
        const grpcarch::LegacyDataRequest* request,
        grpcarch::LegacyDataResponse* response) override {

        auto start = std::chrono::high_resolution_clock::now();

        std::cout << "[Service F] FetchLegacyData called - record_id: "
                  << request->record_id() << ", table: " << request->table_name() << std::endl;

        // Simulate DB lookup delay (3-8ms)
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> delay_dist(3, 8);
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_dist(gen)));

        // Build response
        response->mutable_status()->set_success(true);
        response->mutable_status()->set_message("Record fetched successfully from " + request->table_name());

        auto* record = response->mutable_record();
        record->set_id(request->record_id());

        std::string raw_data = "{\"source\": \"" + request->table_name() +
                               "\", \"data\": \"legacy_value_" + request->record_id() + "\"}";
        record->set_raw_data(raw_data);

        auto now = std::chrono::system_clock::now();
        auto epoch = now.time_since_epoch();
        record->set_created_at(std::chrono::duration_cast<std::chrono::seconds>(epoch).count() - 86400);
        record->set_updated_at(std::chrono::duration_cast<std::chrono::seconds>(epoch).count());
        (*record->mutable_fields())["source"] = request->table_name();
        (*record->mutable_fields())["fetched_by"] = "service-f";

        auto end = std::chrono::high_resolution_clock::now();
        double duration_ms = std::chrono::duration<double, std::milli>(end - start).count();

        std::cout << "[Service F] Record fetched successfully (duration: " << duration_ms << "ms)"
                  << std::endl;

        return grpc::Status::OK;
    }
};

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
    std::cout << "[Service F] Starting gRPC server..." << std::endl;
    RunServer();
    return 0;
}
