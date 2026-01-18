using Grpc.Core;
using Microsoft.AspNetCore.Server.Kestrel.Core;
using OpenTelemetry;
using OpenTelemetry.Metrics;
using OpenTelemetry.Resources;
using OpenTelemetry.Trace;
using System.Diagnostics;
using System.Diagnostics.Metrics;
using GrpcArchitecture.Proto;

var builder = WebApplication.CreateBuilder(args);

// Configure Kestrel to use HTTP/2 only (required for gRPC over plain HTTP)
var port = int.Parse(Environment.GetEnvironmentVariable("GRPC_PORT") ?? "50054");
builder.WebHost.ConfigureKestrel(options =>
{
    options.ListenAnyIP(port, listenOptions =>
    {
        listenOptions.Protocols = HttpProtocols.Http2;
    });
});

// Configure OpenTelemetry
var serviceName = Environment.GetEnvironmentVariable("OTEL_SERVICE_NAME") ?? "service-d";
var otlpEndpoint = Environment.GetEnvironmentVariable("OTEL_EXPORTER_OTLP_ENDPOINT") ?? "http://localhost:4317";
var errorRateStr = Environment.GetEnvironmentVariable("ERROR_RATE") ?? "0.20";
var errorRate = double.Parse(errorRateStr);

builder.Services.AddOpenTelemetry()
    .ConfigureResource(resource => resource
        .AddService(serviceName: serviceName, serviceVersion: "1.0.0"))
    .WithTracing(tracing => tracing
        .AddAspNetCoreInstrumentation()
        .AddGrpcClientInstrumentation()
        .AddSource(serviceName)
        .AddOtlpExporter(opts => opts.Endpoint = new Uri(otlpEndpoint)))
    .WithMetrics(metrics => metrics
        .AddAspNetCoreInstrumentation()
        .AddMeter(serviceName)
        .AddOtlpExporter(opts => opts.Endpoint = new Uri(otlpEndpoint)));

builder.Services.AddGrpc();
builder.Services.AddSingleton(new ValidationService.ServiceDMetrics(serviceName));
builder.Services.AddSingleton(new ValidationService.ErrorRateConfig(errorRate));

var app = builder.Build();

app.MapGrpcService<ValidationService>();
app.MapGet("/", () => "Service D (C#) - Validation Service");

Console.WriteLine($"[Service D] Starting gRPC server on port {port}");
Console.WriteLine($"[Service D] Error rate configured: {errorRate * 100}%");

app.Run();

public class ValidationService : ServiceD.ServiceDBase
{
    private static readonly ActivitySource ActivitySource = new("service-d");
    private readonly ServiceDMetrics _metrics;
    private readonly double _errorRate;
    private readonly Random _random = new();

    public ValidationService(ServiceDMetrics metrics, ErrorRateConfig errorRateConfig)
    {
        _metrics = metrics;
        _errorRate = errorRateConfig.Value;
    }

    public override async Task<ValidationResponse> ValidateData(
        ValidationRequest request,
        ServerCallContext context)
    {
        using var activity = ActivitySource.StartActivity("ValidateData");
        activity?.SetTag("rpc.system", "grpc");
        activity?.SetTag("rpc.service", "ServiceD");
        activity?.SetTag("rpc.method", "ValidateData");

        var stopwatch = Stopwatch.StartNew();
        Console.WriteLine($"[Service D] ValidateData called - data_id: {request.Data?.Id}");

        // Simulate validation delay (5-10ms)
        var delay = _random.Next(5, 11);
        await Task.Delay(delay);

        // Simulate ~20% error rate
        bool shouldFail = _random.NextDouble() < _errorRate;

        stopwatch.Stop();
        var duration = stopwatch.Elapsed.TotalMilliseconds;

        if (shouldFail)
        {
            _metrics.RecordRequest("ValidateData", "error");
            _metrics.RecordLatency("ValidateData", duration);

            activity?.SetStatus(ActivityStatusCode.Error, "Simulated validation error");
            activity?.SetTag("error", true);

            Console.WriteLine($"[Service D] Simulated error triggered (duration: {duration:F2}ms)");

            throw new RpcException(new Grpc.Core.Status(Grpc.Core.StatusCode.InvalidArgument,
                "Simulated validation error: Data failed validation checks"));
        }

        _metrics.RecordRequest("ValidateData", "ok");
        _metrics.RecordLatency("ValidateData", duration);

        var response = new ValidationResponse
        {
            Status = new GrpcArchitecture.Proto.ResponseStatus
            {
                Success = true,
                Message = "Validation successful"
            },
            IsValid = true
        };

        activity?.SetTag("duration_ms", duration);
        activity?.SetTag("is_valid", true);

        Console.WriteLine($"[Service D] Validation successful (duration: {duration:F2}ms)");

        return response;
    }

    public class ErrorRateConfig
    {
        public double Value { get; }
        public ErrorRateConfig(double value) => Value = value;
    }

    public class ServiceDMetrics
    {
        private readonly Counter<long> _requestCounter;
        private readonly Histogram<double> _latencyHistogram;

        public ServiceDMetrics(string serviceName)
        {
            var meter = new Meter(serviceName, "1.0.0");
            _requestCounter = meter.CreateCounter<long>("service_d_requests_total",
                description: "Total requests to Service D");
            _latencyHistogram = meter.CreateHistogram<double>("service_d_request_duration_ms",
                unit: "ms", description: "Request duration in milliseconds");
        }

        public void RecordRequest(string method, string status)
        {
            _requestCounter.Add(1,
                new KeyValuePair<string, object?>("method", method),
                new KeyValuePair<string, object?>("status", status));
        }

        public void RecordLatency(string method, double durationMs)
        {
            _latencyHistogram.Record(durationMs,
                new KeyValuePair<string, object?>("method", method));
        }
    }
}
