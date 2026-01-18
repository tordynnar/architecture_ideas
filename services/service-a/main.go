package main

import (
	"context"
	"fmt"
	"log"
	"math/rand"
	"net"
	"os"
	"sync"
	"time"

	"go.opentelemetry.io/contrib/instrumentation/google.golang.org/grpc/otelgrpc"
	"go.opentelemetry.io/otel"
	"go.opentelemetry.io/otel/attribute"
	"go.opentelemetry.io/otel/exporters/otlp/otlptrace/otlptracegrpc"
	"go.opentelemetry.io/otel/propagation"
	"go.opentelemetry.io/otel/sdk/resource"
	sdktrace "go.opentelemetry.io/otel/sdk/trace"
	semconv "go.opentelemetry.io/otel/semconv/v1.21.0"
	"go.opentelemetry.io/otel/trace"
	"google.golang.org/grpc"
	"google.golang.org/grpc/credentials/insecure"

	pb "service-a/proto"
)

var tracer trace.Tracer

type server struct {
	pb.UnimplementedServiceAServer
	serviceBConn   *grpc.ClientConn
	serviceBClient pb.ServiceBClient
	serviceCConn   *grpc.ClientConn
	serviceCClient pb.ServiceCClient
}

func newServer(serviceBAddr, serviceCAddr string) (*server, error) {
	// Connect to Service B
	serviceBConn, err := grpc.Dial(serviceBAddr,
		grpc.WithTransportCredentials(insecure.NewCredentials()),
		grpc.WithStatsHandler(otelgrpc.NewClientHandler()),
	)
	if err != nil {
		return nil, fmt.Errorf("failed to connect to Service B: %v", err)
	}

	// Connect to Service C
	serviceCConn, err := grpc.Dial(serviceCAddr,
		grpc.WithTransportCredentials(insecure.NewCredentials()),
		grpc.WithStatsHandler(otelgrpc.NewClientHandler()),
	)
	if err != nil {
		serviceBConn.Close()
		return nil, fmt.Errorf("failed to connect to Service C: %v", err)
	}

	return &server{
		serviceBConn:   serviceBConn,
		serviceBClient: pb.NewServiceBClient(serviceBConn),
		serviceCConn:   serviceCConn,
		serviceCClient: pb.NewServiceCClient(serviceCConn),
	}, nil
}

func (s *server) TriggerWorkload(ctx context.Context, req *pb.WorkloadRequest) (*pb.WorkloadResponse, error) {
	ctx, span := tracer.Start(ctx, "TriggerWorkload",
		trace.WithAttributes(
			attribute.String("rpc.system", "grpc"),
			attribute.String("rpc.service", "ServiceA"),
			attribute.String("rpc.method", "TriggerWorkload"),
		),
	)
	defer span.End()

	iterations := int(req.Iterations)
	if iterations <= 0 {
		iterations = 50 // Default to 50 iterations
	}

	log.Printf("[Service A] TriggerWorkload called - iterations: %d", iterations)
	span.SetAttributes(attribute.Int("iterations", iterations))

	response := &pb.WorkloadResponse{
		Status: &pb.ResponseStatus{
			Success: true,
			Message: "Workload started",
		},
		Results: make([]*pb.IterationResult, 0, iterations),
	}

	successCount := 0
	failCount := 0

	for i := 0; i < iterations; i++ {
		iterCtx, iterSpan := tracer.Start(ctx, fmt.Sprintf("workload-iteration-%d", i+1),
			trace.WithAttributes(attribute.Int("iteration", i+1)),
		)

		result := s.runIteration(iterCtx, i+1)
		response.Results = append(response.Results, result)

		if result.Success {
			successCount++
		} else {
			failCount++
		}

		iterSpan.SetAttributes(
			attribute.Bool("success", result.Success),
			attribute.Int64("duration_ms", result.DurationMs),
		)
		iterSpan.End()

		// Delay between iterations (100ms)
		time.Sleep(100 * time.Millisecond)
	}

	response.SuccessfulIterations = int32(successCount)
	response.FailedIterations = int32(failCount)
	response.Status.Message = fmt.Sprintf("Workload complete: %d successful, %d failed", successCount, failCount)

	span.SetAttributes(
		attribute.Int("successful_iterations", successCount),
		attribute.Int("failed_iterations", failCount),
	)

	log.Printf("[Service A] Workload complete - success: %d, failed: %d", successCount, failCount)

	return response, nil
}

