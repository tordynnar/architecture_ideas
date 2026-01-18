import asyncio
import os
import random
import time
from concurrent import futures

import grpc
from grpc import aio

from opentelemetry import trace, metrics
from opentelemetry.sdk.trace import TracerProvider
from opentelemetry.sdk.trace.export import BatchSpanProcessor
from opentelemetry.sdk.metrics import MeterProvider
from opentelemetry.sdk.metrics.export import PeriodicExportingMetricReader
from opentelemetry.sdk.resources import SERVICE_NAME, Resource
from opentelemetry.exporter.otlp.proto.grpc.trace_exporter import OTLPSpanExporter
from opentelemetry.exporter.otlp.proto.grpc.metric_exporter import OTLPMetricExporter
from opentelemetry.instrumentation.grpc import GrpcInstrumentorClient, GrpcInstrumentorServer
from opentelemetry.trace import Status, StatusCode

import services_pb2
import services_pb2_grpc
import common_pb2


def init_telemetry():
    """Initialize OpenTelemetry tracing and metrics."""
    service_name = os.environ.get("OTEL_SERVICE_NAME", "service-c")
    otlp_endpoint = os.environ.get("OTEL_EXPORTER_OTLP_ENDPOINT", "http://localhost:4317")

    resource = Resource(attributes={SERVICE_NAME: service_name})

    # Tracing
    trace_provider = TracerProvider(resource=resource)
    trace_exporter = OTLPSpanExporter(endpoint=otlp_endpoint, insecure=True)
    trace_provider.add_span_processor(BatchSpanProcessor(trace_exporter))
    trace.set_tracer_provider(trace_provider)

    # Metrics
    metric_exporter = OTLPMetricExporter(endpoint=otlp_endpoint, insecure=True)
    metric_reader = PeriodicExportingMetricReader(metric_exporter, export_interval_millis=10000)
    meter_provider = MeterProvider(resource=resource, metric_readers=[metric_reader])
    metrics.set_meter_provider(meter_provider)

    # Instrument gRPC
    GrpcInstrumentorClient().instrument()
    GrpcInstrumentorServer().instrument()

    return trace.get_tracer("service-c"), metrics.get_meter("service-c")


tracer, meter = init_telemetry()

# Metrics
request_counter = meter.create_counter(
    "service_c_requests_total",
    description="Total requests to Service C"
)
latency_histogram = meter.create_histogram(
    "service_c_request_duration_ms",
    unit="ms",
    description="Request duration in milliseconds"
)


