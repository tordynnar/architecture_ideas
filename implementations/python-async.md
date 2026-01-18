# Python Implementation (Asynchronous)

Python async implementation uses `grpclib` with manual instrumentation since automatic instrumentation is not available for this library.

## Key Packages

- `opentelemetry-api` - Core OpenTelemetry API
- `opentelemetry-sdk` - SDK implementation
- `opentelemetry-exporter-otlp-proto-grpc` - OTLP exporter
- `grpclib` - Async gRPC implementation

## Code Sample

```python
import asyncio
import logging
import time

from grpclib.server import Server
from grpclib.client import Channel

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
from opentelemetry.trace import get_current_span
from opentelemetry.trace.propagation.tracecontext import TraceContextTextMapPropagator

from myservice_grpc import MyServiceBase, MyServiceStub
from myservice_pb2 import MyRequest, MyResponse

# ============================================================
# LOGGING SETUP
# ============================================================
class TraceContextFilter(logging.Filter):
    def filter(self, record):
        span = get_current_span()
        ctx = span.get_span_context()
        record.trace_id = format(ctx.trace_id, '032x') if ctx.is_valid else "0"
        record.span_id = format(ctx.span_id, '016x') if ctx.is_valid else "0"
        return True

logging.basicConfig(
    format='{"timestamp":"%(asctime)s","level":"%(levelname)s","message":"%(message)s","trace_id":"%(trace_id)s","span_id":"%(span_id)s"}',
    level=logging.INFO
)
logger = logging.getLogger(__name__)
logger.addFilter(TraceContextFilter())


def init_telemetry():
    resource = Resource.create({
        SERVICE_NAME: "my-python-async-service",
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

    # ============================================================
    # METRICS SETUP
    # ============================================================
    metric_reader = PeriodicExportingMetricReader(
        OTLPMetricExporter(endpoint="otel-collector:4317", insecure=True),
        export_interval_millis=10000,
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
    handler = LoggingHandler(level=logging.INFO, logger_provider=logger_provider)
    logging.getLogger().addHandler(handler)

    return trace.get_tracer(__name__), metrics.get_meter(__name__)


tracer, meter = init_telemetry()
propagator = TraceContextTextMapPropagator()

# Custom metrics instruments
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


class MyServiceImpl(MyServiceBase):
    async def ProcessRequest(self, stream) -> None:
        start_time = time.time()
        request = await stream.recv_message()

        # Track active requests (metric)
        active_requests.add(1, {"method": "ProcessRequest"})

        try:
            # Extract trace context from metadata for distributed tracing
            # This enables correlation with upstream services
            metadata = dict(stream.metadata)
            ctx = propagator.extract(carrier=metadata)

            # Create span for the RPC method (manual since grpclib has no auto-instrumentation)
            with tracer.start_as_current_span(
                "grpc.server.ProcessRequest",
                context=ctx,
                kind=trace.SpanKind.SERVER,
            ) as span:
                span.set_attribute("rpc.system", "grpc")
                span.set_attribute("rpc.method", "ProcessRequest")
                span.set_attribute("rpc.service", "MyService")

                # Contextual logging - trace context now available
                logger.info(f"Processing request: {request.id}")

                # Manual span for specific operation
                with tracer.start_as_current_span("process-business-logic") as child_span:
                    child_span.set_attribute("request.id", request.id)

                    try:
                        result = await self._do_business_logic(request)

                        # Record success metrics
                        duration = time.time() - start_time
                        request_counter.add(1, {"method": "ProcessRequest", "error": "false"})
                        request_duration.record(duration, {"method": "ProcessRequest", "error": "false"})

                        logger.info(f"Request processed successfully in {duration:.3f}s")
                        await stream.send_message(result)

                    except Exception as e:
                        child_span.record_exception(e)
                        child_span.set_status(trace.StatusCode.ERROR, str(e))
                        span.set_status(trace.StatusCode.ERROR, str(e))

                        # Record error metrics
                        duration = time.time() - start_time
                        request_counter.add(1, {"method": "ProcessRequest", "error": "true"})
                        request_duration.record(duration, {"method": "ProcessRequest", "error": "true"})

                        logger.error(f"Business logic failed: {e}")
                        raise

        finally:
            active_requests.add(-1, {"method": "ProcessRequest"})

    async def _do_business_logic(self, request: MyRequest) -> MyResponse:
        # Another manual span for nested operation
        with tracer.start_as_current_span("database-query") as span:
            span.set_attribute("db.system", "postgresql")
            span.set_attribute("db.operation", "SELECT")

            logger.info("Executing database query")

            # Simulate async database operation
            await asyncio.sleep(0.01)

            return MyResponse(result="success")


# Async gRPC client with manual trace propagation
async def call_service(addr: str, port: int, request: MyRequest) -> MyResponse:
    with tracer.start_as_current_span(
        "grpc.client.ProcessRequest",
        kind=trace.SpanKind.CLIENT,
    ) as span:
        span.set_attribute("rpc.system", "grpc")
        span.set_attribute("rpc.method", "ProcessRequest")
        span.set_attribute("net.peer.name", addr)
        span.set_attribute("net.peer.port", port)

        # Inject trace context into metadata for propagation
        metadata = {}
        propagator.inject(carrier=metadata)

        logger.info(f"Calling service at {addr}:{port}")

        async with Channel(addr, port) as channel:
            stub = MyServiceStub(channel)
            return await stub.ProcessRequest(request, metadata=list(metadata.items()))


async def serve():
    server = Server([MyServiceImpl()])
    await server.start('0.0.0.0', 50051)
    logger.info("Starting async gRPC server on port 50051")
    await server.wait_closed()


if __name__ == '__main__':
    asyncio.run(serve())
```

