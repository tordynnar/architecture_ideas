# C Implementation

C requires fully manual instrumentation as there is no official OpenTelemetry C SDK. This implementation uses a lightweight OTLP/HTTP approach with libcurl for exporting telemetry data.

## Key Libraries

- `libcurl` - HTTP client for sending telemetry to the collector
- `jansson` - JSON serialization for OTLP payloads
- `grpc-c` - gRPC server/client implementation
- `pthread` - Threading for background metric export

## Code Sample

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <curl/curl.h>
#include <jansson.h>
#include <grpc/grpc.h>

#define OTEL_COLLECTOR_TRACES "http://otel-collector:4318/v1/traces"
#define OTEL_COLLECTOR_METRICS "http://otel-collector:4318/v1/metrics"
#define OTEL_COLLECTOR_LOGS "http://otel-collector:4318/v1/logs"
#define SERVICE_NAME "my-c-service"
#define SERVICE_VERSION "1.0.0"

// ============================================================
// TRACING STRUCTURES
// ============================================================
typedef struct {
    char trace_id[33];
    char span_id[17];
    char parent_span_id[17];
    long start_time_ns;
    const char *name;
} span_t;

typedef struct {
    char trace_id[33];
    char span_id[17];
} trace_context_t;

// ============================================================
// METRICS STRUCTURES
// ============================================================
typedef struct {
    long request_count;
    long error_count;
    long active_requests;
    double total_duration;
    long duration_count;
    pthread_mutex_t lock;
} metrics_t;

static metrics_t g_metrics = {0, 0, 0, 0.0, 0, PTHREAD_MUTEX_INITIALIZER};

// ============================================================
// UTILITY FUNCTIONS
// ============================================================
static void generate_hex_id(char *buf, size_t len) {
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < len - 1; i++) {
        buf[i] = hex[rand() % 16];
    }
    buf[len - 1] = '\0';
}

static long get_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return ts.tv_sec * 1000000000L + ts.tv_nsec;
}

// ============================================================
// TELEMETRY INITIALIZATION
// ============================================================
void otel_init(void) {
    curl_global_init(CURL_GLOBAL_ALL);
    srand(time(NULL));
}

void otel_shutdown(void) {
    curl_global_cleanup();
}

// ============================================================
// TRACING IMPLEMENTATION
// ============================================================
trace_context_t* otel_create_trace_context(void) {
    trace_context_t *ctx = malloc(sizeof(trace_context_t));
    generate_hex_id(ctx->trace_id, 33);
    generate_hex_id(ctx->span_id, 17);
    return ctx;
}

span_t* otel_start_span(const trace_context_t *parent, const char *name) {
    span_t *span = malloc(sizeof(span_t));
    strcpy(span->trace_id, parent->trace_id);
    strcpy(span->parent_span_id, parent->span_id);
    generate_hex_id(span->span_id, 17);
    span->start_time_ns = get_time_ns();
    span->name = name;
    return span;
}

