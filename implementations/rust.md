# Rust Implementation

Rust uses the `opentelemetry` crate ecosystem with `tonic` for gRPC and `tracing` for instrumentation.

## Key Crates

- `opentelemetry` - Core OpenTelemetry API
- `opentelemetry-otlp` (with `grpc-tonic` feature) - OTLP exporter
- `opentelemetry_sdk` - SDK implementation for traces and metrics
- `tracing` - Application-level tracing framework
- `tracing-opentelemetry` - Bridge between tracing and OpenTelemetry
- `tonic` - gRPC implementation

## Code Sample

```rust
use opentelemetry::global;
use opentelemetry::{metrics::MeterProvider, KeyValue};
use opentelemetry_otlp::WithExportConfig;
use opentelemetry_sdk::{
    metrics::{PeriodicReader, SdkMeterProvider},
    propagation::TraceContextPropagator,
    Resource,
};
use opentelemetry_semantic_conventions::resource::{SERVICE_NAME, SERVICE_VERSION};
use std::sync::Arc;
use std::time::{Duration, Instant};
use tonic::{transport::Server, Request, Response, Status};
use tracing::{error, info, info_span, instrument, Span};
use tracing_subscriber::{layer::SubscriberExt, util::SubscriberInitExt};

pub mod myservice {
    tonic::include_proto!("myservice");
}
use myservice::{
    my_service_server::{MyService, MyServiceServer},
    MyRequest, MyResponse,
};

// Custom metrics
struct Metrics {
    request_counter: opentelemetry::metrics::Counter<u64>,
    request_duration: opentelemetry::metrics::Histogram<f64>,
    active_requests: opentelemetry::metrics::UpDownCounter<i64>,
}

fn init_telemetry() -> (Arc<Metrics>, impl FnOnce()) {
    global::set_text_map_propagator(TraceContextPropagator::new());

    let resource = Resource::new(vec![
        KeyValue::new(SERVICE_NAME, "my-rust-service"),
        KeyValue::new(SERVICE_VERSION, "1.0.0"),
    ]);

    // ============================================================
    // TRACING SETUP
    // ============================================================
    let tracer = opentelemetry_otlp::new_pipeline()
        .tracing()
        .with_exporter(
            opentelemetry_otlp::new_exporter()
                .tonic()
                .with_endpoint("http://otel-collector:4317"),
        )
        .with_trace_config(
            opentelemetry_sdk::trace::Config::default().with_resource(resource.clone()),
        )
        .install_batch(opentelemetry_sdk::runtime::Tokio)
        .expect("Failed to initialize tracer");

    // ============================================================
    // METRICS SETUP
    // ============================================================
    let metric_exporter = opentelemetry_otlp::new_exporter()
        .tonic()
        .with_endpoint("http://otel-collector:4317")
        .build_metrics_exporter(Box::new(
            opentelemetry_sdk::metrics::reader::DefaultTemporalitySelector::new(),
        ))
        .expect("Failed to create metric exporter");

    let reader = PeriodicReader::builder(metric_exporter, opentelemetry_sdk::runtime::Tokio)
        .with_interval(Duration::from_secs(10))
        .build();

    let meter_provider = SdkMeterProvider::builder()
        .with_reader(reader)
        .with_resource(resource)
        .build();

    global::set_meter_provider(meter_provider.clone());
    let meter = global::meter("my-rust-service");

    // Create custom metrics instruments
    let metrics = Arc::new(Metrics {
        request_counter: meter
            .u64_counter("myservice.requests.total")
            .with_description("Total number of requests processed")
            .with_unit("{requests}")
            .init(),
        request_duration: meter
            .f64_histogram("myservice.request.duration")
            .with_description("Request processing duration")
            .with_unit("s")
            .init(),
        active_requests: meter
            .i64_up_down_counter("myservice.requests.active")
            .with_description("Number of requests currently being processed")
            .with_unit("{requests}")
            .init(),
    });

    // ============================================================
    // LOGGING SETUP (via tracing)
    // ============================================================
    let telemetry_layer = tracing_opentelemetry::layer().with_tracer(tracer);

    tracing_subscriber::registry()
        .with(telemetry_layer)
        .with(
            tracing_subscriber::fmt::layer()
                .json()
                .with_span_list(true)
                .with_current_span(true),
        )
        .init();

    let shutdown = move || {
        global::shutdown_tracer_provider();
        meter_provider.shutdown().ok();
    };

    (metrics, shutdown)
}

#[derive(Clone)]
pub struct MyServiceImpl {
    metrics: Arc<Metrics>,
}

impl MyServiceImpl {
    pub fn new(metrics: Arc<Metrics>) -> Self {
        Self { metrics }
    }
}

#[tonic::async_trait]
impl MyService for MyServiceImpl {
    // Automatic span creation via #[instrument]
    #[instrument(skip(self, request), fields(request_id = %request.get_ref().id))]
    async fn process_request(
        &self,
        request: Request<MyRequest>,
    ) -> Result<Response<MyResponse>, Status> {
        let start_time = Instant::now();

        // Track active requests (metric)
        self.metrics.active_requests.add(
            1,
            &[KeyValue::new("method", "ProcessRequest")],
        );

        let req = request.into_inner();

        // Contextual logging - automatically includes trace_id and span_id via tracing
        info!(request_id = %req.id, "Processing request");

        // Manual span for specific operation
        let result = self.do_business_logic(&req).await;

        // Record metrics
        let duration = start_time.elapsed().as_secs_f64();
        let error_occurred = result.is_err();

        let attrs = [
            KeyValue::new("method", "ProcessRequest"),
            KeyValue::new("error", error_occurred),
        ];

        self.metrics.request_counter.add(1, &attrs);
        self.metrics.request_duration.record(duration, &attrs);
        self.metrics.active_requests.add(
            -1,
            &[KeyValue::new("method", "ProcessRequest")],
        );

        match result {
            Ok(response) => {
                info!(duration_seconds = duration, "Request processed successfully");
                Ok(Response::new(response))
            }
            Err(e) => {
                error!(error = %e, duration_seconds = duration, "Business logic failed");
                Err(Status::internal(e.to_string()))
            }
        }
    }
}

impl MyServiceImpl {
    #[instrument(skip(self))]
    async fn do_business_logic(
        &self,
        req: &MyRequest,
    ) -> Result<MyResponse, Box<dyn std::error::Error + Send + Sync>> {
        // Manual span for nested operation
        let span = info_span!("database_query", db.system = "postgresql", table = "users");
        let _guard = span.enter();

        info!("Executing database query");

        // Simulate database operation
        tokio::time::sleep(Duration::from_millis(10)).await;

        Ok(MyResponse {
            result: "success".to_string(),
        })
    }
}

// gRPC client with automatic tracing
async fn create_grpc_client(
    addr: &str,
) -> Result<
    myservice::my_service_client::MyServiceClient<tonic::transport::Channel>,
    Box<dyn std::error::Error>,
> {
    let channel = tonic::transport::Channel::from_shared(addr.to_string())?
        .connect()
        .await?;
    Ok(myservice::my_service_client::MyServiceClient::new(channel))
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let (metrics, shutdown) = init_telemetry();

    let addr = "[::1]:50051".parse()?;
    let service = MyServiceImpl::new(metrics);

    info!("Starting gRPC server on {}", addr);

    Server::builder()
        .add_service(MyServiceServer::new(service))
        .serve(addr)
        .await?;

    shutdown();
    Ok(())
}
```

