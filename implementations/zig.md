# Zig Implementation

Zig requires manual instrumentation as there is no official OpenTelemetry SDK. This implementation uses Zig's standard library HTTP client for OTLP/HTTP export and demonstrates idiomatic Zig patterns for telemetry.

## Key Libraries

- Zig standard library (`std.http`, `std.json`, `std.time`)
- `zig-grpc` or C interop with `grpc-c` for gRPC support
- No external dependencies required for telemetry (uses std lib)

## Code Sample

```zig
const std = @import("std");
const Allocator = std.mem.Allocator;
const Thread = std.Thread;
const Mutex = Thread.Mutex;

// ============================================================
// CONFIGURATION
// ============================================================
const config = struct {
    const otel_collector_traces = "http://otel-collector:4318/v1/traces";
    const otel_collector_metrics = "http://otel-collector:4318/v1/metrics";
    const otel_collector_logs = "http://otel-collector:4318/v1/logs";
    const service_name = "my-zig-service";
    const service_version = "1.0.0";
    const export_interval_ms = 10_000;
};

// ============================================================
// TRACE CONTEXT
// ============================================================
pub const TraceId = [16]u8;
pub const SpanId = [8]u8;

pub const TraceContext = struct {
    trace_id: TraceId,
    span_id: SpanId,

    pub fn generate() TraceContext {
        var ctx: TraceContext = undefined;
        std.crypto.random.bytes(&ctx.trace_id);
        std.crypto.random.bytes(&ctx.span_id);
        return ctx;
    }

    pub fn traceIdHex(self: *const TraceContext) [32]u8 {
        return std.fmt.bytesToHex(self.trace_id, .lower);
    }

    pub fn spanIdHex(self: *const TraceContext) [16]u8 {
        return std.fmt.bytesToHex(self.span_id, .lower);
    }

    /// Parse W3C traceparent header: 00-{trace_id}-{span_id}-{flags}
    pub fn fromTraceparent(header: []const u8) ?TraceContext {
        if (header.len < 55) return null;
        if (!std.mem.startsWith(u8, header, "00-")) return null;

        var ctx: TraceContext = undefined;
        _ = std.fmt.hexToBytes(&ctx.trace_id, header[3..35]) catch return null;
        _ = std.fmt.hexToBytes(&ctx.span_id, header[36..52]) catch return null;
        return ctx;
    }

    pub fn toTraceparent(self: *const TraceContext) [55]u8 {
        var buf: [55]u8 = undefined;
        _ = std.fmt.bufPrint(&buf, "00-{s}-{s}-01", .{
            self.traceIdHex(),
            self.spanIdHex(),
        }) catch unreachable;
        return buf;
    }
};

// ============================================================
// SPAN
// ============================================================
pub const SpanKind = enum(u8) {
    internal = 1,
    server = 2,
    client = 3,
    producer = 4,
    consumer = 5,
};

pub const SpanStatus = enum(u8) {
    unset = 0,
    ok = 1,
    error = 2,
};

pub const Span = struct {
    name: []const u8,
    trace_id: TraceId,
    span_id: SpanId,
    parent_span_id: ?SpanId,
    start_time_ns: i128,
    end_time_ns: ?i128,
    kind: SpanKind,
    status: SpanStatus,
    status_message: ?[]const u8,
    attributes: std.StringHashMap([]const u8),
    allocator: Allocator,

    pub fn init(
        allocator: Allocator,
        name: []const u8,
        parent_ctx: ?*const TraceContext,
        kind: SpanKind,
    ) Span {
        var span = Span{
            .name = name,
            .trace_id = undefined,
            .span_id = undefined,
            .parent_span_id = null,
            .start_time_ns = std.time.nanoTimestamp(),
            .end_time_ns = null,
            .kind = kind,
            .status = .unset,
            .status_message = null,
            .attributes = std.StringHashMap([]const u8).init(allocator),
            .allocator = allocator,
        };

        if (parent_ctx) |ctx| {
            span.trace_id = ctx.trace_id;
            span.parent_span_id = ctx.span_id;
        } else {
            std.crypto.random.bytes(&span.trace_id);
        }
        std.crypto.random.bytes(&span.span_id);

        return span;
    }

    pub fn deinit(self: *Span) void {
        self.attributes.deinit();
    }

    pub fn setAttribute(self: *Span, key: []const u8, value: []const u8) void {
        self.attributes.put(key, value) catch {};
    }

    pub fn setStatus(self: *Span, status: SpanStatus, message: ?[]const u8) void {
        self.status = status;
        self.status_message = message;
    }

    pub fn end(self: *Span) void {
        self.end_time_ns = std.time.nanoTimestamp();
    }

    pub fn getContext(self: *const Span) TraceContext {
        return .{
            .trace_id = self.trace_id,
            .span_id = self.span_id,
        };
    }
};

// ============================================================
// METRICS
// ============================================================
pub const Metrics = struct {
    request_count: std.atomic.Value(u64),
    error_count: std.atomic.Value(u64),
    active_requests: std.atomic.Value(i64),
    total_duration_ns: std.atomic.Value(u64),
    duration_count: std.atomic.Value(u64),

    pub fn init() Metrics {
        return .{
            .request_count = std.atomic.Value(u64).init(0),
            .error_count = std.atomic.Value(u64).init(0),
            .active_requests = std.atomic.Value(i64).init(0),
            .total_duration_ns = std.atomic.Value(u64).init(0),
            .duration_count = std.atomic.Value(u64).init(0),
        };
    }

    pub fn incrementRequests(self: *Metrics, is_error: bool) void {
        _ = self.request_count.fetchAdd(1, .monotonic);
        if (is_error) {
            _ = self.error_count.fetchAdd(1, .monotonic);
        }
    }

    pub fn recordDuration(self: *Metrics, duration_ns: u64) void {
        _ = self.total_duration_ns.fetchAdd(duration_ns, .monotonic);
        _ = self.duration_count.fetchAdd(1, .monotonic);
    }

    pub fn adjustActiveRequests(self: *Metrics, delta: i64) void {
        _ = self.active_requests.fetchAdd(delta, .monotonic);
    }

    pub fn getSnapshot(self: *const Metrics) MetricsSnapshot {
        return .{
            .request_count = self.request_count.load(.monotonic),
            .error_count = self.error_count.load(.monotonic),
            .active_requests = self.active_requests.load(.monotonic),
            .total_duration_ns = self.total_duration_ns.load(.monotonic),
            .duration_count = self.duration_count.load(.monotonic),
        };
    }
};

pub const MetricsSnapshot = struct {
    request_count: u64,
    error_count: u64,
    active_requests: i64,
    total_duration_ns: u64,
    duration_count: u64,

    pub fn avgDurationMs(self: MetricsSnapshot) f64 {
        if (self.duration_count == 0) return 0;
        const avg_ns: f64 = @as(f64, @floatFromInt(self.total_duration_ns)) /
            @as(f64, @floatFromInt(self.duration_count));
        return avg_ns / 1_000_000.0;
    }
};

// ============================================================
// TELEMETRY EXPORTER
// ============================================================
pub const Telemetry = struct {
    allocator: Allocator,
    metrics: Metrics,
    export_thread: ?Thread,
    shutdown: std.atomic.Value(bool),

    pub fn init(allocator: Allocator) Telemetry {
        return .{
            .allocator = allocator,
            .metrics = Metrics.init(),
            .export_thread = null,
            .shutdown = std.atomic.Value(bool).init(false),
        };
    }

    pub fn start(self: *Telemetry) !void {
        self.export_thread = try Thread.spawn(.{}, exportLoop, .{self});
    }

    pub fn shutdown(self: *Telemetry) void {
        self.shutdown.store(true, .release);
        if (self.export_thread) |thread| {
            thread.join();
        }
    }

    fn exportLoop(self: *Telemetry) void {
        while (!self.shutdown.load(.acquire)) {
            std.time.sleep(config.export_interval_ms * std.time.ns_per_ms);
            self.exportMetrics() catch |err| {
                std.log.err("Failed to export metrics: {}", .{err});
            };
        }
    }

    // --------------------------------------------------------
    // SPAN EXPORT
    // --------------------------------------------------------
    pub fn exportSpan(self: *Telemetry, span: *const Span) !void {
        var json_buf = std.ArrayList(u8).init(self.allocator);
        defer json_buf.deinit();

        var writer = json_buf.writer();

        try writer.writeAll(
            \\{"resourceSpans":[{"resource":{"attributes":[
            \\{"key":"service.name","value":{"stringValue":"
        );
        try writer.writeAll(config.service_name);
        try writer.writeAll(
            \\"}},{"key":"service.version","value":{"stringValue":"
        );
        try writer.writeAll(config.service_version);
        try writer.writeAll(
            \\"}}]},"scopeSpans":[{"spans":[
        );

        // Span data
        try writer.writeAll("{\"traceId\":\"");
        try writer.writeAll(&std.fmt.bytesToHex(span.trace_id, .lower));
        try writer.writeAll("\",\"spanId\":\"");
        try writer.writeAll(&std.fmt.bytesToHex(span.span_id, .lower));
        try writer.writeAll("\"");

        if (span.parent_span_id) |parent_id| {
            try writer.writeAll(",\"parentSpanId\":\"");
            try writer.writeAll(&std.fmt.bytesToHex(parent_id, .lower));
            try writer.writeAll("\"");
        }

        try writer.writeAll(",\"name\":\"");
        try writer.writeAll(span.name);
        try writer.print("\",\"startTimeUnixNano\":{d},\"endTimeUnixNano\":{d}", .{
            span.start_time_ns,
            span.end_time_ns orelse std.time.nanoTimestamp(),
        });
        try writer.print(",\"kind\":{d}", .{@intFromEnum(span.kind)});

        // Status
        try writer.print(",\"status\":{{\"code\":{d}", .{@intFromEnum(span.status)});
        if (span.status_message) |msg| {
            try writer.writeAll(",\"message\":\"");
            try writer.writeAll(msg);
            try writer.writeAll("\"");
        }
        try writer.writeAll("}");

        // Attributes
        try writer.writeAll(",\"attributes\":[");
        var first = true;
        var iter = span.attributes.iterator();
        while (iter.next()) |entry| {
            if (!first) try writer.writeAll(",");
            first = false;
            try writer.writeAll("{\"key\":\"");
            try writer.writeAll(entry.key_ptr.*);
            try writer.writeAll("\",\"value\":{\"stringValue\":\"");
            try writer.writeAll(entry.value_ptr.*);
            try writer.writeAll("\"}}");
        }
        try writer.writeAll("]");

        try writer.writeAll("}]}]}]}");

        try self.sendToCollector(config.otel_collector_traces, json_buf.items);
    }

    // --------------------------------------------------------
    // METRICS EXPORT
    // --------------------------------------------------------
    fn exportMetrics(self: *Telemetry) !void {
        const snapshot = self.metrics.getSnapshot();
        const timestamp_ns = std.time.nanoTimestamp();

        var json_buf = std.ArrayList(u8).init(self.allocator);
        defer json_buf.deinit();

        var writer = json_buf.writer();

        try writer.writeAll(
            \\{"resourceMetrics":[{"resource":{"attributes":[
            \\{"key":"service.name","value":{"stringValue":"
        );
        try writer.writeAll(config.service_name);
        try writer.writeAll(
            \\"}}]},"scopeMetrics":[{"metrics":[
        );

        // Request counter
        try writer.writeAll(
            \\{"name":"myservice.requests.total","description":"Total requests",
            \\"unit":"{requests}","sum":{"dataPoints":[{"asInt":
        );
        try writer.print("{d},\"timeUnixNano\":{d}", .{ snapshot.request_count, timestamp_ns });
        try writer.writeAll("}],\"isMonotonic\":true}}");

        // Error counter
        try writer.writeAll(
            \\,{"name":"myservice.errors.total","description":"Total errors",
            \\"unit":"{errors}","sum":{"dataPoints":[{"asInt":
        );
        try writer.print("{d},\"timeUnixNano\":{d}", .{ snapshot.error_count, timestamp_ns });
        try writer.writeAll("}],\"isMonotonic\":true}}");

        // Active requests gauge
        try writer.writeAll(
            \\,{"name":"myservice.requests.active","description":"Active requests",
            \\"unit":"{requests}","gauge":{"dataPoints":[{"asInt":
        );
        try writer.print("{d},\"timeUnixNano\":{d}", .{ snapshot.active_requests, timestamp_ns });
        try writer.writeAll("}]}}");

        try writer.writeAll("]}]}]}");

        try self.sendToCollector(config.otel_collector_metrics, json_buf.items);
    }

    // --------------------------------------------------------
    // LOG EXPORT
    // --------------------------------------------------------
    pub fn exportLog(
        self: *Telemetry,
        severity: LogSeverity,
        message: []const u8,
        trace_ctx: ?*const TraceContext,
    ) !void {
        const timestamp_ns = std.time.nanoTimestamp();

        var json_buf = std.ArrayList(u8).init(self.allocator);
        defer json_buf.deinit();

        var writer = json_buf.writer();

        try writer.writeAll(
            \\{"resourceLogs":[{"resource":{"attributes":[
            \\{"key":"service.name","value":{"stringValue":"
        );
        try writer.writeAll(config.service_name);
        try writer.writeAll(
            \\"}}]},"scopeLogs":[{"logRecords":[
        );

        try writer.print("{{\"timeUnixNano\":{d},\"severityNumber\":{d}", .{
            timestamp_ns,
            @intFromEnum(severity),
        });
        try writer.writeAll(",\"severityText\":\"");
        try writer.writeAll(severity.text());
        try writer.writeAll("\",\"body\":{\"stringValue\":\"");
        try writer.writeAll(message);
        try writer.writeAll("\"}");

        if (trace_ctx) |ctx| {
            try writer.writeAll(",\"traceId\":\"");
            try writer.writeAll(&ctx.traceIdHex());
            try writer.writeAll("\",\"spanId\":\"");
            try writer.writeAll(&ctx.spanIdHex());
            try writer.writeAll("\"");
        }

        try writer.writeAll("}]}]}]}");

        try self.sendToCollector(config.otel_collector_logs, json_buf.items);

        // Also print to stderr for local debugging
        const stderr = std.io.getStdErr().writer();
        stderr.print(
            "{{\"timestamp\":{d},\"level\":\"{s}\",\"message\":\"{s}\"",
            .{ timestamp_ns, severity.text(), message },
        ) catch {};
        if (trace_ctx) |ctx| {
            stderr.print(",\"trace_id\":\"{s}\",\"span_id\":\"{s}\"", .{
                ctx.traceIdHex(),
                ctx.spanIdHex(),
            }) catch {};
        }
        stderr.writeAll("}}\n") catch {};
    }

    // --------------------------------------------------------
    // HTTP TRANSPORT
    // --------------------------------------------------------
    fn sendToCollector(self: *Telemetry, url: []const u8, body: []const u8) !void {
        var client = std.http.Client{ .allocator = self.allocator };
        defer client.deinit();

        const uri = try std.Uri.parse(url);

        var header_buf: [4096]u8 = undefined;
        var req = try client.open(.POST, uri, .{
            .server_header_buffer = &header_buf,
            .extra_headers = &.{
                .{ .name = "Content-Type", .value = "application/json" },
            },
        });
        defer req.deinit();

        req.transfer_encoding = .{ .content_length = body.len };
        try req.send();
        try req.writeAll(body);
        try req.finish();
        try req.wait();

        if (req.status != .ok) {
            std.log.warn("Collector returned status: {}", .{req.status});
        }
    }
};

pub const LogSeverity = enum(u8) {
    trace = 1,
    debug = 5,
    info = 9,
    warn = 13,
    err = 17,
    fatal = 21,

    pub fn text(self: LogSeverity) []const u8 {
        return switch (self) {
            .trace => "TRACE",
            .debug => "DEBUG",
            .info => "INFO",
            .warn => "WARN",
            .err => "ERROR",
            .fatal => "FATAL",
        };
    }
};

// ============================================================
// GRPC SERVICE HANDLER (using C interop or zig-grpc)
// ============================================================
pub const ServiceHandler = struct {
    telemetry: *Telemetry,
    allocator: Allocator,

    pub fn init(allocator: Allocator, telemetry: *Telemetry) ServiceHandler {
        return .{
            .telemetry = telemetry,
            .allocator = allocator,
        };
    }

    pub fn processRequest(
        self: *ServiceHandler,
        request_id: []const u8,
        parent_traceparent: ?[]const u8,
    ) ![]const u8 {
        const start_time = std.time.nanoTimestamp();

        // Track active requests
        self.telemetry.metrics.adjustActiveRequests(1);
        defer self.telemetry.metrics.adjustActiveRequests(-1);

        // Extract or create trace context
        const parent_ctx: ?TraceContext = if (parent_traceparent) |tp|
            TraceContext.fromTraceparent(tp)
        else
            null;

        // Create server span
        var server_span = Span.init(
            self.allocator,
            "grpc.server.ProcessRequest",
            if (parent_ctx) |*ctx| ctx else null,
            .server,
        );
        defer server_span.deinit();

        server_span.setAttribute("rpc.system", "grpc");
        server_span.setAttribute("rpc.service", "MyService");
        server_span.setAttribute("rpc.method", "ProcessRequest");
        server_span.setAttribute("request.id", request_id);

        const span_ctx = server_span.getContext();
        var is_error = false;

        try self.telemetry.exportLog(.info, "Processing request", &span_ctx);

        // Business logic span
        var biz_span = Span.init(self.allocator, "process-business-logic", &span_ctx, .internal);
        defer biz_span.deinit();
        biz_span.setAttribute("request.id", request_id);

        const biz_ctx = biz_span.getContext();
        try self.telemetry.exportLog(.info, "Executing business logic", &biz_ctx);

        // Database query span
        var db_span = Span.init(self.allocator, "database-query", &biz_ctx, .internal);
        defer db_span.deinit();
        db_span.setAttribute("db.system", "postgresql");
        db_span.setAttribute("db.operation", "SELECT");

        try self.telemetry.exportLog(.info, "Executing database query", &db_span.getContext());

        // Simulate database operation
        std.time.sleep(10 * std.time.ns_per_ms);

        db_span.setStatus(.ok, null);
        db_span.end();
        try self.telemetry.exportSpan(&db_span);

        biz_span.setStatus(.ok, null);
        biz_span.end();
        try self.telemetry.exportSpan(&biz_span);

        server_span.setStatus(.ok, null);
        server_span.end();
        try self.telemetry.exportSpan(&server_span);

        // Record metrics
        const end_time = std.time.nanoTimestamp();
        const duration_ns: u64 = @intCast(end_time - start_time);
        self.telemetry.metrics.incrementRequests(is_error);
        self.telemetry.metrics.recordDuration(duration_ns);

        try self.telemetry.exportLog(.info, "Request processed successfully", &span_ctx);

        return "success";
    }

    /// Generate traceparent header for outbound calls
    pub fn makeClientCall(
        self: *ServiceHandler,
        target_service: []const u8,
        request_id: []const u8,
        parent_ctx: ?*const TraceContext,
    ) ![]const u8 {
        var client_span = Span.init(
            self.allocator,
            "grpc.client.ProcessRequest",
            parent_ctx,
            .client,
        );
        defer client_span.deinit();

        client_span.setAttribute("rpc.system", "grpc");
        client_span.setAttribute("rpc.method", "ProcessRequest");
        client_span.setAttribute("peer.service", target_service);

        const span_ctx = client_span.getContext();
        try self.telemetry.exportLog(.info, "Calling external service", &span_ctx);

        // Get traceparent for propagation
        const traceparent = span_ctx.toTraceparent();

        // TODO: Make actual gRPC call with traceparent header
        // const result = try grpc_client.call(target_service, request_id, &traceparent);

        std.time.sleep(5 * std.time.ns_per_ms); // Simulate call

        client_span.setStatus(.ok, null);
        client_span.end();
        try self.telemetry.exportSpan(&client_span);

        return "client-success";
    }
};

// ============================================================
// MAIN
// ============================================================
pub fn main() !void {
    var gpa = std.heap.GeneralPurposeAllocator(.{}){};
    defer _ = gpa.deinit();
    const allocator = gpa.allocator();

    var telemetry = Telemetry.init(allocator);
    try telemetry.start();
    defer telemetry.shutdown();

    var handler = ServiceHandler.init(allocator, &telemetry);

    std.log.info("Starting gRPC server on port 50051", .{});

    // Simulate processing requests
    var i: usize = 0;
    while (i < 10) : (i += 1) {
        var request_id_buf: [16]u8 = undefined;
        _ = try std.fmt.bufPrint(&request_id_buf, "req-{d:0>4}", .{i});

        _ = handler.processRequest(&request_id_buf, null) catch |err| {
            std.log.err("Request failed: {}", .{err});
        };

        std.time.sleep(100 * std.time.ns_per_ms);
    }

    std.log.info("Server shutting down", .{});
}

// ============================================================
// TESTS
// ============================================================
test "TraceContext generation" {
    const ctx = TraceContext.generate();
    const hex_trace = ctx.traceIdHex();
    const hex_span = ctx.spanIdHex();

    try std.testing.expect(hex_trace.len == 32);
    try std.testing.expect(hex_span.len == 16);
}

test "TraceContext traceparent parsing" {
    const traceparent = "00-0af7651916cd43dd8448eb211c80319c-b7ad6b7169203331-01";
    const ctx = TraceContext.fromTraceparent(traceparent);

    try std.testing.expect(ctx != null);
    const hex_trace = ctx.?.traceIdHex();
    try std.testing.expectEqualStrings("0af7651916cd43dd8448eb211c80319c", &hex_trace);
}

test "Metrics atomic operations" {
    var metrics = Metrics.init();

    metrics.incrementRequests(false);
    metrics.incrementRequests(true);
    metrics.adjustActiveRequests(5);
    metrics.adjustActiveRequests(-2);
    metrics.recordDuration(1_000_000);

    const snapshot = metrics.getSnapshot();
    try std.testing.expectEqual(@as(u64, 2), snapshot.request_count);
    try std.testing.expectEqual(@as(u64, 1), snapshot.error_count);
    try std.testing.expectEqual(@as(i64, 3), snapshot.active_requests);
}
```

