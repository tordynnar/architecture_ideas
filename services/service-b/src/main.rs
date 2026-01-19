use std::env;
use std::sync::Arc;
use std::time::{Duration, Instant};

use opentelemetry::metrics::{Counter, Histogram, Meter};
use opentelemetry::trace::TracerProvider as _;
use opentelemetry::KeyValue;
use opentelemetry_appender_tracing::layer::OpenTelemetryTracingBridge;
use opentelemetry_otlp::{LogExporter, MetricExporter, SpanExporter, WithExportConfig};
use opentelemetry_sdk::logs::LoggerProvider;
use opentelemetry_sdk::metrics::{PeriodicReader, SdkMeterProvider};
use opentelemetry_sdk::{runtime, trace as sdktrace, Resource};
use rand::Rng;
use tonic::{transport::Server, Request, Response, Status};
use tracing::{info, instrument, warn};
use tracing_subscriber::{layer::SubscriberExt, util::SubscriberInitExt};

pub mod grpcarch {
    tonic::include_proto!("grpcarch");
}

use grpcarch::{
    service_b_server::{ServiceB, ServiceBServer},
    service_d_client::ServiceDClient,
    service_e_client::ServiceEClient,
    ComputeRequest, DataPayload, ProcessRequest, ProcessResponse, ProcessingMetrics,
    RequestMetadata, ResponseStatus, ValidationRequest,
};

/// Metrics for Service B
pub struct ServiceBMetrics {
    request_counter: Counter<u64>,
    latency_histogram: Histogram<f64>,
}

impl ServiceBMetrics {
    pub fn new(meter: Meter) -> Self {
        let request_counter = meter
            .u64_counter("service_b_requests_total")
            .with_description("Total requests to Service B")
            .build();

        let latency_histogram = meter
            .f64_histogram("service_b_request_duration_ms")
            .with_description("Request duration in milliseconds")
            .with_unit("ms")
            .build();

        Self {
            request_counter,
            latency_histogram,
        }
    }

    pub fn record_request(&self, method: &str, status: &str) {
        self.request_counter.add(
            1,
            &[
                KeyValue::new("method", method.to_string()),
                KeyValue::new("status", status.to_string()),
            ],
        );
    }

    pub fn record_latency(&self, method: &str, duration_ms: f64) {
        self.latency_histogram.record(
            duration_ms,
            &[KeyValue::new("method", method.to_string())],
        );
    }
}

pub struct ServiceBImpl {
    service_d_addr: String,
    service_e_addr: String,
    metrics: Arc<ServiceBMetrics>,
}

impl ServiceBImpl {
    pub fn new(service_d_addr: String, service_e_addr: String, metrics: Arc<ServiceBMetrics>) -> Self {
        Self {
            service_d_addr,
            service_e_addr,
            metrics,
        }
    }
}

#[tonic::async_trait]
impl ServiceB for ServiceBImpl {
    #[instrument(skip(self, request), fields(service = "service-b"))]
    async fn process_data(
        &self,
        request: Request<ProcessRequest>,
    ) -> Result<Response<ProcessResponse>, Status> {
        let start = Instant::now();
        let req = request.into_inner();

        let data_id = req
            .payload
            .as_ref()
            .map(|p| p.id.clone())
            .unwrap_or_default();
        info!("[Service B] ProcessData called - data_id: {}", data_id);

        // Simulate processing delay (10-20ms)
        let delay_ms = rand::thread_rng().gen_range(10..=20);
        tokio::time::sleep(Duration::from_millis(delay_ms)).await;

        // Call Service E first (computation)
        let compute_result = self.call_service_e(&req).await;

        // Then call Service D (validation)
        let validation_result = self.call_service_d(&req).await;

        let duration_ms = start.elapsed().as_millis() as i64;

        // Build response
        let mut response = ProcessResponse {
            status: Some(ResponseStatus {
                success: true,
                message: String::new(),
                error_code: 0,
            }),
            result: Some(DataPayload {
                id: format!("processed-{}", data_id),
                content: String::from("Processed data"),
                attributes: std::collections::HashMap::new(),
            }),
            metrics: Some(ProcessingMetrics {
                processing_time_ms: duration_ms,
                items_processed: 1,
                processor_id: String::from("service-b-processor"),
            }),
        };

        // Handle errors from downstream services
        let mut errors = Vec::new();
        if let Err(e) = compute_result {
            errors.push(format!("Service E: {}", e));
        }
        if let Err(e) = validation_result {
            errors.push(format!("Service D: {}", e));
        }

        if !errors.is_empty() {
            let error_msg = errors.join("; ");
            warn!("[Service B] Downstream errors: {}", error_msg);
            self.metrics.record_request("ProcessData", "error");
            self.metrics.record_latency("ProcessData", duration_ms as f64);
            if let Some(status) = response.status.as_mut() {
                status.success = false;
                status.message = format!("Partial failure: {}", error_msg);
            }
        } else {
            self.metrics.record_request("ProcessData", "ok");
            self.metrics.record_latency("ProcessData", duration_ms as f64);
            if let Some(status) = response.status.as_mut() {
                status.message = String::from("Processing completed successfully");
            }
        }

        info!(
            "[Service B] Processing complete (duration: {}ms)",
            duration_ms
        );

        Ok(Response::new(response))
    }
}