static void otel_export_span(const span_t *span, int is_error, json_t *attributes) {
    CURL *curl = curl_easy_init();
    if (!curl) return;

    long end_time_ns = get_time_ns();

    // Build OTLP JSON payload
    json_t *root = json_object();
    json_t *resource_spans = json_array();
    json_t *resource_span = json_object();

    // Resource attributes
    json_t *resource = json_object();
    json_t *res_attrs = json_array();

    json_t *svc_name = json_object();
    json_object_set_new(svc_name, "key", json_string("service.name"));
    json_object_set_new(svc_name, "value", json_pack("{s:s}", "stringValue", SERVICE_NAME));
    json_array_append_new(res_attrs, svc_name);

    json_t *svc_version = json_object();
    json_object_set_new(svc_version, "key", json_string("service.version"));
    json_object_set_new(svc_version, "value", json_pack("{s:s}", "stringValue", SERVICE_VERSION));
    json_array_append_new(res_attrs, svc_version);

    json_object_set_new(resource, "attributes", res_attrs);
    json_object_set_new(resource_span, "resource", resource);

    // Span data
    json_t *scope_spans = json_array();
    json_t *scope_span = json_object();
    json_t *spans = json_array();
    json_t *span_obj = json_object();

    json_object_set_new(span_obj, "traceId", json_string(span->trace_id));
    json_object_set_new(span_obj, "spanId", json_string(span->span_id));
    if (span->parent_span_id[0] != '\0') {
        json_object_set_new(span_obj, "parentSpanId", json_string(span->parent_span_id));
    }
    json_object_set_new(span_obj, "name", json_string(span->name));
    json_object_set_new(span_obj, "startTimeUnixNano", json_integer(span->start_time_ns));
    json_object_set_new(span_obj, "endTimeUnixNano", json_integer(end_time_ns));
    json_object_set_new(span_obj, "kind", json_integer(2)); // SERVER

    // Status
    json_t *status = json_object();
    json_object_set_new(status, "code", json_integer(is_error ? 2 : 1)); // ERROR or OK
    json_object_set_new(span_obj, "status", status);

    if (attributes) {
        json_object_set(span_obj, "attributes", attributes);
    }

    json_array_append_new(spans, span_obj);
    json_object_set_new(scope_span, "spans", spans);
    json_array_append_new(scope_spans, scope_span);
    json_object_set_new(resource_span, "scopeSpans", scope_spans);
    json_array_append_new(resource_spans, resource_span);
    json_object_set_new(root, "resourceSpans", resource_spans);

    char *json_str = json_dumps(root, JSON_COMPACT);

    // Send to collector
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, OTEL_COLLECTOR_TRACES);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_str);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
    curl_easy_perform(curl);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    free(json_str);
    json_decref(root);
}

void otel_end_span(span_t *span, int is_error) {
    otel_export_span(span, is_error, NULL);
    free(span);
}

// ============================================================
// METRICS IMPLEMENTATION
// ============================================================
void metrics_increment_requests(const char *method, int is_error) {
    pthread_mutex_lock(&g_metrics.lock);
    g_metrics.request_count++;
    if (is_error) {
        g_metrics.error_count++;
    }
    pthread_mutex_unlock(&g_metrics.lock);
}

void metrics_record_duration(double duration_seconds) {
    pthread_mutex_lock(&g_metrics.lock);
    g_metrics.total_duration += duration_seconds;
    g_metrics.duration_count++;
    pthread_mutex_unlock(&g_metrics.lock);
}

void metrics_set_active_requests(int delta) {
    pthread_mutex_lock(&g_metrics.lock);
    g_metrics.active_requests += delta;
    pthread_mutex_unlock(&g_metrics.lock);
}