## How It Works

### Tracing

**Automatic Span Creation via `#[instrument]`:**
The `#[instrument]` macro from the `tracing` crate automatically creates a span when the function is called. The span:
- Uses the function name as the span name
- Captures function arguments as span attributes (unless skipped with `skip()`)
- Records the span duration
- Propagates context to nested calls

The `tracing-opentelemetry` layer bridges Rust's `tracing` ecosystem with OpenTelemetry, exporting spans to the collector.

**Manual Spans:**
Use `info_span!()` or `tracing::span!()` to create spans manually. Enter the span with `.enter()` to make it the current span. The span automatically ends when the guard is dropped.

**Span Fields:**
Add contextual data using the `fields()` attribute in `#[instrument]` or directly in span macros. Fields appear as span attributes in Jaeger.

**Context Propagation:**
`TraceContextPropagator` ensures W3C Trace Context headers are injected/extracted for distributed tracing. When making outbound gRPC calls, the trace context is automatically propagated via tonic's interceptors.

### Metrics

**Custom Metrics Instruments:**
- **Counter** (`u64_counter`): Monotonically increasing values for counting events
- **Histogram** (`f64_histogram`): Distribution of values, automatically creates buckets for percentile calculations
- **UpDownCounter** (`i64_up_down_counter`): Gauge-like values that can increase or decrease

**Metric Attributes:**
Use `KeyValue` pairs to add dimensions to metrics. These become labels in Prometheus, enabling queries like `myservice_requests_total{method="ProcessRequest", error="false"}`.