## How It Works

### Tracing

**Manual Trace Context:**
Zig doesn't have an OpenTelemetry SDK, so trace context is managed manually using simple structures:
- `TraceContext`: Holds trace_id (16 bytes) and span_id (8 bytes)
- `Span`: Represents an active span with timing, attributes, and parent relationship

**Span Lifecycle:**
```zig
var span = Span.init(allocator, "operation-name", parent_ctx, .server);
defer span.deinit();

span.setAttribute("key", "value");
// ... operation code ...
span.setStatus(.ok, null);
span.end();
try telemetry.exportSpan(&span);
```

The `defer` pattern ensures spans are cleaned up even on error paths.

**W3C Trace Context Propagation:**
```zig
// Parse incoming traceparent header
const parent_ctx = TraceContext.fromTraceparent(header);

// Generate outgoing traceparent header
const traceparent = span_ctx.toTraceparent();
// Result: "00-{trace_id}-{span_id}-01"
```

This enables distributed tracing across service boundaries using the standard W3C format.

**Span Kinds:**
```zig
pub const SpanKind = enum(u8) {
    internal = 1,  // Internal operation
    server = 2,    // Incoming RPC
    client = 3,    // Outgoing RPC
    producer = 4,  // Message producer
    consumer = 5,  // Message consumer
};
```

