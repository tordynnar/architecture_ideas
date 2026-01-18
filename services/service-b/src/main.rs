use std::env;
use std::time::{Duration, Instant};

use opentelemetry_otlp::WithExportConfig;
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

pub struct ServiceBImpl {
    service_d_addr: String,
    service_e_addr: String,
}

impl ServiceBImpl {
    pub fn new(service_d_addr: String, service_e_addr: String) -> Self {
        Self {
            service_d_addr,
            service_e_addr,
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
            if let Some(status) = response.status.as_mut() {
                status.success = false;
                status.message = format!("Partial failure: {}", error_msg);
            }
        } else if let Some(status) = response.status.as_mut() {
            status.message = String::from("Processing completed successfully");
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

fn init_tracing() {
    let otlp_endpoint = env::var("OTEL_EXPORTER_OTLP_ENDPOINT")
        .unwrap_or_else(|_| "http://localhost:4317".into());
    let service_name = env::var("OTEL_SERVICE_NAME")
        .unwrap_or_else(|_| "service-b".into());

    // Set up OTLP exporter and tracer
    let tracer = opentelemetry_otlp::new_pipeline()
        .tracing()
        .with_exporter(
            opentelemetry_otlp::new_exporter()
                .tonic()
                .with_endpoint(&otlp_endpoint),
        )
        .with_trace_config(
            sdktrace::Config::default().with_resource(Resource::new(vec![
                opentelemetry::KeyValue::new("service.name", service_name),
            ])),
        )
        .install_batch(runtime::Tokio)
        .expect("Failed to install OpenTelemetry tracer");

    // Create OpenTelemetry tracing layer
    let otel_layer = tracing_opentelemetry::layer().with_tracer(tracer);

    tracing_subscriber::registry()
        .with(tracing_subscriber::EnvFilter::new("info"))
        .with(tracing_subscriber::fmt::layer())
        .with(otel_layer)
        .init();

    println!("[Service B] OpenTelemetry tracing initialized, endpoint: {}", otlp_endpoint);
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    println!("[Service B] Initializing tracing...");
    init_tracing();

    let port = env::var("GRPC_PORT").unwrap_or_else(|_| "50052".into());
    let service_d_addr = env::var("SERVICE_D_ADDR").unwrap_or_else(|_| "localhost:50054".into());
    let service_e_addr = env::var("SERVICE_E_ADDR").unwrap_or_else(|_| "localhost:50055".into());

    let addr = format!("0.0.0.0:{}", port).parse()?;

    let service = ServiceBImpl::new(service_d_addr.clone(), service_e_addr.clone());

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
