/*
 * Service F - Pure C gRPC Implementation
 *
 * This is a pure C implementation using:
 * - gRPC C core library for the gRPC server
 * - protobuf-c for protocol buffer serialization
 * - Manual OTLP exporters for traces, logs, and metrics
 */

/* Enable POSIX features for strdup, usleep, etc. */
#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <stdint.h>

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/support/log.h>
#include <grpc/support/alloc.h>
#include <grpc/byte_buffer.h>
#include <grpc/byte_buffer_reader.h>

#include "services.pb-c.h"
#include "common.pb-c.h"
#include "otlp_exporter.h"
#include "otlp_log_exporter.h"
#include "otlp_metrics_exporter.h"

/* Server state */
static grpc_server *g_server = NULL;
static grpc_completion_queue *g_cq = NULL;
static int g_shutdown = 0;
static otlp_exporter_t *g_trace_exporter = NULL;
static otlp_log_exporter_t *g_log_exporter = NULL;
static otlp_metrics_exporter_t *g_metrics_exporter = NULL;
static const char *g_service_name = "service-f";

/* Metrics state */
static pthread_mutex_t g_metrics_mutex = PTHREAD_MUTEX_INITIALIZER;
static uint64_t g_request_count = 0;
static double g_total_duration_ms = 0;
static pthread_t g_metrics_thread;

/* Log with OTLP export */
static void log_otlp(log_severity_t severity, const char *trace_id, const char *span_id,
                     const char *message) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    uint64_t timestamp_nanos = (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;

    /* Also print to stdout for debugging */
    const char *level_str;
    switch (severity) {
        case LOG_SEVERITY_DEBUG: level_str = "DEBUG"; break;
        case LOG_SEVERITY_INFO: level_str = "INFO"; break;
        case LOG_SEVERITY_WARN: level_str = "WARN"; break;
        case LOG_SEVERITY_ERROR: level_str = "ERROR"; break;
        case LOG_SEVERITY_FATAL: level_str = "FATAL"; break;
        default: level_str = "INFO"; break;
    }
    printf("[%s] %s | trace_id=%s span_id=%s\n", level_str, message,
           trace_id ? trace_id : "", span_id ? span_id : "");
    fflush(stdout);

    /* Export to OTLP if exporter is available */
    if (g_log_exporter) {
        otlp_log_record_t record = {0};
        record.trace_id = trace_id;
        record.span_id = span_id;
        record.severity = severity;
        record.body = message;
        record.timestamp_nanos = timestamp_nanos;

        /* Add service attribute */
        log_attribute_t attrs[1];
        attrs[0].key = "service.name";
        attrs[0].string_value = g_service_name;
        record.attributes = attrs;
        record.attribute_count = 1;

        otlp_export_log(g_log_exporter, &record);
    }
}

/* Record request metrics */
static void record_request_metrics(double duration_ms) {
    pthread_mutex_lock(&g_metrics_mutex);
    g_request_count++;
    g_total_duration_ms += duration_ms;
    pthread_mutex_unlock(&g_metrics_mutex);
}

/* Background thread to periodically export metrics */
static void* metrics_export_thread(void *arg) {
    (void)arg;

    while (!g_shutdown) {
        sleep(10); /* Export every 10 seconds */

        if (g_shutdown || !g_metrics_exporter) break;

        /* Get current timestamp */
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        uint64_t timestamp_nanos = (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;

        /* Get and reset metrics atomically */
        pthread_mutex_lock(&g_metrics_mutex);
        uint64_t request_count = g_request_count;
        double total_duration = g_total_duration_ms;
        pthread_mutex_unlock(&g_metrics_mutex);

        if (request_count == 0) continue;

        /* Create counter data point */
        metric_data_point_t counter_dp = {0};
        counter_dp.timestamp_nanos = timestamp_nanos;
        counter_dp.int_value = (int64_t)request_count;
        counter_dp.is_double = 0;

        /* Create gauge data point for average duration */
        metric_data_point_t gauge_dp = {0};
        gauge_dp.timestamp_nanos = timestamp_nanos;
        gauge_dp.double_value = request_count > 0 ? total_duration / request_count : 0;
        gauge_dp.is_double = 1;

        /* Create metrics */
        otlp_metric_t metrics[2];

        metrics[0].name = "grpcarch_service_f_requests_total";
        metrics[0].description = "Total number of requests";
        metrics[0].unit = "1";
        metrics[0].type = METRIC_TYPE_COUNTER;
        metrics[0].data_points = &counter_dp;
        metrics[0].data_point_count = 1;
        metrics[0].histogram_points = NULL;
        metrics[0].histogram_point_count = 0;

        metrics[1].name = "grpcarch_service_f_request_duration_ms";
        metrics[1].description = "Average request duration in milliseconds";
        metrics[1].unit = "ms";
        metrics[1].type = METRIC_TYPE_GAUGE;
        metrics[1].data_points = &gauge_dp;
        metrics[1].data_point_count = 1;
        metrics[1].histogram_points = NULL;
        metrics[1].histogram_point_count = 0;

        /* Export metrics */
        otlp_export_metrics(g_metrics_exporter, metrics, 2);
    }

    return NULL;
}

/* Request context for async handling */
typedef struct {
    grpc_call *call;
    grpc_metadata_array request_metadata;
    grpc_byte_buffer *request_payload;
    grpc_call_details call_details;
    int request_id;
} call_context_t;

/* Generate a random trace ID (16 bytes as hex string) */
static void generate_trace_id(char *out, size_t len) {
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < len - 1 && i < 32; i++) {
        out[i] = hex[rand() % 16];
    }
    out[len - 1] = '\0';
}