**OTLP/HTTP Export:**
Spans are exported directly to the OpenTelemetry Collector via HTTP POST to `/v1/traces`. The payload follows the OTLP JSON format, constructed manually using Zig's standard library.

### Metrics

**Lock-Free Atomic Counters:**
Metrics use Zig's `std.atomic.Value` for thread-safe, lock-free operations:
```zig
pub const Metrics = struct {
    request_count: std.atomic.Value(u64),
    error_count: std.atomic.Value(u64),
    active_requests: std.atomic.Value(i64),
    // ...

    pub fn incrementRequests(self: *Metrics, is_error: bool) void {
        _ = self.request_count.fetchAdd(1, .monotonic);
        if (is_error) {
            _ = self.error_count.fetchAdd(1, .monotonic);
        }
    }
};
```

**Metric Types Implemented:**
- **Counter** (`request_count`, `error_count`): Monotonically increasing, uses `fetchAdd`
- **Gauge** (`active_requests`): Can increase or decrease, uses `fetchAdd` with signed delta
- **Summary** (`total_duration_ns`, `duration_count`): Used to calculate averages

**Background Export Thread:**
```zig
pub fn start(self: *Telemetry) !void {
    self.export_thread = try Thread.spawn(.{}, exportLoop, .{self});
}

fn exportLoop(self: *Telemetry) void {
    while (!self.shutdown.load(.acquire)) {
        std.time.sleep(config.export_interval_ms * std.time.ns_per_ms);
        self.exportMetrics() catch |err| {
            std.log.err("Failed to export metrics: {}", .{err});
        };
    }
}
```