class ServiceCServicer(services_pb2_grpc.ServiceCServicer):
    """Analytics service implementation."""

    def __init__(self):
        service_d_addr = os.environ.get("SERVICE_D_ADDR", "localhost:50054")
        service_f_addr = os.environ.get("SERVICE_F_ADDR", "localhost:50056")

        self.service_d_channel = grpc.insecure_channel(service_d_addr)
        self.service_d_stub = services_pb2_grpc.ServiceDStub(self.service_d_channel)

        self.service_f_channel = grpc.insecure_channel(service_f_addr)
        self.service_f_stub = services_pb2_grpc.ServiceFStub(self.service_f_channel)

        print(f"[Service C] Connected to Service D at {service_d_addr}")
        print(f"[Service C] Connected to Service F at {service_f_addr}")

    def RunAnalytics(self, request, context):
        """Run analytics/ML inference."""
        start_time = time.time()

        with tracer.start_as_current_span("RunAnalytics") as span:
            span.set_attribute("rpc.system", "grpc")
            span.set_attribute("rpc.service", "ServiceC")
            span.set_attribute("rpc.method", "RunAnalytics")
            span.set_attribute("model_name", request.model_name)

            print(f"[Service C] RunAnalytics called - model: {request.model_name}, "
                  f"input_id: {request.input_data.id if request.input_data else 'N/A'}")

            # Simulate ML inference delay (15-25ms)
            inference_delay = random.uniform(15, 25) / 1000
            time.sleep(inference_delay)

            # Call Service D and Service F in parallel
            validation_result = None
            legacy_result = None
            errors = []

            with tracer.start_as_current_span("parallel_downstream_calls") as parallel_span:
                # Using ThreadPoolExecutor for parallel calls
                with futures.ThreadPoolExecutor(max_workers=2) as executor:
                    # Submit both calls
                    validation_future = executor.submit(self._call_service_d, request)
                    legacy_future = executor.submit(self._call_service_f, request)

                    # Wait for both to complete
                    try:
                        validation_result = validation_future.result(timeout=5.0)
                    except Exception as e:
                        errors.append(f"Service D error: {str(e)}")
                        print(f"[Service C] Service D call failed: {e}")

                    try:
                        legacy_result = legacy_future.result(timeout=5.0)
                    except Exception as e:
                        errors.append(f"Service F error: {str(e)}")
                        print(f"[Service C] Service F call failed: {e}")

                parallel_span.set_attribute("validation_success", validation_result is not None)
                parallel_span.set_attribute("legacy_success", legacy_result is not None)

            # Build response
            response = services_pb2.AnalyticsResponse()

            if errors:
                response.status.success = False
                response.status.message = f"Partial failure: {'; '.join(errors)}"
                span.set_status(Status(StatusCode.ERROR, response.status.message))
            else:
                response.status.success = True
                response.status.message = "Analytics completed successfully"
                span.set_status(Status(StatusCode.OK))

            # Simulated analytics result
            response.result.confidence_score = random.uniform(0.75, 0.99)
            response.result.prediction = f"prediction_for_{request.model_name}"
            response.result.feature_importance["feature_1"] = random.uniform(0.1, 0.5)
            response.result.feature_importance["feature_2"] = random.uniform(0.1, 0.3)
            response.result.feature_importance["feature_3"] = random.uniform(0.05, 0.2)
            response.result.inference_time_ms = int(inference_delay * 1000)

            duration_ms = (time.time() - start_time) * 1000

            # Record metrics
            request_counter.add(1, {"method": "RunAnalytics", "status": "ok" if response.status.success else "error"})
            latency_histogram.record(duration_ms, {"method": "RunAnalytics"})

            span.set_attribute("duration_ms", duration_ms)
            span.set_attribute("confidence_score", response.result.confidence_score)

            print(f"[Service C] Analytics complete (duration: {duration_ms:.2f}ms, "
                  f"confidence: {response.result.confidence_score:.3f})")

            return response

    def _call_service_d(self, request):
        """Call Service D for validation."""
        with tracer.start_as_current_span("CallServiceD") as span:
            validation_request = services_pb2.ValidationRequest()
            validation_request.metadata.caller_service = "service-c"
            validation_request.data.id = request.input_data.id if request.input_data else "analytics-input"
            validation_request.data.content = f"Analytics input for {request.model_name}"

            print("[Service C] Calling Service D for validation...")
            response = self.service_d_stub.ValidateData(validation_request, timeout=5.0)

            span.set_attribute("is_valid", response.is_valid)
            return response

    def _call_service_f(self, request):
        """Call Service F for legacy data."""
        with tracer.start_as_current_span("CallServiceF") as span:
            legacy_request = services_pb2.LegacyDataRequest()
            legacy_request.metadata.caller_service = "service-c"
            legacy_request.record_id = request.input_data.id if request.input_data else "default-record"
            legacy_request.table_name = "analytics_reference"

            print("[Service C] Calling Service F for legacy data...")
            response = self.service_f_stub.FetchLegacyData(legacy_request, timeout=5.0)

            span.set_attribute("record_id", legacy_request.record_id)
            return response


def serve():
    """Start the gRPC server."""
    port = os.environ.get("GRPC_PORT", "50053")
    server = grpc.server(futures.ThreadPoolExecutor(max_workers=10))
    services_pb2_grpc.add_ServiceCServicer_to_server(ServiceCServicer(), server)
    server.add_insecure_port(f"0.0.0.0:{port}")

    print(f"[Service C] Starting gRPC server on port {port}")
    print("[Service C] Analytics service (Python) ready")

    server.start()
    server.wait_for_termination()


if __name__ == "__main__":
    serve()
