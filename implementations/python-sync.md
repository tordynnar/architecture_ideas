# Python Implementation (Synchronous)

Python sync implementation uses `grpcio` with automatic instrumentation from OpenTelemetry.

## Key Packages

- `opentelemetry-api` - Core OpenTelemetry API
- `opentelemetry-sdk` - SDK implementation
- `opentelemetry-exporter-otlp-proto-grpc` - OTLP exporter
- `opentelemetry-instrumentation-grpc` - Automatic gRPC instrumentation
- `grpcio` - gRPC implementation

## Code Sample

```python
import logging
import time
import grpc
from concurrent import futures

from opentelemetry import trace, metrics
from opentelemetry.sdk.trace import TracerProvider
from opentelemetry.sdk.trace.export import BatchSpanProcessor
from opentelemetry.sdk.metrics import MeterProvider
from opentelemetry.sdk.metrics.export import PeriodicExportingMetricReader
from opentelemetry.exporter.otlp.proto.grpc.trace_exporter import OTLPSpanExporter
from opentelemetry.exporter.otlp.proto.grpc.metric_exporter import OTLPMetricExporter
from opentelemetry.exporter.otlp.proto.grpc._log_exporter import OTLPLogExporter
from opentelemetry.sdk._logs import LoggerProvider, LoggingHandler
from opentelemetry.sdk._logs.export import BatchLogRecordProcessor
from opentelemetry.sdk.resources import Resource, SERVICE_NAME, SERVICE_VERSION
from opentelemetry.instrumentation.grpc import GrpcInstrumentorServer, GrpcInstrumentorClient
from opentelemetry.trace import get_current_span

import myservice_pb2
import myservice_pb2_grpc

# ============================================================
# LOGGING SETUP
# ============================================================
# Custom filter to inject trace context into log records
class TraceContextFilter(logging.Filter):
    def filter(self, record):
        span = get_current_span()
        ctx = span.get_span_context()
        record.trace_id = format(ctx.trace_id, '032x') if ctx.is_valid else "0"
        record.span_id = format(ctx.span_id, '016x') if ctx.is_valid else "0"
        return True

# Configure structured JSON logging
logging.basicConfig(
    format='{"timestamp":"%(asctime)s","level":"%(levelname)s","message":"%(message)s","trace_id":"%(trace_id)s","span_id":"%(span_id)s"}',
    level=logging.INFO
)
logger = logging.getLogger(__name__)
logger.addFilter(TraceContextFilter())


def init_telemetry():
    resource = Resource.create({
        SERVICE_NAME: "my-python-service",
        SERVICE_VERSION: "1.0.0",
    })

    # ============================================================
    # TRACING SETUP
    # ============================================================
    trace_provider = TracerProvider(resource=resource)
    trace_processor = BatchSpanProcessor(
        OTLPSpanExporter(endpoint="otel-collector:4317", insecure=True)
    )
    trace_provider.add_span_processor(trace_processor)
    trace.set_tracer_provider(trace_provider)

    # Automatic instrumentation for gRPC server and client
    GrpcInstrumentorServer().instrument()
    GrpcInstrumentorClient().instrument()

    # ============================================================
    # METRICS SETUP
    # ============================================================
    metric_reader = PeriodicExportingMetricReader(
        OTLPMetricExporter(endpoint="otel-collector:4317", insecure=True),
        export_interval_millis=10000,  # Export every 10 seconds
    )
    meter_provider = MeterProvider(resource=resource, metric_readers=[metric_reader])
    metrics.set_meter_provider(meter_provider)

    # ============================================================
    # LOG EXPORT SETUP (OTLP)
    # ============================================================
    logger_provider = LoggerProvider(resource=resource)
    log_processor = BatchLogRecordProcessor(
        OTLPLogExporter(endpoint="otel-collector:4317", insecure=True)
    )
    logger_provider.add_log_record_processor(log_processor)

    # Add OTLP handler to root logger
    handler = LoggingHandler(level=logging.INFO, logger_provider=logger_provider)
    logging.getLogger().addHandler(handler)

    return trace.get_tracer(__name__), metrics.get_meter(__name__)


tracer, meter = init_telemetry()

# Create custom metrics instruments
request_counter = meter.create_counter(
    name="myservice.requests.total",
    description="Total number of requests processed",
    unit="{requests}",
)
request_duration = meter.create_histogram(
    name="myservice.request.duration",
    description="Request processing duration",
    unit="s",
)
active_requests = meter.create_up_down_counter(
    name="myservice.requests.active",
    description="Number of requests currently being processed",
    unit="{requests}",
)


class MyServiceServicer(myservice_pb2_grpc.MyServiceServicer):
    def ProcessRequest(self, request, context):
        start_time = time.time()

        # Track active requests (metric)
        active_requests.add(1, {"method": "ProcessRequest"})

        try:
            # Contextual logging - trace_id/span_id automatically included
            logger.info(f"Processing request: {request.id}")

            # Manual span for specific operation
            with tracer.start_as_current_span("process-business-logic") as span:
                span.set_attribute("request.id", request.id)

                try:
                    result = self._do_business_logic(request)

                    # Record success metrics
                    duration = time.time() - start_time
                    request_counter.add(1, {"method": "ProcessRequest", "error": "false"})
                    request_duration.record(duration, {"method": "ProcessRequest", "error": "false"})

                    logger.info(f"Request processed successfully in {duration:.3f}s")
                    return result

                except Exception as e:
                    span.record_exception(e)
                    span.set_status(trace.StatusCode.ERROR, str(e))

                    # Record error metrics
                    duration = time.time() - start_time
                    request_counter.add(1, {"method": "ProcessRequest", "error": "true"})
                    request_duration.record(duration, {"method": "ProcessRequest", "error": "true"})

                    logger.error(f"Business logic failed: {e}")
                    raise

        finally:
            # Always decrement active requests
            active_requests.add(-1, {"method": "ProcessRequest"})

    def _do_business_logic(self, request):
        # Another manual span for nested operation
        with tracer.start_as_current_span("database-query") as span:
            span.set_attribute("db.system", "postgresql")
            span.set_attribute("db.operation", "SELECT")

            logger.info("Executing database query")

            # Simulate database operation
            time.sleep(0.01)

            return myservice_pb2.MyResponse(result="success")


# Create gRPC client with automatic tracing (already instrumented)
def create_grpc_client(addr: str):
    channel = grpc.insecure_channel(addr)
    return myservice_pb2_grpc.MyServiceStub(channel)


def serve():
    server = grpc.server(futures.ThreadPoolExecutor(max_workers=10))
    myservice_pb2_grpc.add_MyServiceServicer_to_server(MyServiceServicer(), server)
    server.add_insecure_port('[::]:50051')
    logger.info("Starting gRPC server on port 50051")
    server.start()
    server.wait_for_termination()


if __name__ == '__main__':
    serve()
```

