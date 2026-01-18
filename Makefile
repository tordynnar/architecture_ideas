.PHONY: all build up down logs clean proto help

# Default target
all: build up

# Build all services
build:
	@echo "Building all services..."
	docker-compose build

# Build specific service
build-%:
	@echo "Building service-$*..."
	docker-compose build service-$*

# Start all services
up:
	@echo "Starting all services..."
	docker-compose up -d

# Start services with logs
up-logs:
	@echo "Starting all services with logs..."
	docker-compose up

# Stop all services
down:
	@echo "Stopping all services..."
	docker-compose down

# Stop and remove volumes
down-clean:
	@echo "Stopping all services and removing volumes..."
	docker-compose down -v

# View logs
logs:
	docker-compose logs -f

# View logs for specific service
logs-%:
	docker-compose logs -f service-$*

# Restart services
restart:
	@echo "Restarting all services..."
	docker-compose restart

# Restart specific service
restart-%:
	@echo "Restarting service-$*..."
	docker-compose restart service-$*

# Check service status
status:
	docker-compose ps

# Clean up
clean:
	@echo "Cleaning up..."
	docker-compose down -v --rmi local
	rm -rf services/service-a/proto/*.pb.go
	rm -rf services/service-b/target
	rm -rf services/service-c/__pycache__ services/service-c/*_pb2*.py
	rm -rf services/service-d/bin services/service-d/obj
	rm -rf services/service-e/build
	rm -rf services/service-f/build

# Generate proto files locally (for development)
proto:
	@echo "Generating proto files..."
	# Go
	mkdir -p services/service-a/proto
	protoc --proto_path=./proto \
		--go_out=services/service-a/proto --go_opt=paths=source_relative \
		--go-grpc_out=services/service-a/proto --go-grpc_opt=paths=source_relative \
		./proto/common.proto ./proto/services.proto
	# Python
	python -m grpc_tools.protoc \
		-I./proto \
		--python_out=services/service-c \
		--grpc_python_out=services/service-c \
		./proto/common.proto ./proto/services.proto

# Run observability stack only
observability:
	@echo "Starting observability stack..."
	docker-compose up -d otel-collector jaeger prometheus grafana

# URLs for accessing services
urls:
	@echo ""
	@echo "=== Observability Stack URLs ==="
	@echo "Jaeger UI:     http://localhost:16686"
	@echo "Prometheus:    http://localhost:9090"
	@echo "Grafana:       http://localhost:3000 (admin/admin)"
	@echo ""
	@echo "=== Service Ports ==="
	@echo "Service A (Go):      localhost:50051"
	@echo "Service B (Rust):    localhost:50052"
	@echo "Service C (Python):  localhost:50053"
	@echo "Service D (C#):      localhost:50054"
	@echo "Service E (C++):     localhost:50055"
	@echo "Service F (C):       localhost:50056"
	@echo ""

# Trigger workload manually (requires grpcurl)
trigger:
	@echo "Triggering workload on Service A..."
	grpcurl -plaintext -d '{"iterations": 50}' \
		localhost:50051 grpcarch.ServiceA/TriggerWorkload

# Health check all services
health:
	@echo "Checking service health..."
	@echo "Service A:" && grpcurl -plaintext localhost:50051 grpcarch.ServiceA/HealthCheck || echo "  Not responding"
	@echo ""

# Help
help:
	@echo "Polyglot gRPC Microservices"
	@echo ""
	@echo "Usage: make [target]"
	@echo ""
	@echo "Targets:"
	@echo "  all          Build and start all services (default)"
	@echo "  build        Build all Docker images"
	@echo "  build-X      Build specific service (a, b, c, d, e, f)"
	@echo "  up           Start all services in background"
	@echo "  up-logs      Start all services with logs"
	@echo "  down         Stop all services"
	@echo "  down-clean   Stop services and remove volumes"
	@echo "  logs         View all logs"
	@echo "  logs-X       View logs for specific service"
	@echo "  restart      Restart all services"
	@echo "  restart-X    Restart specific service"
	@echo "  status       Show service status"
	@echo "  clean        Clean up all artifacts"
	@echo "  proto        Generate proto files locally"
	@echo "  observability Start only observability stack"
	@echo "  urls         Show access URLs"
	@echo "  trigger      Trigger workload manually"
	@echo "  health       Check service health"
	@echo "  help         Show this help"