func (s *server) runIteration(ctx context.Context, iteration int) *pb.IterationResult {
	start := time.Now()
	result := &pb.IterationResult{
		Iteration: int32(iteration),
		Success:   true,
	}

	// Simulate orchestration delay (5-15ms)
	time.Sleep(time.Duration(5+rand.Intn(11)) * time.Millisecond)

	// Call Service B and C in parallel
	var wg sync.WaitGroup
	var bErr, cErr error

	wg.Add(2)

	// Call Service B
	go func() {
		defer wg.Done()
		_, bSpan := tracer.Start(ctx, "call-service-b")
		defer bSpan.End()

		log.Printf("[Service A] Iteration %d: Calling Service B...", iteration)

		req := &pb.ProcessRequest{
			Metadata: &pb.RequestMetadata{
				CallerService: "service-a",
				TimestampMs:   time.Now().UnixMilli(),
			},
			Payload: &pb.DataPayload{
				Id:      fmt.Sprintf("iteration-%d-data", iteration),
				Content: fmt.Sprintf("Data for iteration %d", iteration),
			},
		}

		_, bErr = s.serviceBClient.ProcessData(ctx, req)
		if bErr != nil {
			bSpan.RecordError(bErr)
			log.Printf("[Service A] Iteration %d: Service B error: %v", iteration, bErr)
		}
	}()

	// Call Service C
	go func() {
		defer wg.Done()
		_, cSpan := tracer.Start(ctx, "call-service-c")
		defer cSpan.End()

		log.Printf("[Service A] Iteration %d: Calling Service C...", iteration)

		req := &pb.AnalyticsRequest{
			Metadata: &pb.RequestMetadata{
				CallerService: "service-a",
				TimestampMs:   time.Now().UnixMilli(),
			},
			InputData: &pb.DataPayload{
				Id:      fmt.Sprintf("iteration-%d-analytics", iteration),
				Content: fmt.Sprintf("Analytics input for iteration %d", iteration),
			},
			ModelName: "default-model",
		}

		_, cErr = s.serviceCClient.RunAnalytics(ctx, req)
		if cErr != nil {
			cSpan.RecordError(cErr)
			log.Printf("[Service A] Iteration %d: Service C error: %v", iteration, cErr)
		}
	}()

	wg.Wait()

	duration := time.Since(start)
	result.DurationMs = duration.Milliseconds()

	// Check for errors
	var errors []string
	if bErr != nil {
		errors = append(errors, fmt.Sprintf("Service B: %v", bErr))
	}
	if cErr != nil {
		errors = append(errors, fmt.Sprintf("Service C: %v", cErr))
	}

	if len(errors) > 0 {
		result.Success = false
		result.ErrorMessage = fmt.Sprintf("Errors: %v", errors)
	}

	log.Printf("[Service A] Iteration %d complete (duration: %dms, success: %v)",
		iteration, result.DurationMs, result.Success)

	return result
}

func (s *server) HealthCheck(ctx context.Context, req *pb.HealthCheckRequest) (*pb.HealthCheckResponse, error) {
	return &pb.HealthCheckResponse{
		Healthy:     true,
		ServiceName: "service-a",
		Version:     "1.0.0",
	}, nil
}

func initTracer(ctx context.Context) (*sdktrace.TracerProvider, error) {
	endpoint := os.Getenv("OTEL_EXPORTER_OTLP_ENDPOINT")
	if endpoint == "" {
		endpoint = "localhost:4317"
	}

	exporter, err := otlptracegrpc.New(ctx,
		otlptracegrpc.WithEndpoint(endpoint),
		otlptracegrpc.WithInsecure(),
	)
	if err != nil {
		return nil, fmt.Errorf("failed to create OTLP exporter: %w", err)
	}

	res, err := resource.New(ctx,
		resource.WithAttributes(
			semconv.ServiceNameKey.String("service-a"),
			semconv.ServiceVersionKey.String("1.0.0"),
		),
	)
	if err != nil {
		return nil, fmt.Errorf("failed to create resource: %w", err)
	}

	tp := sdktrace.NewTracerProvider(
		sdktrace.WithBatcher(exporter),
		sdktrace.WithResource(res),
	)

	otel.SetTracerProvider(tp)
	otel.SetTextMapPropagator(propagation.NewCompositeTextMapPropagator(
		propagation.TraceContext{},
		propagation.Baggage{},
	))

	tracer = tp.Tracer("service-a")

	return tp, nil
}

func main() {
	log.Println("[Service A] Initializing OpenTelemetry...")

	ctx := context.Background()
	tp, err := initTracer(ctx)
	if err != nil {
		log.Fatalf("Failed to initialize tracer: %v", err)
	}
	defer func() {
		if err := tp.Shutdown(ctx); err != nil {
			log.Printf("Error shutting down tracer provider: %v", err)
		}
	}()

	port := os.Getenv("GRPC_PORT")
	if port == "" {
		port = "50051"
	}

	serviceBAddr := os.Getenv("SERVICE_B_ADDR")
	if serviceBAddr == "" {
		serviceBAddr = "localhost:50052"
	}

	serviceCAddr := os.Getenv("SERVICE_C_ADDR")
	if serviceCAddr == "" {
		serviceCAddr = "localhost:50053"
	}

	log.Printf("[Service A] Starting gRPC server on port %s", port)
	log.Printf("[Service A] Service B address: %s", serviceBAddr)
	log.Printf("[Service A] Service C address: %s", serviceCAddr)

	srv, err := newServer(serviceBAddr, serviceCAddr)
	if err != nil {
		log.Fatalf("Failed to create server: %v", err)
	}
	defer srv.serviceBConn.Close()
	defer srv.serviceCConn.Close()

	lis, err := net.Listen("tcp", fmt.Sprintf(":%s", port))
	if err != nil {
		log.Fatalf("Failed to listen: %v", err)
	}

	grpcServer := grpc.NewServer(
		grpc.StatsHandler(otelgrpc.NewServerHandler()),
	)
	pb.RegisterServiceAServer(grpcServer, srv)

	log.Println("[Service A] Entry point service (Go) ready")
	log.Println("[Service A] Waiting for TriggerWorkload calls...")

	// Auto-trigger workload on startup after a delay
	go func() {
		time.Sleep(5 * time.Second) // Wait for other services to be ready
		log.Println("[Service A] Auto-triggering startup workload (50 iterations)...")

		ctx, span := tracer.Start(context.Background(), "startup-workload")
		defer span.End()

		req := &pb.WorkloadRequest{
			Iterations: 50,
		}
		resp, err := srv.TriggerWorkload(ctx, req)
		if err != nil {
			log.Printf("[Service A] Startup workload error: %v", err)
			span.RecordError(err)
		} else {
			log.Printf("[Service A] Startup workload complete: %d/%d successful",
				resp.SuccessfulIterations, resp.SuccessfulIterations+resp.FailedIterations)
		}
	}()

	if err := grpcServer.Serve(lis); err != nil {
		log.Fatalf("Failed to serve: %v", err)
	}
}
