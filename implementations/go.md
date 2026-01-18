# Go Implementation

Go has first-class OpenTelemetry support with idiomatic patterns.

## Key Libraries

- `go.opentelemetry.io/otel` - Core OpenTelemetry API
- `go.opentelemetry.io/otel/exporters/otlp/otlptrace/otlptracegrpc` - OTLP trace exporter
- `go.opentelemetry.io/otel/exporters/otlp/otlpmetric/otlpmetricgrpc` - OTLP metrics exporter
- `go.opentelemetry.io/otel/exporters/otlp/otlplog/otlploggrpc` - OTLP log exporter
- `go.opentelemetry.io/contrib/instrumentation/google.golang.org/grpc/otelgrpc` - gRPC instrumentation

## Code Sample

```go
package main

import (
	"context"
	"log/slog"
	"os"
	"time"

	"go.opentelemetry.io/contrib/instrumentation/google.golang.org/grpc/otelgrpc"
	"go.opentelemetry.io/otel"
	"go.opentelemetry.io/otel/attribute"
	"go.opentelemetry.io/otel/exporters/otlp/otlplog/otlploggrpc"
	"go.opentelemetry.io/otel/exporters/otlp/otlpmetric/otlpmetricgrpc"
	"go.opentelemetry.io/otel/exporters/otlp/otlptrace/otlptracegrpc"
	"go.opentelemetry.io/otel/log/global"
	"go.opentelemetry.io/otel/metric"
	"go.opentelemetry.io/otel/propagation"
	sdklog "go.opentelemetry.io/otel/sdk/log"
	sdkmetric "go.opentelemetry.io/otel/sdk/metric"
	"go.opentelemetry.io/otel/sdk/resource"
	sdktrace "go.opentelemetry.io/otel/sdk/trace"
	semconv "go.opentelemetry.io/otel/semconv/v1.26.0"
	"go.opentelemetry.io/otel/trace"
	"google.golang.org/grpc"
	"google.golang.org/grpc/credentials/insecure"

	pb "example.com/myservice/proto"
)

var (
	tracer  trace.Tracer
	meter   metric.Meter

	// Custom metrics
	requestCounter  metric.Int64Counter
	requestDuration metric.Float64Histogram
	activeRequests  metric.Int64UpDownCounter
)

func initTelemetry(ctx context.Context) (func(), error) {
	res, _ := resource.New(ctx,
		resource.WithAttributes(
			semconv.ServiceName("my-go-service"),
			semconv.ServiceVersion("1.0.0"),
		),
	)

	// ============================================================
	// TRACING SETUP
	// ============================================================
	traceExporter, _ := otlptracegrpc.New(ctx,
		otlptracegrpc.WithEndpoint("otel-collector:4317"),
		otlptracegrpc.WithInsecure(),
	)
	tracerProvider := sdktrace.NewTracerProvider(
		sdktrace.WithBatcher(traceExporter),
		sdktrace.WithResource(res),
	)
	otel.SetTracerProvider(tracerProvider)
	otel.SetTextMapPropagator(propagation.TraceContext{})
	tracer = otel.Tracer("my-go-service")

	// ============================================================
	// METRICS SETUP
	// ============================================================
	metricExporter, _ := otlpmetricgrpc.New(ctx,
		otlpmetricgrpc.WithEndpoint("otel-collector:4317"),
		otlpmetricgrpc.WithInsecure(),
	)
	meterProvider := sdkmetric.NewMeterProvider(
		sdkmetric.WithReader(sdkmetric.NewPeriodicReader(metricExporter,
			sdkmetric.WithInterval(10*time.Second))),
		sdkmetric.WithResource(res),
	)
	otel.SetMeterProvider(meterProvider)
	meter = otel.Meter("my-go-service")

	// Create custom metrics instruments
	requestCounter, _ = meter.Int64Counter("myservice.requests.total",
		metric.WithDescription("Total number of requests processed"),
		metric.WithUnit("{requests}"),
	)
	requestDuration, _ = meter.Float64Histogram("myservice.request.duration",
		metric.WithDescription("Request processing duration"),
		metric.WithUnit("s"),
	)
	activeRequests, _ = meter.Int64UpDownCounter("myservice.requests.active",
		metric.WithDescription("Number of requests currently being processed"),
		metric.WithUnit("{requests}"),
	)

	// ============================================================
	// LOGGING SETUP
	// ============================================================
	logExporter, _ := otlploggrpc.New(ctx,
		otlploggrpc.WithEndpoint("otel-collector:4317"),
		otlploggrpc.WithInsecure(),
	)
	loggerProvider := sdklog.NewLoggerProvider(
		sdklog.WithProcessor(sdklog.NewBatchProcessor(logExporter)),
		sdklog.WithResource(res),
	)
	global.SetLoggerProvider(loggerProvider)

	return func() {
		tracerProvider.Shutdown(ctx)
		meterProvider.Shutdown(ctx)
		loggerProvider.Shutdown(ctx)
	}, nil
}

// gRPC server with automatic tracing and metrics
func newGRPCServer() *grpc.Server {
	return grpc.NewServer(
		grpc.StatsHandler(otelgrpc.NewServerHandler()),
	)
}

// gRPC client with automatic tracing and metrics
func newGRPCClient(ctx context.Context, addr string) (*grpc.ClientConn, error) {
	return grpc.DialContext(ctx, addr,
		grpc.WithTransportCredentials(insecure.NewCredentials()),
		grpc.WithStatsHandler(otelgrpc.NewClientHandler()),
	)
}

// Service implementation with manual spans, metrics, and contextual logging
type MyServiceServer struct {
	pb.UnimplementedMyServiceServer
	logger *slog.Logger
}

func (s *MyServiceServer) ProcessRequest(ctx context.Context, req *pb.Request) (*pb.Response, error) {
	startTime := time.Now()

	// Track active requests (metric)
	activeRequests.Add(ctx, 1)
	defer activeRequests.Add(ctx, -1)

	// Extract trace context for contextual logging
	spanCtx := trace.SpanContextFromContext(ctx)
	logger := s.logger.With(
		slog.String("trace_id", spanCtx.TraceID().String()),
		slog.String("span_id", spanCtx.SpanID().String()),
	)

	logger.InfoContext(ctx, "Processing request", slog.String("request_id", req.Id))

	// Manual span for specific operation
	ctx, span := tracer.Start(ctx, "process-business-logic")
	span.SetAttributes(attribute.String("request.id", req.Id))
	defer span.End()

	result, err := s.doBusinessLogic(ctx, req)

	// Record metrics
	duration := time.Since(startTime).Seconds()
	attrs := metric.WithAttributes(
		attribute.String("method", "ProcessRequest"),
		attribute.Bool("error", err != nil),
	)
	requestCounter.Add(ctx, 1, attrs)
	requestDuration.Record(ctx, duration, attrs)

	if err != nil {
		span.RecordError(err)
		logger.ErrorContext(ctx, "Business logic failed", slog.Any("error", err))
		return nil, err
	}

	logger.InfoContext(ctx, "Request processed successfully",
		slog.Float64("duration_seconds", duration))
	return result, nil
}

func (s *MyServiceServer) doBusinessLogic(ctx context.Context, req *pb.Request) (*pb.Response, error) {
	// Another manual span for nested operation
	ctx, span := tracer.Start(ctx, "database-query")
	span.SetAttributes(attribute.String("db.system", "postgresql"))
	defer span.End()

	// Business logic here
	return &pb.Response{}, nil
}

func main() {
	ctx := context.Background()
	shutdown, _ := initTelemetry(ctx)
	defer shutdown()

	logger := slog.New(slog.NewJSONHandler(os.Stdout, nil))
	server := newGRPCServer()
	pb.RegisterMyServiceServer(server, &MyServiceServer{logger: logger})
	// Start server...
}
```