## How It Works

### Tracing

**Manual Instrumentation (Required for grpclib):**
Unlike `grpcio`, `grpclib` does not have automatic OpenTelemetry instrumentation. You must manually create spans for each RPC method. The example shows how to:
1. Extract trace context from incoming metadata
2. Create a server span with appropriate attributes
3. Propagate context to child spans

**Trace Context Extraction:**
```python
metadata = dict(stream.metadata)
ctx = propagator.extract(carrier=metadata)
```
This extracts the W3C Trace Context (`traceparent` header) from incoming gRPC metadata, allowing the span to be a child of the upstream service's span.

**Span Kinds:**
- `SpanKind.SERVER` for incoming RPC handlers
- `SpanKind.CLIENT` for outgoing RPC calls

These span kinds affect how traces are visualized in Jaeger (server spans show as receiving, client spans as sending).

**Trace Context Injection (Client):**
```python
metadata = {}
propagator.inject(carrier=metadata)
```
This injects the current trace context into outgoing metadata, enabling the downstream service to continue the trace.

**Manual Spans for Business Logic:**
Use nested `start_as_current_span()` context managers to create child spans. Each span automatically inherits the parent context.

### Metrics

**Custom Metrics Instruments:**
The same metric types as the sync implementation:
- **Counter**: Request counts, error counts
- **Histogram**: Request durations, response sizes
- **UpDownCounter**: Active connections, queue depth

**Async-Safe Metrics:**
OpenTelemetry metrics instruments are thread-safe and async-safe. You can safely call `counter.add()` or `histogram.record()` from any async task.

**Metric Attributes:**
```python
request_counter.add(1, {"method": "ProcessRequest", "error": "false"})
```
Attributes become Prometheus labels, enabling filtering and grouping in Grafana dashboards.

**Timing Async Operations:**
Use `time.time()` before and after the operation to measure duration. For more precise timing of specific async operations, you can use `time.perf_counter()`:
```python
start = time.perf_counter()
await some_async_operation()
duration = time.perf_counter() - start
```

### Logging

**Trace Context in Async Code:**
The `TraceContextFilter` works correctly in async code because OpenTelemetry's context management is based on `contextvars`, which properly propagates context across `await` boundaries.

**Structured JSON Logging:**
Each log entry includes:
```json
{
  "timestamp": "2024-01-15 10:30:00,000",
  "level": "INFO",
  "message": "Processing request: abc123",
  "trace_id": "0af7651916cd43dd8448eb211c80319c",
  "span_id": "b7ad6b7169203331"
}
```

**OTLP Log Export:**
Logs are sent to the OpenTelemetry Collector via OTLP, then forwarded to Elasticsearch. The `BatchLogRecordProcessor` batches logs to reduce network overhead.

**Logging Async Operations:**
Log at key points in async operations:
- When starting an async operation
- When awaiting external services
- When the operation completes (success or failure)

This helps trace the flow of async requests through the system.

### Key Differences from Sync Implementation

| Aspect | Sync (grpcio) | Async (grpclib) |
|--------|---------------|-----------------|
| Auto-instrumentation | Available | Not available |
| Span creation | Automatic for RPC | Manual required |
| Context propagation | Automatic | Manual inject/extract |
| Event loop | Thread pool | asyncio |
| Metrics | Same API | Same API |
| Logging | Same API | Same API (contextvars-based) |