The export thread runs independently, collecting and exporting metrics every 10 seconds without blocking request handling.

**Atomic Memory Ordering:**
- `.monotonic`: Used for counters where only the final value matters
- `.acquire`/`.release`: Used for shutdown flag to ensure proper synchronization

### Logging

**Structured Log Export:**
Logs are exported via OTLP/HTTP to `/v1/logs` with full trace correlation:
```zig
pub fn exportLog(
    self: *Telemetry,
    severity: LogSeverity,
    message: []const u8,
    trace_ctx: ?*const TraceContext,
) !void {
    // Includes trace_id and span_id when available
}
```

**Dual Output:**
Logs are written to both:
1. **OTLP/HTTP**: For centralized aggregation in Elasticsearch
2. **stderr**: For local debugging and `docker logs` access

**Log Severity Levels:**
```zig
pub const LogSeverity = enum(u8) {
    trace = 1,   // Verbose tracing
    debug = 5,   // Debug information
    info = 9,    // Normal operations
    warn = 13,   // Warnings
    err = 17,    // Errors
    fatal = 21,  // Fatal errors
};
```

These values follow the OpenTelemetry severity number specification.

**Trace Correlation:**
Every log includes `trace_id` and `span_id` from the current context:
```json
{
  "timestamp": 1705312200000000000,
  "level": "INFO",
  "message": "Processing request",
  "trace_id": "0af7651916cd43dd8448eb211c80319c",
  "span_id": "b7ad6b7169203331"
}
```