## How It Works

### Tracing

**Automatic Tracing via gRPC Interceptor:**
The `otelgrpc.NewServerHandler()` stats handler automatically creates a span for every incoming gRPC request. This span captures:
- RPC method name (`rpc.method`)
- gRPC status code (`rpc.grpc.status_code`)
- Request/response message counts
- Error details if the RPC fails

The trace context is automatically extracted from incoming gRPC metadata headers (`traceparent`) and propagated to child spans.

**Manual Spans:**
Use `tracer.Start(ctx, "span-name")` to create child spans for specific operations. The context must be passed through to maintain the parent-child relationship. Always call `span.End()` when the operation completes (typically via `defer`).

**Span Attributes:**
Add contextual information to spans using `span.SetAttributes()`. This data appears in Jaeger and helps with debugging and analysis.

**Context Propagation:**
The `propagation.TraceContext{}` propagator ensures W3C Trace Context headers are used when making outbound gRPC calls, enabling distributed tracing across services.

### Metrics

**Automatic gRPC Metrics:**
The `otelgrpc.NewServerHandler()` automatically records standard gRPC metrics:
- `rpc.server.duration` - Histogram of RPC durations
- `rpc.server.request.size` - Request message sizes
- `rpc.server.response.size` - Response message sizes
- `rpc.server.requests_per_rpc` - Messages per RPC

**Custom Metrics Instruments:**
- **Counter** (`Int64Counter`): Monotonically increasing values, ideal for counting requests or errors
- **Histogram** (`Float64Histogram`): Distribution of values like latencies or sizes
- **UpDownCounter** (`Int64UpDownCounter`): Values that can increase or decrease, like active connections