static void export_metrics(void) {
    CURL *curl = curl_easy_init();
    if (!curl) return;

    pthread_mutex_lock(&g_metrics.lock);
    long requests = g_metrics.request_count;
    long errors = g_metrics.error_count;
    long active = g_metrics.active_requests;
    double avg_duration = g_metrics.duration_count > 0
        ? g_metrics.total_duration / g_metrics.duration_count
        : 0.0;
    pthread_mutex_unlock(&g_metrics.lock);

    long timestamp_ns = get_time_ns();

    // Build OTLP metrics payload
    json_t *root = json_object();
    json_t *resource_metrics = json_array();
    json_t *resource_metric = json_object();

    // Resource
    json_t *resource = json_object();
    json_t *res_attrs = json_array();
    json_t *svc_name = json_object();
    json_object_set_new(svc_name, "key", json_string("service.name"));
    json_object_set_new(svc_name, "value", json_pack("{s:s}", "stringValue", SERVICE_NAME));
    json_array_append_new(res_attrs, svc_name);
    json_object_set_new(resource, "attributes", res_attrs);
    json_object_set_new(resource_metric, "resource", resource);

    // Scope metrics
    json_t *scope_metrics = json_array();
    json_t *scope_metric = json_object();
    json_t *metrics = json_array();

    // Request counter
    json_t *req_metric = json_object();
    json_object_set_new(req_metric, "name", json_string("myservice.requests.total"));
    json_object_set_new(req_metric, "description", json_string("Total requests"));
    json_object_set_new(req_metric, "unit", json_string("{requests}"));
    json_t *sum = json_object();
    json_t *data_points = json_array();
    json_t *dp = json_object();
    json_object_set_new(dp, "asInt", json_integer(requests));
    json_object_set_new(dp, "timeUnixNano", json_integer(timestamp_ns));
    json_array_append_new(data_points, dp);
    json_object_set_new(sum, "dataPoints", data_points);
    json_object_set_new(sum, "isMonotonic", json_true());
    json_object_set_new(req_metric, "sum", sum);
    json_array_append_new(metrics, req_metric);

    // Active requests gauge
    json_t *active_metric = json_object();
    json_object_set_new(active_metric, "name", json_string("myservice.requests.active"));
    json_t *gauge = json_object();
    json_t *gauge_dps = json_array();
    json_t *gauge_dp = json_object();
    json_object_set_new(gauge_dp, "asInt", json_integer(active));
    json_object_set_new(gauge_dp, "timeUnixNano", json_integer(timestamp_ns));
    json_array_append_new(gauge_dps, gauge_dp);
    json_object_set_new(gauge, "dataPoints", gauge_dps);
    json_object_set_new(active_metric, "gauge", gauge);
    json_array_append_new(metrics, active_metric);

    json_object_set_new(scope_metric, "metrics", metrics);
    json_array_append_new(scope_metrics, scope_metric);
    json_object_set_new(resource_metric, "scopeMetrics", scope_metrics);
    json_array_append_new(resource_metrics, resource_metric);
    json_object_set_new(root, "resourceMetrics", resource_metrics);

    char *json_str = json_dumps(root, JSON_COMPACT);

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, OTEL_COLLECTOR_METRICS);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_str);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
    curl_easy_perform(curl);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    free(json_str);
    json_decref(root);
}

// Background thread for periodic metric export
static void* metrics_export_thread(void *arg) {
    while (1) {
        sleep(10); // Export every 10 seconds
        export_metrics();
    }
    return NULL;
}

void start_metrics_exporter(void) {
    pthread_t thread;
    pthread_create(&thread, NULL, metrics_export_thread, NULL);
    pthread_detach(thread);
}

// ============================================================
// LOGGING IMPLEMENTATION
// ============================================================
void otel_log(const trace_context_t *ctx, const char *level, const char *message) {
    time_t now = time(NULL);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));

    // Print to stdout (for local debugging / docker logs)
    printf("{\"timestamp\":\"%s\",\"level\":\"%s\",\"message\":\"%s\","
           "\"trace_id\":\"%s\",\"span_id\":\"%s\",\"service\":\"%s\"}\n",
           timestamp, level, message,
           ctx ? ctx->trace_id : "0",
           ctx ? ctx->span_id : "0",
           SERVICE_NAME);
    fflush(stdout);

    // Export to OTLP (optional, can be batched for production)
    CURL *curl = curl_easy_init();
    if (!curl) return;

    long timestamp_ns = get_time_ns();

    json_t *root = json_object();
    json_t *resource_logs = json_array();
    json_t *resource_log = json_object();

    // Resource
    json_t *resource = json_object();
    json_t *res_attrs = json_array();
    json_t *svc_name = json_object();
    json_object_set_new(svc_name, "key", json_string("service.name"));
    json_object_set_new(svc_name, "value", json_pack("{s:s}", "stringValue", SERVICE_NAME));
    json_array_append_new(res_attrs, svc_name);
    json_object_set_new(resource, "attributes", res_attrs);
    json_object_set_new(resource_log, "resource", resource);

    // Scope logs
    json_t *scope_logs = json_array();
    json_t *scope_log = json_object();
    json_t *log_records = json_array();
    json_t *log_record = json_object();

    json_object_set_new(log_record, "timeUnixNano", json_integer(timestamp_ns));
    json_object_set_new(log_record, "severityText", json_string(level));
    json_object_set_new(log_record, "body", json_pack("{s:s}", "stringValue", message));

    if (ctx) {
        json_object_set_new(log_record, "traceId", json_string(ctx->trace_id));
        json_object_set_new(log_record, "spanId", json_string(ctx->span_id));
    }

    json_array_append_new(log_records, log_record);
    json_object_set_new(scope_log, "logRecords", log_records);
    json_array_append_new(scope_logs, scope_log);
    json_object_set_new(resource_log, "scopeLogs", scope_logs);
    json_array_append_new(resource_logs, resource_log);
    json_object_set_new(root, "resourceLogs", resource_logs);

    char *json_str = json_dumps(root, JSON_COMPACT);

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, OTEL_COLLECTOR_LOGS);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_str);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
    curl_easy_perform(curl);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    free(json_str);
    json_decref(root);
}