### Build Configuration

**build.zig:**
```zig
const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    const exe = b.addExecutable(.{
        .name = "my-zig-service",
        .root_source_file = b.path("src/main.zig"),
        .target = target,
        .optimize = optimize,
    });

    // Link against C libraries if using grpc-c
    // exe.linkSystemLibrary("grpc");
    // exe.linkLibC();

    b.installArtifact(exe);

    // Add test step
    const unit_tests = b.addTest(.{
        .root_source_file = b.path("src/main.zig"),
        .target = target,
        .optimize = optimize,
    });

    const run_unit_tests = b.addRunArtifact(unit_tests);
    const test_step = b.step("test", "Run unit tests");
    test_step.dependOn(&run_unit_tests.step);
}
```

### gRPC Integration Options

**Option 1: C Interop with grpc-c**
```zig
const c = @cImport({
    @cInclude("grpc/grpc.h");
});

// Use C gRPC API with Zig wrappers
```

**Option 2: Community zig-grpc Library**
```zig
const grpc = @import("zig-grpc");

// Use native Zig gRPC implementation
```

**Option 3: HTTP/2 with Protocol Buffers**
Use Zig's HTTP client with a protobuf library for a pure-Zig solution.

### Production Considerations

**Error Handling:**
Zig's explicit error handling ensures all failure paths are handled:
```zig
handler.processRequest(&request_id_buf, null) catch |err| {
    std.log.err("Request failed: {}", .{err});
    // Record error metrics, update span status
};
```

**Memory Management:**
Use `defer` for cleanup and prefer arena allocators for request-scoped allocations:
```zig
var arena = std.heap.ArenaAllocator.init(std.heap.page_allocator);
defer arena.deinit();
const request_allocator = arena.allocator();
// All request allocations freed at once
```

**Batching:**
For production, implement span batching to reduce HTTP overhead:
```zig
const SpanBatch = struct {
    spans: std.ArrayList(Span),
    mutex: Mutex,

    pub fn add(self: *SpanBatch, span: Span) void {
        self.mutex.lock();
        defer self.mutex.unlock();
        self.spans.append(span) catch {};
        if (self.spans.items.len >= 100) {
            self.flush();
        }
    }
};
```

**Comptime Configuration:**
Use Zig's comptime features for zero-cost configuration:
```zig
const config = if (@import("builtin").mode == .Debug)
    .{ .export_interval_ms = 1_000 }  // Fast export in debug
else
    .{ .export_interval_ms = 10_000 }; // Normal export in release
```