### Best Practices for Async

1. **Always extract trace context** at the start of each RPC handler
2. **Use context managers** (`with tracer.start_as_current_span()`) to ensure spans are properly closed
3. **Pass context explicitly** when spawning background tasks:
   ```python
   ctx = trace.get_current_span().get_span_context()
   asyncio.create_task(background_task(ctx))
   ```
4. **Log before and after** long-running async operations
5. **Use `try/finally`** to ensure metrics are recorded even on exceptions

## Quick Reference: gRPC Server

Minimal async gRPC server method implementation with metrics, tracing, and logging:

```python
class MyServiceImpl(MyServiceBase):
    async def ProcessRequest(self, stream) -> None:
        start_time = time.time()
        request = await stream.recv_message()

        # Metrics: track active requests
        active_requests.add(1, {"method": "ProcessRequest"})

        try:
            # Tracing: extract context and create server span (manual for grpclib)
            metadata = dict(stream.metadata)
            ctx = propagator.extract(carrier=metadata)

            with tracer.start_as_current_span(
                "grpc.server.ProcessRequest",
                context=ctx,
                kind=trace.SpanKind.SERVER,
            ) as span:
                span.set_attribute("rpc.system", "grpc")
                span.set_attribute("rpc.method", "ProcessRequest")
                span.set_attribute("request.id", request.id)

                # Logging: trace context now available
                logger.info(f"Processing request: {request.id}")

                # Tracing: manual span for business logic
                with tracer.start_as_current_span("process-business-logic") as child_span:
                    child_span.set_attribute("request.id", request.id)

                    logger.info("Executing business logic")
                    result = await self._do_business_logic(request)

                    # Metrics: record success
                    duration = time.time() - start_time
                    request_counter.add(1, {"method": "ProcessRequest", "error": "false"})
                    request_duration.record(duration, {"method": "ProcessRequest", "error": "false"})

                    logger.info(f"Request completed in {duration:.3f}s")
                    await stream.send_message(result)

        except Exception as e:
            # Tracing: record error
            span = trace.get_current_span()
            span.record_exception(e)
            span.set_status(trace.StatusCode.ERROR, str(e))

            # Metrics: record error
            duration = time.time() - start_time
            request_counter.add(1, {"method": "ProcessRequest", "error": "true"})
            request_duration.record(duration, {"method": "ProcessRequest", "error": "true"})

            logger.error(f"Request failed: {e}")
            raise

        finally:
            # Metrics: always decrement active requests
            active_requests.add(-1, {"method": "ProcessRequest"})
```

## Quick Reference: gRPC Client Call

Minimal async gRPC client call with metrics, tracing, and logging:

```python
async def call_service(addr: str, port: int, request_id: str) -> MyResponse:
    start_time = time.time()

    # Logging: trace context automatically included
    logger.info(f"Calling external service at {addr}:{port}")

    # Tracing: create client span
    with tracer.start_as_current_span(
        "grpc.client.ProcessRequest",
        kind=trace.SpanKind.CLIENT,
    ) as span:
        span.set_attribute("rpc.system", "grpc")
        span.set_attribute("rpc.service", "MyService")
        span.set_attribute("net.peer.name", addr)
        span.set_attribute("net.peer.port", port)
        span.set_attribute("request.id", request_id)

        # Tracing: inject context for propagation (manual for grpclib)
        metadata = {}
        propagator.inject(carrier=metadata)

        try:
            async with Channel(addr, port) as channel:
                stub = MyServiceStub(channel)
                request = MyRequest(id=request_id)
                result = await stub.ProcessRequest(request, metadata=list(metadata.items()))

                # Metrics: record success
                duration = time.time() - start_time
                client_call_counter.add(1, {"service": "other-service", "error": "false"})
                client_call_duration.record(duration, {"service": "other-service", "error": "false"})

                logger.info(f"Service call completed in {duration:.3f}s")
                return result

        except Exception as e:
            # Tracing: record error
            span.record_exception(e)
            span.set_status(trace.StatusCode.ERROR, str(e))

            # Metrics: record error
            duration = time.time() - start_time
            client_call_counter.add(1, {"service": "other-service", "error": "true"})
            client_call_duration.record(duration, {"service": "other-service", "error": "true"})

            logger.error(f"Service call failed: {e}")
            raise
