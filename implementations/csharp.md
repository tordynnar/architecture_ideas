# C# Implementation

C# has mature OpenTelemetry support via the .NET SDK with native gRPC integration through ASP.NET Core.

## Key Packages

- `OpenTelemetry` - Core OpenTelemetry API
- `OpenTelemetry.Exporter.OpenTelemetryProtocol` - OTLP exporter for traces, metrics, and logs
- `OpenTelemetry.Instrumentation.GrpcNetClient` - Automatic gRPC client instrumentation
- `OpenTelemetry.Instrumentation.AspNetCore` - Automatic ASP.NET Core instrumentation (includes gRPC server)
- `Grpc.AspNetCore` - gRPC server implementation

## Code Sample

```csharp
// Program.cs
using System.Diagnostics;
using System.Diagnostics.Metrics;
using OpenTelemetry;
using OpenTelemetry.Logs;
using OpenTelemetry.Metrics;
using OpenTelemetry.Resources;
using OpenTelemetry.Trace;
using Grpc.Core;
using Grpc.Net.Client;

var builder = WebApplication.CreateBuilder(args);

// Service identification
var serviceName = "my-csharp-service";
var serviceVersion = "1.0.0";

// ============================================================
// OPENTELEMETRY CONFIGURATION
// ============================================================
builder.Services.AddOpenTelemetry()
    .ConfigureResource(resource => resource.AddService(
        serviceName: serviceName,
        serviceVersion: serviceVersion))

    // ============================================================
    // TRACING SETUP
    // ============================================================
    .WithTracing(tracing => tracing
        // Automatic instrumentation for ASP.NET Core (includes gRPC server)
        .AddAspNetCoreInstrumentation(options =>
        {
            // Enrich spans with additional request data
            options.RecordException = true;
        })
        // Automatic instrumentation for gRPC client
        .AddGrpcClientInstrumentation(options =>
        {
            options.SuppressDownstreamInstrumentation = false;
        })
        // Automatic instrumentation for HttpClient
        .AddHttpClientInstrumentation()
        // Manual instrumentation source
        .AddSource(serviceName)
        // Export to OpenTelemetry Collector
        .AddOtlpExporter(opts =>
        {
            opts.Endpoint = new Uri("http://otel-collector:4317");
        }))

    // ============================================================
    // METRICS SETUP
    // ============================================================
    .WithMetrics(metrics => metrics
        // Automatic ASP.NET Core metrics
        .AddAspNetCoreInstrumentation()
        // Automatic HttpClient metrics
        .AddHttpClientInstrumentation()
        // Runtime metrics (GC, thread pool, etc.)
        .AddRuntimeInstrumentation()
        // Custom metrics source
        .AddMeter(serviceName)
        // Export to OpenTelemetry Collector
        .AddOtlpExporter(opts =>
        {
            opts.Endpoint = new Uri("http://otel-collector:4317");
        }));

// ============================================================
// LOGGING SETUP
// ============================================================
builder.Logging.AddOpenTelemetry(logging =>
{
    logging.SetResourceBuilder(ResourceBuilder.CreateDefault().AddService(serviceName));
    logging.AddOtlpExporter(opts =>
    {
        opts.Endpoint = new Uri("http://otel-collector:4317");
    });
    // Include formatted message in logs
    logging.IncludeFormattedMessage = true;
    // Include logging scopes for contextual data
    logging.IncludeScopes = true;
});

builder.Services.AddGrpc();
builder.Services.AddSingleton<MyServiceImpl>();

var app = builder.Build();
app.MapGrpcService<MyServiceImpl>();
app.Run();

// ============================================================
// SERVICE IMPLEMENTATION
// ============================================================
public class MyServiceImpl : MyService.MyServiceBase
{
    // ActivitySource for manual tracing (Activity = Span in OpenTelemetry)
    private static readonly ActivitySource ActivitySource = new("my-csharp-service");

    // Meter for custom metrics
    private static readonly Meter Meter = new("my-csharp-service", "1.0.0");

    // Custom metrics instruments
    private static readonly Counter<long> RequestCounter = Meter.CreateCounter<long>(
        "myservice.requests.total",
        unit: "{requests}",
        description: "Total number of requests processed");

    private static readonly Histogram<double> RequestDuration = Meter.CreateHistogram<double>(
        "myservice.request.duration",
        unit: "s",
        description: "Request processing duration");

    private static readonly UpDownCounter<long> ActiveRequests = Meter.CreateUpDownCounter<long>(
        "myservice.requests.active",
        unit: "{requests}",
        description: "Number of requests currently being processed");

    private readonly ILogger<MyServiceImpl> _logger;

    public MyServiceImpl(ILogger<MyServiceImpl> logger)
    {
        _logger = logger;
    }

    public override async Task<MyResponse> ProcessRequest(MyRequest request, ServerCallContext context)
    {
        var stopwatch = System.Diagnostics.Stopwatch.StartNew();

        // Track active requests (metric)
        ActiveRequests.Add(1, new KeyValuePair<string, object?>("method", "ProcessRequest"));

        try
        {
            // Contextual logging - trace_id and span_id automatically included by OpenTelemetry
            _logger.LogInformation("Processing request {RequestId}", request.Id);

            // Manual span for specific operation
            using var activity = ActivitySource.StartActivity("process-business-logic");
            activity?.SetTag("request.id", request.Id);

            try
            {
                var result = await DoBusinessLogicAsync(request);

                // Record success metrics
                stopwatch.Stop();
                var duration = stopwatch.Elapsed.TotalSeconds;

                RequestCounter.Add(1,
                    new KeyValuePair<string, object?>("method", "ProcessRequest"),
                    new KeyValuePair<string, object?>("error", false));
                RequestDuration.Record(duration,
                    new KeyValuePair<string, object?>("method", "ProcessRequest"),
                    new KeyValuePair<string, object?>("error", false));

                _logger.LogInformation("Request processed successfully in {Duration:F3}s", duration);
                return result;
            }
            catch (Exception ex)
            {
                // Record error on span
                activity?.SetStatus(ActivityStatusCode.Error, ex.Message);
                activity?.RecordException(ex);

                // Record error metrics
                stopwatch.Stop();
                var duration = stopwatch.Elapsed.TotalSeconds;

                RequestCounter.Add(1,
                    new KeyValuePair<string, object?>("method", "ProcessRequest"),
                    new KeyValuePair<string, object?>("error", true));
                RequestDuration.Record(duration,
                    new KeyValuePair<string, object?>("method", "ProcessRequest"),
                    new KeyValuePair<string, object?>("error", true));

                _logger.LogError(ex, "Business logic failed for request {RequestId}", request.Id);
                throw new RpcException(new Status(StatusCode.Internal, ex.Message));
            }
        }
        finally
        {
            // Always decrement active requests
            ActiveRequests.Add(-1, new KeyValuePair<string, object?>("method", "ProcessRequest"));
        }
    }

    private async Task<MyResponse> DoBusinessLogicAsync(MyRequest request)
    {
        // Another manual span for nested operation
        using var activity = ActivitySource.StartActivity("database-query");
        activity?.SetTag("db.system", "postgresql");
        activity?.SetTag("db.operation", "SELECT");

        _logger.LogInformation("Executing database query");

        // Simulate async database operation
        await Task.Delay(10);

        return new MyResponse { Result = "success" };
    }
}

// ============================================================
// GRPC CLIENT WITH AUTOMATIC TRACING
// ============================================================
public class MyServiceClient
{
    private static readonly ActivitySource ActivitySource = new("my-csharp-service");
    private static readonly Meter Meter = new("my-csharp-service", "1.0.0");

    private static readonly Counter<long> ClientCallCounter = Meter.CreateCounter<long>(
        "myservice.client.calls.total",
        description: "Total outbound gRPC calls");

    private readonly MyService.MyServiceClient _client;
    private readonly ILogger<MyServiceClient> _logger;

    public MyServiceClient(ILogger<MyServiceClient> logger)
    {
        _logger = logger;
        // Channel automatically inherits tracing from GrpcClientInstrumentation
        var channel = GrpcChannel.ForAddress("http://other-service:50051");
        _client = new MyService.MyServiceClient(channel);
    }

    public async Task<MyResponse> CallServiceAsync(string requestId)
    {
        // Manual span for client-side operation
        using var activity = ActivitySource.StartActivity("call-external-service");
        activity?.SetTag("peer.service", "other-service");
        activity?.SetTag("request.id", requestId);

        _logger.LogInformation("Calling external service with request {RequestId}", requestId);

        try
        {
            var request = new MyRequest { Id = requestId };
            // Trace context automatically propagated via GrpcClientInstrumentation
            var result = await _client.ProcessRequestAsync(request);

            ClientCallCounter.Add(1,
                new KeyValuePair<string, object?>("service", "other-service"),
                new KeyValuePair<string, object?>("error", false));

            return result;
        }
        catch (RpcException ex)
        {
            activity?.SetStatus(ActivityStatusCode.Error, ex.Message);
            activity?.RecordException(ex);

            ClientCallCounter.Add(1,
                new KeyValuePair<string, object?>("service", "other-service"),
                new KeyValuePair<string, object?>("error", true));

            _logger.LogError(ex, "Failed to call external service");
            throw;
        }
    }
}
```