**Metric Attributes:**
Add dimensions to metrics using `metric.WithAttributes()`. This enables filtering and grouping in Prometheus/Grafana (e.g., by method, error status).

**Periodic Export:**
Metrics are collected and exported every 10 seconds via `sdkmetric.NewPeriodicReader()`. This batches metrics to reduce network overhead.

### Logging

**Structured JSON Logging:**
Using `slog.NewJSONHandler()` produces structured JSON logs that are easily parsed by log aggregators.

**Trace Correlation:**
Each log entry includes `trace_id` and `span_id` extracted from the current span context. This enables clicking on a trace ID in Grafana to see all related logs, or vice versa.

**Log Levels:**
Use appropriate log levels:
- `logger.InfoContext()` for normal operations
- `logger.ErrorContext()` for errors (automatically includes error details)
- `logger.DebugContext()` for verbose debugging (typically filtered in production)

**OTLP Log Export:**
Logs are exported to the OpenTelemetry Collector via OTLP/gRPC, which forwards them to Elasticsearch. The `BatchProcessor` batches logs to reduce network calls.

## Quick Reference: gRPC Server

Minimal gRPC server method implementation with metrics, tracing, and logging:

```go
func (s *MyServiceServer) ProcessRequest(ctx context.Context, req *pb.Request) (*pb.Response, error) {
	startTime := time.Now()

	// Metrics: track active requests
	activeRequests.Add(ctx, 1, metric.WithAttributes(attribute.String("method", "ProcessRequest")))
	defer activeRequests.Add(ctx, -1, metric.WithAttributes(attribute.String("method", "ProcessRequest")))

	// Logging: extract trace context for correlation
	spanCtx := trace.SpanContextFromContext(ctx)
	logger := s.logger.With(
		slog.String("trace_id", spanCtx.TraceID().String()),
		slog.String("span_id", spanCtx.SpanID().String()),
	)
	logger.InfoContext(ctx, "Processing request", slog.String("request_id", req.Id))

	// Tracing: create manual span for business logic
	ctx, span := tracer.Start(ctx, "process-business-logic")
	span.SetAttributes(attribute.String("request.id", req.Id))
	defer span.End()

	// Execute business logic
	result, err := s.doBusinessLogic(ctx, req)

	// Metrics: record request outcome
	duration := time.Since(startTime).Seconds()
	attrs := metric.WithAttributes(
		attribute.String("method", "ProcessRequest"),
		attribute.Bool("error", err != nil),
	)
	requestCounter.Add(ctx, 1, attrs)
	requestDuration.Record(ctx, duration, attrs)

	if err != nil {
		span.RecordError(err)
		span.SetStatus(codes.Error, err.Error())
		logger.ErrorContext(ctx, "Request failed", slog.Any("error", err))
		return nil, err
	}

	logger.InfoContext(ctx, "Request completed", slog.Float64("duration_seconds", duration))
	return result, nil
}
```

## Quick Reference: gRPC Client Call

Minimal gRPC client call with metrics, tracing, and logging:

```go
func (c *MyServiceClient) CallService(ctx context.Context, requestID string) (*pb.Response, error) {
	startTime := time.Now()

	// Logging: extract trace context
	spanCtx := trace.SpanContextFromContext(ctx)
	logger := c.logger.With(
		slog.String("trace_id", spanCtx.TraceID().String()),
		slog.String("span_id", spanCtx.SpanID().String()),
	)
	logger.InfoContext(ctx, "Calling external service", slog.String("request_id", requestID))

	// Tracing: create client span (trace context auto-propagated via otelgrpc)
	ctx, span := tracer.Start(ctx, "grpc.client.ProcessRequest",
		trace.WithSpanKind(trace.SpanKindClient))
	span.SetAttributes(
		attribute.String("rpc.system", "grpc"),
		attribute.String("rpc.service", "MyService"),
		attribute.String("request.id", requestID),
	)
	defer span.End()

	// Make gRPC call (trace context automatically propagated)
	req := &pb.Request{Id: requestID}
	result, err := c.client.ProcessRequest(ctx, req)

	// Metrics: record call outcome
	duration := time.Since(startTime).Seconds()
	attrs := metric.WithAttributes(
		attribute.String("service", "other-service"),
		attribute.Bool("error", err != nil),
	)
	clientCallCounter.Add(ctx, 1, attrs)
	clientCallDuration.Record(ctx, duration, attrs)

	if err != nil {
		span.RecordError(err)
		span.SetStatus(codes.Error, err.Error())
		logger.ErrorContext(ctx, "Service call failed", slog.Any("error", err))
		return nil, err
	}

	logger.InfoContext(ctx, "Service call completed", slog.Float64("duration_seconds", duration))
	return result, nil
}