**Periodic Export:**
The `PeriodicReader` collects and exports metrics every 10 seconds. Metrics are batched to reduce network overhead and exported via OTLP/gRPC.

**Thread Safety:**
Metrics instruments are thread-safe and can be shared across async tasks using `Arc`. The `Clone` derive on `MyServiceImpl` allows the service to be shared across multiple gRPC handlers.

### Logging

**Structured Logging via `tracing`:**
The `tracing` crate provides structured, contextual logging. Log macros like `info!()`, `error!()` automatically include:
- Current span context (trace_id, span_id)
- Span hierarchy
- Custom fields

**JSON Output:**
The `tracing_subscriber::fmt::layer().json()` formats logs as JSON for easy parsing by log aggregators. Each log entry includes:
```json
{
  "timestamp": "2024-01-15T10:30:00.000Z",
  "level": "INFO",
  "message": "Processing request",
  "target": "my_rust_service",
  "span": {"name": "process_request", "request_id": "abc123"},
  "spans": [{"name": "process_request"}]
}
```

**Log Levels:**
- `tracing::info!()` - Normal operations
- `tracing::error!()` - Errors with automatic error formatting
- `tracing::debug!()` - Verbose debugging (compile-time filtered in release builds)
- `tracing::warn!()` - Warnings

**OpenTelemetry Integration:**
The `tracing-opentelemetry` layer exports trace data to the OpenTelemetry Collector. Logs are correlated with traces via the span context, which is automatically included in each log event.

## Quick Reference: gRPC Server

Minimal gRPC server method implementation with metrics, tracing, and logging:

```rust
#[tonic::async_trait]
impl MyService for MyServiceImpl {
    #[instrument(skip(self, request), fields(request_id = %request.get_ref().id))]
    async fn process_request(
        &self,
        request: Request<MyRequest>,
    ) -> Result<Response<MyResponse>, Status> {
        let start_time = Instant::now();

        // Metrics: track active requests
        self.metrics.active_requests.add(1, &[KeyValue::new("method", "ProcessRequest")]);

        let req = request.into_inner();

        // Logging: automatically includes trace_id/span_id via tracing
        info!(request_id = %req.id, "Processing request");

        // Tracing: manual span for business logic
        let result = {
            let span = info_span!("process-business-logic", request.id = %req.id);
            let _guard = span.enter();

            info!("Executing business logic");
            self.do_business_logic(&req).await
        };

        // Metrics: record request outcome
        let duration = start_time.elapsed().as_secs_f64();
        let is_error = result.is_err();
        let attrs = [
            KeyValue::new("method", "ProcessRequest"),
            KeyValue::new("error", is_error),
        ];
        self.metrics.request_counter.add(1, &attrs);
        self.metrics.request_duration.record(duration, &attrs);
        self.metrics.active_requests.add(-1, &[KeyValue::new("method", "ProcessRequest")]);

        match result {
            Ok(response) => {
                info!(duration_seconds = duration, "Request completed");
                Ok(Response::new(response))
            }
            Err(e) => {
                error!(error = %e, duration_seconds = duration, "Request failed");
                Err(Status::internal(e.to_string()))
            }
        }
    }
}
```

## Quick Reference: gRPC Client Call

Minimal gRPC client call with metrics, tracing, and logging:

```rust
impl MyServiceClient {
    #[instrument(skip(self))]
    pub async fn call_service(&self, request_id: &str) -> Result<MyResponse, Box<dyn std::error::Error>> {
        let start_time = Instant::now();

        // Logging: automatically includes trace context
        info!(request_id = %request_id, "Calling external service");

        // Tracing: create client span
        let span = info_span!(
            "grpc.client.ProcessRequest",
            rpc.system = "grpc",
            rpc.service = "MyService",
            request.id = %request_id,
        );
        let _guard = span.enter();

        // Make gRPC call (trace context propagated via tonic interceptor)
        let request = MyRequest { id: request_id.to_string() };
        let result = self.client.clone().process_request(request).await;

        // Metrics: record call outcome
        let duration = start_time.elapsed().as_secs_f64();
        let is_error = result.is_err();
        let attrs = [
            KeyValue::new("service", "other-service"),
            KeyValue::new("error", is_error),
        ];
        self.metrics.client_call_counter.add(1, &attrs);
        self.metrics.client_call_duration.record(duration, &attrs);

        match result {
            Ok(response) => {
                info!(duration_seconds = duration, "Service call completed");
                Ok(response.into_inner())
            }
            Err(e) => {
                error!(error = %e, "Service call failed");
                Err(Box::new(e))
            }
        }
    }
}