## How It Works

### Tracing

**Automatic Instrumentation:**
The `AddAspNetCoreInstrumentation()` automatically creates spans for all incoming HTTP/gRPC requests. For gRPC, this includes:
- `rpc.system`: "grpc"
- `rpc.service`: The gRPC service name
- `rpc.method`: The method being called
- `rpc.grpc.status_code`: The response status code

The `AddGrpcClientInstrumentation()` instruments outbound gRPC calls and automatically propagates trace context via metadata headers.

**Manual Spans with Activity:**
In .NET, OpenTelemetry uses the `System.Diagnostics.Activity` class as the span representation. Use `ActivitySource.StartActivity()` to create spans:

```csharp
using var activity = ActivitySource.StartActivity("operation-name");
activity?.SetTag("key", "value");
// ... operation code ...
// span automatically ends when disposed
```

The `using` statement ensures the span is properly ended even if an exception occurs.

**Activity vs Span:**
- `ActivitySource` = `Tracer` in OpenTelemetry terminology
- `Activity` = `Span` in OpenTelemetry terminology
- This is .NET's native implementation that OpenTelemetry builds upon

**Span Attributes:**
Use `SetTag()` to add attributes to spans. These appear in Jaeger and help with filtering and analysis:
```csharp
activity?.SetTag("request.id", requestId);
activity?.SetTag("db.system", "postgresql");
```