// ============================================================
// GRPC SERVICE HANDLER
// ============================================================
void process_request_handler(grpc_call *call, const char *request_id, trace_context_t *parent_ctx) {
    struct timespec start_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);

    // Track active requests
    metrics_set_active_requests(1);

    // Create server span
    span_t *server_span = otel_start_span(parent_ctx, "grpc.server.ProcessRequest");
    otel_log(parent_ctx, "INFO", "Processing request");

    int is_error = 0;

    // Manual span for business logic
    span_t *biz_span = otel_start_span(parent_ctx, "process-business-logic");
    otel_log(parent_ctx, "INFO", "Executing business logic");

    // Nested span for database query
    span_t *db_span = otel_start_span(parent_ctx, "database-query");
    otel_log(parent_ctx, "INFO", "Executing database query");

    // Simulate database operation
    usleep(10000); // 10ms

    otel_end_span(db_span, 0);
    otel_end_span(biz_span, 0);

    // Calculate duration
    struct timespec end_time;
    clock_gettime(CLOCK_MONOTONIC, &end_time);
    double duration = (end_time.tv_sec - start_time.tv_sec) +
                      (end_time.tv_nsec - start_time.tv_nsec) / 1e9;

    // Record metrics
    metrics_increment_requests("ProcessRequest", is_error);
    metrics_record_duration(duration);
    metrics_set_active_requests(-1);

    otel_log(parent_ctx, "INFO", "Request processed successfully");
    otel_end_span(server_span, is_error);
}

// gRPC server interceptor for automatic tracing
void grpc_server_tracing_interceptor(grpc_call *call,
    void (*handler)(grpc_call*, const char*, trace_context_t*)) {

    // Extract trace context from metadata or create new
    trace_context_t *ctx = otel_create_trace_context();

    // TODO: Extract traceparent header from grpc metadata
    // grpc_metadata *metadata = grpc_call_get_metadata(call);
    // parse_traceparent(metadata, ctx);

    handler(call, "request-123", ctx);
    free(ctx);
}