## How It Works

### Tracing

**Automatic Instrumentation:**
The `GrpcInstrumentorServer().instrument()` call patches gRPC to automatically create spans for every incoming RPC. These spans include:
- `rpc.system`: "grpc"
- `rpc.method`: The full method name (e.g., "/myservice.MyService/ProcessRequest")
- `rpc.grpc.status_code`: The gRPC status code
- Request/response metadata

Similarly, `GrpcInstrumentorClient().instrument()` instruments outbound gRPC calls and automatically propagates trace context via metadata headers.

**Manual Spans:**
Use `tracer.start_as_current_span("span-name")` as a context manager to create child spans. The span automatically:
- Becomes the current span within the context
- Ends when the context exits
- Records exceptions if they occur

**Span Attributes:**
Add contextual information using `span.set_attribute("key", value)`. Common attributes include request IDs, database operations, and user identifiers.

**Exception Recording:**
Use `span.record_exception(e)` to attach exception details to a span. This captures the exception type, message, and stack trace in Jaeger.

### Metrics

**Custom Metrics Instruments:**
- **Counter** (`create_counter`): Monotonically increasing values. Use for counting requests, errors, or events.
- **Histogram** (`create_histogram`): Distribution of values. Automatically creates buckets for percentile calculations (p50, p95, p99).
- **UpDownCounter** (`create_up_down_counter`): Values that can increase or decrease. Use for tracking active connections, queue depth, etc.

**Metric Attributes:**
Pass a dictionary of attributes when recording metrics. These become labels in Prometheus:
```python
request_counter.add(1, {"method": "ProcessRequest", "error": "false"})
```

This enables queries like:
```promql
myservice_requests_total{method="ProcessRequest", error="false"}
```

**Periodic Export:**
Metrics are collected and exported every 10 seconds via `PeriodicExportingMetricReader`. This batches metrics to reduce network overhead.

**Automatic gRPC Metrics:**
The gRPC instrumentation automatically records:
- `rpc.server.duration`: Histogram of RPC durations
- `rpc.server.request.size`: Request message sizes
- `rpc.server.response.size`: Response message sizes

### Logging

**Structured JSON Logging:**
The logging format produces JSON logs that are easily parsed:
```json
{
  "timestamp": "2024-01-15 10:30:00,000",
  "level": "INFO",
  "message": "Processing request: abc123",
  "trace_id": "0af7651916cd43dd8448eb211c80319c",
  "span_id": "b7ad6b7169203331"
}
```

**Trace Context Injection:**
The `TraceContextFilter` automatically injects `trace_id` and `span_id` into every log record. This enables:
- Clicking a trace ID in Grafana to see all related logs
- Searching logs by trace ID to understand a request's journey
- Correlating errors with their distributed trace

**OTLP Log Export:**
The `LoggingHandler` with `OTLPLogExporter` sends logs to the OpenTelemetry Collector via OTLP/gRPC. The collector forwards them to Elasticsearch with trace correlation preserved.

**Log Levels:**
- `logger.info()`: Normal operations
- `logger.error()`: Errors (consider also calling `span.record_exception()`)
- `logger.warning()`: Warnings
- `logger.debug()`: Verbose debugging (typically filtered in production)

**Best Practices:**
- Always log at the start and end of significant operations
- Include relevant context (request IDs, user IDs) in log messages
- Use structured logging (key-value pairs) rather than string interpolation where possible
- Log errors with sufficient context to debug without accessing the trace