**Exception Recording:**
Use `RecordException()` to attach exception details to a span:
```csharp
activity?.SetStatus(ActivityStatusCode.Error, ex.Message);
activity?.RecordException(ex);
```

### Metrics

**Automatic Metrics:**
The instrumentation packages automatically record:
- `http.server.request.duration` - Request duration histogram
- `http.server.active_requests` - Currently active requests
- `kestrel.active_connections` - Active HTTP connections
- Runtime metrics (when `AddRuntimeInstrumentation()` is used):
  - `process.runtime.dotnet.gc.collections.count`
  - `process.runtime.dotnet.thread_pool.thread.count`
  - `process.runtime.dotnet.gc.heap.size`

**Custom Metrics Instruments:**
- **Counter** (`CreateCounter<T>`): Monotonically increasing values
- **Histogram** (`CreateHistogram<T>`): Distribution of values with automatic bucketing
- **UpDownCounter** (`CreateUpDownCounter<T>`): Values that can increase or decrease
- **ObservableGauge** (`CreateObservableGauge<T>`): For callback-based values

**Metric Tags:**
Add dimensions using `KeyValuePair`:
```csharp
RequestCounter.Add(1,
    new KeyValuePair<string, object?>("method", "ProcessRequest"),
    new KeyValuePair<string, object?>("error", false));
```

These become labels in Prometheus, enabling queries like:
```promql
myservice_requests_total{method="ProcessRequest", error="false"}
```

**Metric Export:**
Metrics are exported to the OpenTelemetry Collector via OTLP. The default export interval is 60 seconds, configurable via:
```csharp
.AddOtlpExporter(opts =>
{
    opts.Endpoint = new Uri("http://otel-collector:4317");
    opts.ExportProcessorType = ExportProcessorType.Batch;
    opts.BatchExportProcessorOptions = new BatchExportProcessorOptions<Activity>
    {
        ScheduledDelayMilliseconds = 10000 // 10 seconds
    };
})
```

### Logging

**Automatic Trace Correlation:**
When using `ILogger` with OpenTelemetry logging configured, each log entry automatically includes:
- `TraceId`: The current trace ID
- `SpanId`: The current span ID
- `TraceFlags`: Sampling flags

This correlation is automatic - no additional code needed.

**Structured Logging:**
Use structured logging with message templates:
```csharp
_logger.LogInformation("Processing request {RequestId}", request.Id);
```

This produces structured logs where `RequestId` is a separate field, enabling:
- Searching by specific request IDs
- Aggregating logs by field values
- Better log analytics

**Logging Scopes:**
Use scopes to add contextual data to multiple log entries:
```csharp
using (_logger.BeginScope(new Dictionary<string, object>
{
    ["UserId"] = userId,
    ["CorrelationId"] = correlationId
}))
{
    _logger.LogInformation("Processing started");
    // All logs in this scope include UserId and CorrelationId
    _logger.LogInformation("Processing completed");
}
```

**Log Levels:**
- `LogDebug()`: Verbose debugging (filtered in production)
- `LogInformation()`: Normal operations
- `LogWarning()`: Warnings
- `LogError()`: Errors (include exception parameter for stack traces)
- `LogCritical()`: Critical failures

**OTLP Log Export:**
Logs are exported to the OpenTelemetry Collector, then forwarded to Elasticsearch. The `BatchLogRecordExportProcessor` batches logs for efficiency.

### Dependency Injection Integration

**Service Registration:**
Register telemetry-enabled services in DI:
```csharp
builder.Services.AddSingleton<MyServiceImpl>();
builder.Services.AddSingleton<MyServiceClient>();
```

**ILogger Injection:**
`ILogger<T>` is automatically available for injection and includes trace correlation:
```csharp
public MyServiceImpl(ILogger<MyServiceImpl> logger)
{
    _logger = logger;
}
```

### Best Practices

1. **Use `using` for Activities**: Ensures spans are properly closed
2. **Check for null**: `activity?.SetTag()` - Activity may be null if sampling excludes it
3. **Use message templates**: `LogInformation("Request {Id}", id)` not string interpolation
4. **Add meaningful tags**: Include request IDs, user IDs, operation types
5. **Record exceptions**: Always call `RecordException()` in catch blocks
6. **Use appropriate log levels**: Debug for development, Info/Error for production