impl ServiceBImpl {
    #[instrument(skip(self, _req), fields(downstream = "service-e"))]
    async fn call_service_e(&self, _req: &ProcessRequest) -> Result<(), String> {
        info!("[Service B] Calling Service E for computation...");

        let mut client = ServiceEClient::connect(format!("http://{}", self.service_e_addr))
            .await
            .map_err(|e| format!("Failed to connect to Service E: {}", e))?;

        let compute_request = ComputeRequest {
            metadata: Some(RequestMetadata {
                request_id: String::new(),
                trace_id: String::new(),
                caller_service: String::from("service-b"),
                timestamp_ms: chrono_timestamp_ms(),
            }),
            input_values: vec![1.0, 2.0, 3.0, 4.0, 5.0],
            operation: String::from("sum"),
        };

        let response = client
            .compute(Request::new(compute_request))
            .await
            .map_err(|e| format!("Service E call failed: {}", e))?;

        let resp = response.into_inner();
        if let Some(status) = resp.status {
            if !status.success {
                return Err(format!("Service E returned failure: {}", status.message));
            }
        }

        info!(
            "[Service B] Service E computation successful, results: {:?}",
            resp.output_values
        );
        Ok(())
    }

    #[instrument(skip(self, req), fields(downstream = "service-d"))]
    async fn call_service_d(&self, req: &ProcessRequest) -> Result<(), String> {
        info!("[Service B] Calling Service D for validation...");

        let mut client = ServiceDClient::connect(format!("http://{}", self.service_d_addr))
            .await
            .map_err(|e| format!("Failed to connect to Service D: {}", e))?;

        let validation_request = ValidationRequest {
            metadata: Some(RequestMetadata {
                request_id: String::new(),
                trace_id: String::new(),
                caller_service: String::from("service-b"),
                timestamp_ms: chrono_timestamp_ms(),
            }),
            data: req.payload.clone(),
            validation_rules: vec![String::from("required"), String::from("format")],
        };

        let response = client
            .validate_data(Request::new(validation_request))
            .await
            .map_err(|e| format!("Service D call failed: {}", e))?;

        let resp = response.into_inner();
        if let Some(status) = resp.status {
            if !status.success {
                return Err(format!("Service D returned failure: {}", status.message));
            }
        }

        info!("[Service B] Service D validation successful");
        Ok(())
    }
}

fn chrono_timestamp_ms() -> i64 {
    std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .unwrap()
        .as_millis() as i64
}

fn init_telemetry() {
    let otlp_endpoint = env::var("OTEL_EXPORTER_OTLP_ENDPOINT")
        .unwrap_or_else(|_| "http://localhost:4317".into());
    let service_name = env::var("OTEL_SERVICE_NAME")
        .unwrap_or_else(|_| "service-b".into());

    let resource = Resource::new(vec![
        KeyValue::new("service.name", service_name.clone()),
        KeyValue::new("service.version", "1.0.0"),
        KeyValue::new("deployment.environment", "development"),
    ]);

    // Initialize tracer
    let span_exporter = SpanExporter::builder()
        .with_tonic()
        .with_endpoint(&otlp_endpoint)
        .build()
        .expect("Failed to create span exporter");

    let tracer_provider = sdktrace::TracerProvider::builder()
        .with_resource(resource.clone())
        .with_batch_exporter(span_exporter, runtime::Tokio)
        .build();

    let tracer = tracer_provider.tracer("service-b");

    // Initialize logger provider for OTLP log export
    let log_exporter = LogExporter::builder()
        .with_tonic()
        .with_endpoint(&otlp_endpoint)
        .build()
        .expect("Failed to create log exporter");

    let logger_provider = LoggerProvider::builder()
        .with_resource(resource.clone())
        .with_batch_exporter(log_exporter, runtime::Tokio)
        .build();

    // Initialize metrics
    let metric_exporter = MetricExporter::builder()
        .with_tonic()
        .with_endpoint(&otlp_endpoint)
        .build()
        .expect("Failed to create metric exporter");

    let metric_reader = PeriodicReader::builder(metric_exporter, runtime::Tokio)
        .with_interval(std::time::Duration::from_secs(10))
        .build();

    let meter_provider = SdkMeterProvider::builder()
        .with_resource(resource)
        .with_reader(metric_reader)
        .build();

    // Set the global meter provider to prevent it from being dropped
    opentelemetry::global::set_meter_provider(meter_provider);

    // Create OpenTelemetry tracing layer
    let otel_trace_layer = tracing_opentelemetry::layer().with_tracer(tracer);

    // Create OpenTelemetry log bridge layer
    let otel_log_layer = OpenTelemetryTracingBridge::new(&logger_provider);

    tracing_subscriber::registry()
        .with(tracing_subscriber::EnvFilter::new("info"))
        .with(tracing_subscriber::fmt::layer())
        .with(otel_trace_layer)
        .with(otel_log_layer)
        .init();

    println!("[Service B] OpenTelemetry telemetry initialized, endpoint: {}", otlp_endpoint);
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    println!("[Service B] Initializing OpenTelemetry...");
    init_telemetry();

    let port = env::var("GRPC_PORT").unwrap_or_else(|_| "50052".into());
    let service_d_addr = env::var("SERVICE_D_ADDR").unwrap_or_else(|_| "localhost:50054".into());
    let service_e_addr = env::var("SERVICE_E_ADDR").unwrap_or_else(|_| "localhost:50055".into());

    let addr = format!("0.0.0.0:{}", port).parse()?;

    // Create metrics using the global meter provider
    let meter = opentelemetry::global::meter("service-b");
    let metrics = Arc::new(ServiceBMetrics::new(meter));

    let service = ServiceBImpl::new(service_d_addr.clone(), service_e_addr.clone(), metrics);

    println!("[Service B] Starting gRPC server on port {}", port);
    println!("[Service B] Data processor service (Rust) ready");
    println!("[Service B] Service D address: {}", service_d_addr);
    println!("[Service B] Service E address: {}", service_e_addr);

    Server::builder()
        .add_service(ServiceBServer::new(service))
        .serve(addr)
        .await?;

    Ok(())
}