int main(int argc, char **argv) {
    otel_init();
    start_metrics_exporter();

    printf("Starting gRPC server...\n");

    // Start gRPC server with tracing interceptor...
    // grpc_server_start(...);

    otel_shutdown();
    return 0;
}
```

## How It Works

### Tracing

**Manual Trace Context:**
Since there's no OpenTelemetry C SDK, trace context is managed manually using simple structures:
- `trace_context_t`: Holds the current trace_id and span_id
- `span_t`: Represents an active span with timing information

**Span Lifecycle:**
1. `otel_start_span()`: Creates a new span, generating a new span_id and recording the start time
2. Business logic executes with the span context available
3. `otel_end_span()`: Records the end time and exports the span via OTLP/HTTP

**OTLP/HTTP Export:**
Spans are exported directly to the OpenTelemetry Collector via HTTP POST to `/v1/traces`. The payload follows the OTLP JSON format. Using HTTP instead of gRPC simplifies the implementation (no need for protobuf compilation).

**Trace Context Propagation:**
For distributed tracing, extract the `traceparent` header from incoming gRPC metadata:
```
traceparent: 00-{trace_id}-{parent_span_id}-01
```
Parse this header to populate the `trace_context_t` structure, ensuring child spans are properly linked.

**Span Attributes:**
Add attributes by building a JSON array and passing it to `otel_export_span()`:
```c
json_t *attrs = json_array();
json_t *attr = json_object();
json_object_set_new(attr, "key", json_string("request.id"));
json_object_set_new(attr, "value", json_pack("{s:s}", "stringValue", request_id));
json_array_append_new(attrs, attr);
otel_export_span(span, 0, attrs);
```

### Metrics

**In-Memory Aggregation:**
Metrics are aggregated in memory using a thread-safe structure (`metrics_t`). The mutex ensures correct values when accessed from multiple threads.

**Metric Types Implemented:**
- **Counter** (`request_count`, `error_count`): Monotonically increasing values
- **Gauge** (`active_requests`): Current value that can go up or down
- **Summary** (`total_duration`, `duration_count`): Used to calculate average duration

**Background Export:**
A detached thread runs every 10 seconds to export metrics to the collector. This avoids blocking request handling and batches metrics for efficiency.

**OTLP Metrics Format:**
Metrics are exported as OTLP JSON to `/v1/metrics`. Each metric includes:
- Name and description
- Data points with timestamps
- Metric type (sum, gauge, histogram)

**Thread Safety:**
All metric operations use `pthread_mutex_lock/unlock` to prevent race conditions:
```c
pthread_mutex_lock(&g_metrics.lock);
g_metrics.request_count++;
pthread_mutex_unlock(&g_metrics.lock);
```

### Logging

**Dual Output:**
Logs are written to both:
1. **stdout**: For local debugging and `docker logs` access
2. **OTLP/HTTP**: For centralized log aggregation in Elasticsearch

**Structured JSON Format:**
Each log entry is JSON-formatted with trace correlation:
```json
{
  "timestamp": "2024-01-15T10:30:00Z",
  "level": "INFO",
  "message": "Processing request",
  "trace_id": "abc123...",
  "span_id": "def456...",
  "service": "my-c-service"
}
```

**Trace Correlation:**
Every log includes `trace_id` and `span_id` from the current trace context. This enables:
- Searching logs by trace ID in Grafana
- Jumping from a log entry to its trace in Jaeger
- Understanding the full context of an error

**Log Levels:**
Use standard severity levels:
- `"INFO"`: Normal operations
- `"ERROR"`: Errors (set `is_error=1` when ending the span)
- `"DEBUG"`: Verbose debugging (consider compiling out in production)
- `"WARN"`: Warnings

### Production Considerations

**Batching:**
The current implementation exports each span and log immediately. For production, implement batching:
```c
static span_t *pending_spans[MAX_BATCH_SIZE];
static int pending_count = 0;

void otel_end_span(span_t *span, int is_error) {
    pthread_mutex_lock(&batch_lock);
    pending_spans[pending_count++] = span;
    if (pending_count >= MAX_BATCH_SIZE) {
        flush_spans();
    }
    pthread_mutex_unlock(&batch_lock);
}
```

**Error Handling:**
Add retry logic for failed exports:
```c
int retries = 3;
while (retries-- > 0) {
    CURLcode res = curl_easy_perform(curl);
    if (res == CURLE_OK) break;
    usleep(100000); // 100ms backoff
}
```

**Memory Management:**
Ensure all allocated memory is freed, especially in error paths. Consider using a memory pool for frequently allocated structures like spans.

**Async Export:**
For high-throughput services, export telemetry in a separate thread to avoid blocking request handling:
```c
void queue_span_for_export(span_t *span) {
    // Add to thread-safe queue
    // Background thread processes queue
}
```