/* Generate a random span ID (8 bytes as hex string) */
static void generate_span_id(char *out, size_t len) {
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < len - 1 && i < 16; i++) {
        out[i] = hex[rand() % 16];
    }
    out[len - 1] = '\0';
}

/* Get current time in nanoseconds */
static uint64_t get_time_nanos(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/* Extract trace context from metadata */
static void extract_trace_context(grpc_metadata_array *metadata,
                                   char *trace_id, size_t trace_id_len,
                                   char *parent_span_id, size_t parent_span_len) {
    trace_id[0] = '\0';
    parent_span_id[0] = '\0';

    for (size_t i = 0; i < metadata->count; i++) {
        grpc_metadata *md = &metadata->metadata[i];
        const char *key = grpc_slice_to_c_string(md->key);

        if (strcmp(key, "traceparent") == 0) {
            /* W3C trace context format: 00-traceid-spanid-flags */
            char *value = grpc_slice_to_c_string(md->value);
            if (strlen(value) >= 55) {
                /* Extract trace_id (32 chars starting at position 3) */
                strncpy(trace_id, value + 3, 32);
                trace_id[32] = '\0';
                /* Extract parent span_id (16 chars starting at position 36) */
                strncpy(parent_span_id, value + 36, 16);
                parent_span_id[16] = '\0';
            }
            gpr_free(value);
        }
        gpr_free((void*)key);
    }

    /* Generate new trace ID if not provided */
    if (trace_id[0] == '\0') {
        generate_trace_id(trace_id, trace_id_len);
    }
}

/* Simulate random delay (3-8ms) */
static void simulate_db_delay(void) {
    int delay_ms = 3 + (rand() % 6);
    usleep(delay_ms * 1000);
}

/* Handle FetchLegacyData RPC */
static void handle_fetch_legacy_data(call_context_t *ctx) {
    uint64_t start_time = get_time_nanos();
    char trace_id[64] = {0};
    char parent_span_id[32] = {0};
    char span_id[32] = {0};

    /* Extract trace context from incoming metadata */
    extract_trace_context(&ctx->request_metadata, trace_id, sizeof(trace_id),
                          parent_span_id, sizeof(parent_span_id));

    /* Generate span ID for this operation */
    generate_span_id(span_id, sizeof(span_id));

    /* Deserialize request */
    Grpcarch__LegacyDataRequest *request = NULL;
    if (ctx->request_payload != NULL) {
        grpc_byte_buffer_reader reader;
        grpc_byte_buffer_reader_init(&reader, ctx->request_payload);
        grpc_slice slice = grpc_byte_buffer_reader_readall(&reader);

        request = grpcarch__legacy_data_request__unpack(
            NULL,
            GRPC_SLICE_LENGTH(slice),
            GRPC_SLICE_START_PTR(slice)
        );

        grpc_slice_unref(slice);
        grpc_byte_buffer_reader_destroy(&reader);
    }

    const char *record_id = request && request->record_id ? request->record_id : "unknown";
    const char *table_name = request && request->table_name ? request->table_name : "unknown";

    char log_msg[256];
    snprintf(log_msg, sizeof(log_msg), "FetchLegacyData called - record_id: %s, table: %s",
             record_id, table_name);
    log_otlp(LOG_SEVERITY_INFO, trace_id, span_id, log_msg);

    /* Simulate DB lookup delay */
    simulate_db_delay();

    /* Build response */
    Grpcarch__LegacyDataResponse response = GRPCARCH__LEGACY_DATA_RESPONSE__INIT;
    Grpcarch__ResponseStatus status = GRPCARCH__RESPONSE_STATUS__INIT;
    Grpcarch__LegacyRecord record = GRPCARCH__LEGACY_RECORD__INIT;

    status.success = 1;
    char status_msg[256];
    snprintf(status_msg, sizeof(status_msg), "Record fetched successfully from %s", table_name);
    status.message = status_msg;

    record.id = (char*)record_id;

    /* Build raw_data JSON */
    char raw_data[512];
    snprintf(raw_data, sizeof(raw_data),
             "{\"source\": \"%s\", \"data\": \"legacy_value_%s\"}",
             table_name, record_id);
    record.raw_data.data = (uint8_t*)raw_data;
    record.raw_data.len = strlen(raw_data);

    time_t now = time(NULL);
    record.created_at = now - 86400;
    record.updated_at = now;

    /* Add fields map */
    record.n_fields = 2;
    Grpcarch__LegacyRecord__FieldsEntry *entries[2];
    Grpcarch__LegacyRecord__FieldsEntry entry1 = GRPCARCH__LEGACY_RECORD__FIELDS_ENTRY__INIT;
    Grpcarch__LegacyRecord__FieldsEntry entry2 = GRPCARCH__LEGACY_RECORD__FIELDS_ENTRY__INIT;

    entry1.key = "source";
    entry1.value = (char*)table_name;
    entry2.key = "fetched_by";
    entry2.value = "service-f";

    entries[0] = &entry1;
    entries[1] = &entry2;
    record.fields = entries;

    response.status = &status;
    response.record = &record;

    /* Serialize response */
    size_t response_len = grpcarch__legacy_data_response__get_packed_size(&response);
    uint8_t *response_buf = malloc(response_len);
    grpcarch__legacy_data_response__pack(&response, response_buf);

    /* Create byte buffer for response */
    grpc_slice response_slice = grpc_slice_from_copied_buffer((char*)response_buf, response_len);
    grpc_byte_buffer *response_bb = grpc_raw_byte_buffer_create(&response_slice, 1);

    /* Send response */
    grpc_metadata_array trailing_metadata;
    grpc_metadata_array_init(&trailing_metadata);

    grpc_op ops[3];
    memset(ops, 0, sizeof(ops));

    ops[0].op = GRPC_OP_SEND_INITIAL_METADATA;
    ops[0].data.send_initial_metadata.count = 0;
    ops[0].flags = 0;

    ops[1].op = GRPC_OP_SEND_MESSAGE;
    ops[1].data.send_message.send_message = response_bb;
    ops[1].flags = 0;

    ops[2].op = GRPC_OP_SEND_STATUS_FROM_SERVER;
    ops[2].data.send_status_from_server.trailing_metadata_count = 0;
    ops[2].data.send_status_from_server.status = GRPC_STATUS_OK;
    grpc_slice status_details = grpc_slice_from_static_string("OK");
    ops[2].data.send_status_from_server.status_details = &status_details;
    ops[2].flags = 0;

    grpc_call_error err = grpc_call_start_batch(ctx->call, ops, 3, (void*)(intptr_t)2, NULL);
    if (err != GRPC_CALL_OK) {
        fprintf(stderr, "[Service F] Error sending response: %d\n", err);
    }

    /* Wait for send to complete */
    grpc_event ev;
    do {
        ev = grpc_completion_queue_next(g_cq, gpr_inf_future(GPR_CLOCK_REALTIME), NULL);
    } while (ev.type != GRPC_OP_COMPLETE || ev.tag != (void*)(intptr_t)2);

    uint64_t end_time = get_time_nanos();
    double duration_ms = (end_time - start_time) / 1000000.0;

    snprintf(log_msg, sizeof(log_msg), "Record fetched successfully (duration: %.2fms)", duration_ms);
    log_otlp(LOG_SEVERITY_INFO, trace_id, span_id, log_msg);

    /* Record metrics */
    record_request_metrics(duration_ms);

    /* Export trace span */
    if (g_trace_exporter != NULL) {
        otlp_span_t span = {0};
        span.trace_id = trace_id;
        span.span_id = span_id;
        span.parent_span_id = parent_span_id[0] ? parent_span_id : NULL;
        span.name = "FetchLegacyData";
        span.kind = SPAN_KIND_SERVER;
        span.start_time_nanos = start_time;
        span.end_time_nanos = end_time;
        span.status_code = SPAN_STATUS_OK;

        /* Add attributes */
        span_attribute_t attrs[4];
        attrs[0].key = "rpc.system";
        attrs[0].string_value = "grpc";
        attrs[1].key = "rpc.service";
        attrs[1].string_value = "grpcarch.ServiceF";
        attrs[2].key = "rpc.method";
        attrs[2].string_value = "FetchLegacyData";
        attrs[3].key = "db.table";
        attrs[3].string_value = table_name;

        span.attributes = attrs;
        span.attribute_count = 4;

        otlp_export_span(g_trace_exporter, &span);
    }

    /* Cleanup */
    free(response_buf);
    grpc_slice_unref(response_slice);
    grpc_byte_buffer_destroy(response_bb);
    grpc_metadata_array_destroy(&trailing_metadata);

    if (request) {
        grpcarch__legacy_data_request__free_unpacked(request, NULL);
    }
}

/* Request a new call */
static call_context_t* request_call(void) {
    call_context_t *ctx = calloc(1, sizeof(call_context_t));
    if (!ctx) return NULL;

    static int request_counter = 0;
    ctx->request_id = ++request_counter;

    grpc_metadata_array_init(&ctx->request_metadata);
    grpc_call_details_init(&ctx->call_details);

    grpc_call_error err = grpc_server_request_call(
        g_server,
        &ctx->call,
        &ctx->call_details,
        &ctx->request_metadata,
        g_cq,
        g_cq,
        (void*)(intptr_t)ctx->request_id
    );

    if (err != GRPC_CALL_OK) {
        fprintf(stderr, "[Service F] Error requesting call: %d\n", err);
        free(ctx);
        return NULL;
    }

    return ctx;
}

/* Cleanup call context */
static void cleanup_call_context(call_context_t *ctx) {
    if (!ctx) return;

    if (ctx->call) {
        grpc_call_unref(ctx->call);
    }
    grpc_metadata_array_destroy(&ctx->request_metadata);
    grpc_call_details_destroy(&ctx->call_details);
    if (ctx->request_payload) {
        grpc_byte_buffer_destroy(ctx->request_payload);
    }
    free(ctx);
}

/* Main server loop */
static void run_server(const char *port) {
    char server_address[256];
    snprintf(server_address, sizeof(server_address), "0.0.0.0:%s", port);

    grpc_init();

    g_cq = grpc_completion_queue_create_for_next(NULL);
    g_server = grpc_server_create(NULL, NULL);

    grpc_server_credentials *creds = grpc_insecure_server_credentials_create();
    int bound_port = grpc_server_add_http2_port(g_server, server_address, creds);
    grpc_server_credentials_release(creds);
    if (bound_port == 0) {
        fprintf(stderr, "[Service F] Failed to bind to %s\n", server_address);
        exit(1);
    }

    grpc_server_register_completion_queue(g_server, g_cq, NULL);
    grpc_server_start(g_server);

    printf("[Service F] Server listening on %s\n", server_address);
    printf("[Service F] Legacy data service (C) ready\n");

    /* Request first call */
    call_context_t *pending_ctx = request_call();

    while (!g_shutdown) {
        gpr_timespec deadline = gpr_time_add(
            gpr_now(GPR_CLOCK_REALTIME),
            gpr_time_from_millis(100, GPR_TIMESPAN)
        );

        grpc_event ev = grpc_completion_queue_next(g_cq, deadline, NULL);

        if (ev.type == GRPC_OP_COMPLETE && ev.success) {
            if (ev.tag == (void*)(intptr_t)pending_ctx->request_id) {
                /* New call received */
                call_context_t *ctx = pending_ctx;

                /* Request next call immediately */
                pending_ctx = request_call();

                /* Check the method being called */
                char *method = grpc_slice_to_c_string(ctx->call_details.method);

                if (strstr(method, "FetchLegacyData") != NULL) {
                    /* Receive the message */
                    grpc_op ops[1];
                    memset(ops, 0, sizeof(ops));

                    ops[0].op = GRPC_OP_RECV_MESSAGE;
                    ops[0].data.recv_message.recv_message = &ctx->request_payload;
                    ops[0].flags = 0;

                    grpc_call_start_batch(ctx->call, ops, 1, (void*)(intptr_t)1, NULL);

                    /* Wait for message */
                    grpc_event msg_ev;
                    do {
                        msg_ev = grpc_completion_queue_next(g_cq, gpr_inf_future(GPR_CLOCK_REALTIME), NULL);
                    } while (msg_ev.type != GRPC_OP_COMPLETE || msg_ev.tag != (void*)(intptr_t)1);

                    /* Handle the request */
                    handle_fetch_legacy_data(ctx);
                } else {
                    printf("[Service F] Unknown method: %s\n", method);

                    /* Send UNIMPLEMENTED status */
                    grpc_op ops[2];
                    memset(ops, 0, sizeof(ops));

                    ops[0].op = GRPC_OP_SEND_INITIAL_METADATA;
                    ops[0].data.send_initial_metadata.count = 0;

                    ops[1].op = GRPC_OP_SEND_STATUS_FROM_SERVER;
                    ops[1].data.send_status_from_server.status = GRPC_STATUS_UNIMPLEMENTED;
                    grpc_slice status_details = grpc_slice_from_static_string("Method not implemented");
                    ops[1].data.send_status_from_server.status_details = &status_details;

                    grpc_call_start_batch(ctx->call, ops, 2, (void*)(intptr_t)3, NULL);

                    grpc_event status_ev;
                    do {
                        status_ev = grpc_completion_queue_next(g_cq, gpr_inf_future(GPR_CLOCK_REALTIME), NULL);
                    } while (status_ev.type != GRPC_OP_COMPLETE || status_ev.tag != (void*)(intptr_t)3);
                }

                gpr_free(method);
                cleanup_call_context(ctx);
            }
        } else if (ev.type == GRPC_QUEUE_SHUTDOWN) {
            break;
        }
    }

    /* Cleanup */
    if (pending_ctx) {
        cleanup_call_context(pending_ctx);
    }

    grpc_server_shutdown_and_notify(g_server, g_cq, NULL);
    grpc_completion_queue_shutdown(g_cq);

    /* Drain completion queue */
    while (grpc_completion_queue_next(g_cq, gpr_inf_future(GPR_CLOCK_REALTIME), NULL).type != GRPC_QUEUE_SHUTDOWN);

    grpc_completion_queue_destroy(g_cq);
    grpc_server_destroy(g_server);
    grpc_shutdown();
}

int main(int argc, char **argv) {
    const char *port = getenv("GRPC_PORT");
    if (!port) port = "50056";

    const char *otel_endpoint = getenv("OTEL_EXPORTER_OTLP_ENDPOINT");
    const char *service_name = getenv("OTEL_SERVICE_NAME");
    if (!service_name) service_name = "service-f";
    g_service_name = service_name;

    /* Seed random number generator */
    srand(time(NULL));

    printf("[Service F] Starting gRPC server...\n");

    /* Initialize OTLP exporters */
    if (otel_endpoint) {
        g_trace_exporter = otlp_exporter_create(otel_endpoint, service_name);
        if (g_trace_exporter) {
            printf("[Service F] OTLP trace exporter initialized: %s\n", otel_endpoint);
        } else {
            fprintf(stderr, "[Service F] Warning: Failed to initialize OTLP trace exporter\n");
        }

        g_log_exporter = otlp_log_exporter_create(otel_endpoint, service_name);
        if (g_log_exporter) {
            printf("[Service F] OTLP log exporter initialized: %s\n", otel_endpoint);
        } else {
            fprintf(stderr, "[Service F] Warning: Failed to initialize OTLP log exporter\n");
        }

        g_metrics_exporter = otlp_metrics_exporter_create(otel_endpoint, service_name);
        if (g_metrics_exporter) {
            printf("[Service F] OTLP metrics exporter initialized: %s\n", otel_endpoint);
            /* Start metrics export thread */
            pthread_create(&g_metrics_thread, NULL, metrics_export_thread, NULL);
        } else {
            fprintf(stderr, "[Service F] Warning: Failed to initialize OTLP metrics exporter\n");
        }
    }

    run_server(port);

    /* Cleanup exporters */
    g_shutdown = 1;

    /* Wait for metrics thread to finish */
    if (g_metrics_exporter) {
        pthread_join(g_metrics_thread, NULL);
        otlp_metrics_exporter_destroy(g_metrics_exporter);
    }

    if (g_log_exporter) {
        otlp_log_exporter_flush(g_log_exporter);
        otlp_log_exporter_destroy(g_log_exporter);
    }

    if (g_trace_exporter) {
        otlp_exporter_destroy(g_trace_exporter);
    }

    return 0;
}
