/*
 * OTLP Log Exporter - Pure C Implementation
 *
 * This implementation uses gRPC C core to export logs via OTLP/gRPC.
 * It builds the protobuf messages using the generated protobuf-c code.
 */

#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include "otlp_log_exporter.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/support/log.h>
#include <grpc/support/alloc.h>
#include <grpc/byte_buffer.h>
#include <grpc/byte_buffer_reader.h>

/* OTLP protobuf-c generated headers */
#include "opentelemetry/proto/logs/v1/logs.pb-c.h"
#include "opentelemetry/proto/collector/logs/v1/logs_service.pb-c.h"
#include "opentelemetry/proto/resource/v1/resource.pb-c.h"
#include "opentelemetry/proto/common/v1/common.pb-c.h"

/* Maximum log records to batch before export */
#define MAX_BATCH_SIZE 64

/* Exporter internal structure */
struct otlp_log_exporter {
    char *endpoint;
    char *host;
    char *port;
    char *service_name;
    grpc_channel *channel;
    grpc_completion_queue *cq;

    /* Log record batching */
    otlp_log_record_t *pending_records[MAX_BATCH_SIZE];
    size_t pending_count;
    pthread_mutex_t mutex;

    /* Background export thread */
    pthread_t export_thread;
    int running;
};

/* Parse endpoint URL */
static int parse_endpoint(const char *endpoint, char **host, char **port) {
    const char *ptr = endpoint;

    /* Skip http:// or https:// */
    if (strncmp(ptr, "http://", 7) == 0) {
        ptr += 7;
    } else if (strncmp(ptr, "https://", 8) == 0) {
        ptr += 8;
    }

    /* Find port separator */
    const char *colon = strchr(ptr, ':');
    if (colon) {
        size_t host_len = colon - ptr;
        *host = malloc(host_len + 1);
        strncpy(*host, ptr, host_len);
        (*host)[host_len] = '\0';

        *port = strdup(colon + 1);
    } else {
        *host = strdup(ptr);
        *port = strdup("4317");  /* Default OTLP gRPC port */
    }

    return 0;
}

/* Convert hex string to bytes */
static int hex_to_bytes(const char *hex, uint8_t *bytes, size_t len) {
    for (size_t i = 0; i < len; i++) {
        unsigned int val;
        if (sscanf(hex + (i * 2), "%2x", &val) != 1) {
            return -1;
        }
        bytes[i] = (uint8_t)val;
    }
    return 0;
}

/* Build and export log records via gRPC */
static int do_export(otlp_log_exporter_t *exporter, otlp_log_record_t **records, size_t count) {
    if (count == 0) return 0;

    /* Build ExportLogsServiceRequest */
    Opentelemetry__Proto__Collector__Logs__V1__ExportLogsServiceRequest request =
        OPENTELEMETRY__PROTO__COLLECTOR__LOGS__V1__EXPORT_LOGS_SERVICE_REQUEST__INIT;

    /* Create ResourceLogs */
    Opentelemetry__Proto__Logs__V1__ResourceLogs resource_logs =
        OPENTELEMETRY__PROTO__LOGS__V1__RESOURCE_LOGS__INIT;

    /* Create Resource with service name */
    Opentelemetry__Proto__Resource__V1__Resource resource =
        OPENTELEMETRY__PROTO__RESOURCE__V1__RESOURCE__INIT;

    Opentelemetry__Proto__Common__V1__KeyValue service_attr =
        OPENTELEMETRY__PROTO__COMMON__V1__KEY_VALUE__INIT;
    Opentelemetry__Proto__Common__V1__AnyValue service_value =
        OPENTELEMETRY__PROTO__COMMON__V1__ANY_VALUE__INIT;

    service_attr.key = "service.name";
    service_value.value_case = OPENTELEMETRY__PROTO__COMMON__V1__ANY_VALUE__VALUE_STRING_VALUE;
    service_value.string_value = exporter->service_name;
    service_attr.value = &service_value;

    Opentelemetry__Proto__Common__V1__KeyValue *attrs[1] = { &service_attr };
    resource.attributes = attrs;
    resource.n_attributes = 1;

    resource_logs.resource = &resource;

    /* Create ScopeLogs */
    Opentelemetry__Proto__Logs__V1__ScopeLogs scope_logs =
        OPENTELEMETRY__PROTO__LOGS__V1__SCOPE_LOGS__INIT;

    Opentelemetry__Proto__Common__V1__InstrumentationScope scope =
        OPENTELEMETRY__PROTO__COMMON__V1__INSTRUMENTATION_SCOPE__INIT;
    scope.name = "service-f-c";
    scope.version = "1.0.0";
    scope_logs.scope = &scope;

    /* Convert log records */
    Opentelemetry__Proto__Logs__V1__LogRecord **proto_logs =
        malloc(count * sizeof(Opentelemetry__Proto__Logs__V1__LogRecord*));
    uint8_t **trace_ids = malloc(count * sizeof(uint8_t*));
    uint8_t **span_ids = malloc(count * sizeof(uint8_t*));
    Opentelemetry__Proto__Common__V1__AnyValue **bodies = malloc(count * sizeof(Opentelemetry__Proto__Common__V1__AnyValue*));
    Opentelemetry__Proto__Common__V1__KeyValue ***log_attrs =
        malloc(count * sizeof(Opentelemetry__Proto__Common__V1__KeyValue**));
    Opentelemetry__Proto__Common__V1__AnyValue **attr_values =
        malloc(count * 16 * sizeof(Opentelemetry__Proto__Common__V1__AnyValue*));

    for (size_t i = 0; i < count; i++) {
        otlp_log_record_t *record = records[i];

        proto_logs[i] = malloc(sizeof(Opentelemetry__Proto__Logs__V1__LogRecord));
        opentelemetry__proto__logs__v1__log_record__init(proto_logs[i]);

        /* Convert trace_id (32 hex chars -> 16 bytes) */
        if (record->trace_id && strlen(record->trace_id) >= 32) {
            trace_ids[i] = malloc(16);
            hex_to_bytes(record->trace_id, trace_ids[i], 16);
            proto_logs[i]->trace_id.data = trace_ids[i];
            proto_logs[i]->trace_id.len = 16;
        } else {
            trace_ids[i] = NULL;
            proto_logs[i]->trace_id.data = NULL;
            proto_logs[i]->trace_id.len = 0;
        }

        /* Convert span_id (16 hex chars -> 8 bytes) */
        if (record->span_id && strlen(record->span_id) >= 16) {
            span_ids[i] = malloc(8);
            hex_to_bytes(record->span_id, span_ids[i], 8);
            proto_logs[i]->span_id.data = span_ids[i];
            proto_logs[i]->span_id.len = 8;
        } else {
            span_ids[i] = NULL;
            proto_logs[i]->span_id.data = NULL;
            proto_logs[i]->span_id.len = 0;
        }

        proto_logs[i]->time_unix_nano = record->timestamp_nanos;
        proto_logs[i]->severity_number = (Opentelemetry__Proto__Logs__V1__SeverityNumber)record->severity;

        /* Set severity text */
        switch (record->severity) {
            case LOG_SEVERITY_TRACE: proto_logs[i]->severity_text = "TRACE"; break;
            case LOG_SEVERITY_DEBUG: proto_logs[i]->severity_text = "DEBUG"; break;
            case LOG_SEVERITY_INFO: proto_logs[i]->severity_text = "INFO"; break;
            case LOG_SEVERITY_WARN: proto_logs[i]->severity_text = "WARN"; break;
            case LOG_SEVERITY_ERROR: proto_logs[i]->severity_text = "ERROR"; break;
            case LOG_SEVERITY_FATAL: proto_logs[i]->severity_text = "FATAL"; break;
            default: proto_logs[i]->severity_text = "UNSPECIFIED"; break;
        }

        /* Set body */
        bodies[i] = malloc(sizeof(Opentelemetry__Proto__Common__V1__AnyValue));
        opentelemetry__proto__common__v1__any_value__init(bodies[i]);
        bodies[i]->value_case = OPENTELEMETRY__PROTO__COMMON__V1__ANY_VALUE__VALUE_STRING_VALUE;
        bodies[i]->string_value = (char*)record->body;
        proto_logs[i]->body = bodies[i];

        /* Add attributes */
        if (record->attribute_count > 0 && record->attributes) {
            log_attrs[i] = malloc(record->attribute_count * sizeof(Opentelemetry__Proto__Common__V1__KeyValue*));
            proto_logs[i]->n_attributes = record->attribute_count;

            for (size_t j = 0; j < record->attribute_count; j++) {
                log_attrs[i][j] = malloc(sizeof(Opentelemetry__Proto__Common__V1__KeyValue));
                opentelemetry__proto__common__v1__key_value__init(log_attrs[i][j]);

                attr_values[i * 16 + j] = malloc(sizeof(Opentelemetry__Proto__Common__V1__AnyValue));
                opentelemetry__proto__common__v1__any_value__init(attr_values[i * 16 + j]);

                log_attrs[i][j]->key = (char*)record->attributes[j].key;
                attr_values[i * 16 + j]->value_case =
                    OPENTELEMETRY__PROTO__COMMON__V1__ANY_VALUE__VALUE_STRING_VALUE;
                attr_values[i * 16 + j]->string_value = (char*)record->attributes[j].string_value;
                log_attrs[i][j]->value = attr_values[i * 16 + j];
            }
            proto_logs[i]->attributes = log_attrs[i];
        } else {
            log_attrs[i] = NULL;
            proto_logs[i]->attributes = NULL;
            proto_logs[i]->n_attributes = 0;
        }
    }

    scope_logs.log_records = proto_logs;
    scope_logs.n_log_records = count;

    Opentelemetry__Proto__Logs__V1__ScopeLogs *scope_logs_arr[1] = { &scope_logs };
    resource_logs.scope_logs = scope_logs_arr;
    resource_logs.n_scope_logs = 1;

    Opentelemetry__Proto__Logs__V1__ResourceLogs *resource_logs_arr[1] = { &resource_logs };
    request.resource_logs = resource_logs_arr;
    request.n_resource_logs = 1;

    /* Serialize request */
    size_t request_len = opentelemetry__proto__collector__logs__v1__export_logs_service_request__get_packed_size(&request);
    uint8_t *request_buf = malloc(request_len);
    opentelemetry__proto__collector__logs__v1__export_logs_service_request__pack(&request, request_buf);

    /* Make gRPC call */
    grpc_slice method_slice = grpc_slice_from_static_string(
        "/opentelemetry.proto.collector.logs.v1.LogsService/Export");

    grpc_call *call = grpc_channel_create_call(
        exporter->channel,
        NULL,
        GRPC_PROPAGATE_DEFAULTS,
        exporter->cq,
        method_slice,
        NULL,
        gpr_inf_future(GPR_CLOCK_REALTIME),
        NULL
    );

    if (!call) {
        fprintf(stderr, "[OTLP-LOGS] Failed to create call\n");
        goto cleanup;
    }

    /* Prepare request */
    grpc_slice request_slice = grpc_slice_from_copied_buffer((char*)request_buf, request_len);
    grpc_byte_buffer *request_bb = grpc_raw_byte_buffer_create(&request_slice, 1);

    grpc_metadata_array initial_metadata;
    grpc_metadata_array trailing_metadata;
    grpc_byte_buffer *response_bb = NULL;
    grpc_status_code status_code;
    grpc_slice status_details;

    grpc_metadata_array_init(&initial_metadata);
    grpc_metadata_array_init(&trailing_metadata);

    grpc_op ops[6];
    memset(ops, 0, sizeof(ops));

    ops[0].op = GRPC_OP_SEND_INITIAL_METADATA;
    ops[0].data.send_initial_metadata.count = 0;

    ops[1].op = GRPC_OP_SEND_MESSAGE;
    ops[1].data.send_message.send_message = request_bb;

    ops[2].op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;

    ops[3].op = GRPC_OP_RECV_INITIAL_METADATA;
    ops[3].data.recv_initial_metadata.recv_initial_metadata = &initial_metadata;

    ops[4].op = GRPC_OP_RECV_MESSAGE;
    ops[4].data.recv_message.recv_message = &response_bb;

    ops[5].op = GRPC_OP_RECV_STATUS_ON_CLIENT;
    ops[5].data.recv_status_on_client.trailing_metadata = &trailing_metadata;
    ops[5].data.recv_status_on_client.status = &status_code;
    ops[5].data.recv_status_on_client.status_details = &status_details;

    grpc_call_error err = grpc_call_start_batch(call, ops, 6, (void*)(intptr_t)200, NULL);
    if (err != GRPC_CALL_OK) {
        fprintf(stderr, "[OTLP-LOGS] Failed to start batch: %d\n", err);
        grpc_call_unref(call);
        goto cleanup;
    }

    /* Wait for completion with timeout */
    gpr_timespec deadline = gpr_time_add(
        gpr_now(GPR_CLOCK_REALTIME),
        gpr_time_from_seconds(5, GPR_TIMESPAN)
    );

    grpc_event ev = grpc_completion_queue_next(exporter->cq, deadline, NULL);

    if (ev.type == GRPC_OP_COMPLETE && ev.success) {
        if (status_code != GRPC_STATUS_OK) {
            char *details = grpc_slice_to_c_string(status_details);
            fprintf(stderr, "[OTLP-LOGS] Export failed: %d - %s\n", status_code, details);
            gpr_free(details);
        }
    } else {
        fprintf(stderr, "[OTLP-LOGS] Export timed out or failed\n");
    }

    /* Cleanup gRPC resources */
    grpc_slice_unref(status_details);
    grpc_metadata_array_destroy(&initial_metadata);
    grpc_metadata_array_destroy(&trailing_metadata);
    if (response_bb) {
        grpc_byte_buffer_destroy(response_bb);
    }
    grpc_byte_buffer_destroy(request_bb);
    grpc_slice_unref(request_slice);
    grpc_call_unref(call);

cleanup:
    /* Free all allocated memory */
    for (size_t i = 0; i < count; i++) {
        if (trace_ids[i]) free(trace_ids[i]);
        if (span_ids[i]) free(span_ids[i]);
        free(bodies[i]);

        if (log_attrs[i]) {
            for (size_t j = 0; j < proto_logs[i]->n_attributes; j++) {
                free(attr_values[i * 16 + j]);
                free(log_attrs[i][j]);
            }
            free(log_attrs[i]);
        }

        free(proto_logs[i]);
    }

    free(proto_logs);
    free(trace_ids);
    free(span_ids);
    free(bodies);
    free(log_attrs);
    free(attr_values);
    free(request_buf);

    return 0;
}

/* Background export thread */
static void* export_thread_func(void *arg) {
    otlp_log_exporter_t *exporter = (otlp_log_exporter_t*)arg;

    while (exporter->running) {
        /* Sleep for batch interval */
        usleep(1000000);  /* 1 second */

        /* Check for pending records */
        pthread_mutex_lock(&exporter->mutex);

        if (exporter->pending_count > 0) {
            /* Copy pending records */
            otlp_log_record_t *batch[MAX_BATCH_SIZE];
            size_t batch_count = exporter->pending_count;

            for (size_t i = 0; i < batch_count; i++) {
                batch[i] = exporter->pending_records[i];
                exporter->pending_records[i] = NULL;
            }
            exporter->pending_count = 0;

            pthread_mutex_unlock(&exporter->mutex);

            /* Export batch */
            do_export(exporter, batch, batch_count);

            /* Free record copies */
            for (size_t i = 0; i < batch_count; i++) {
                free((void*)batch[i]->trace_id);
                free((void*)batch[i]->span_id);
                free((void*)batch[i]->body);
                if (batch[i]->attributes) {
                    for (size_t j = 0; j < batch[i]->attribute_count; j++) {
                        free((void*)batch[i]->attributes[j].key);
                        free((void*)batch[i]->attributes[j].string_value);
                    }
                    free(batch[i]->attributes);
                }
                free(batch[i]);
            }
        } else {
            pthread_mutex_unlock(&exporter->mutex);
        }
    }

    return NULL;
}

otlp_log_exporter_t* otlp_log_exporter_create(const char *endpoint, const char *service_name) {
    otlp_log_exporter_t *exporter = calloc(1, sizeof(otlp_log_exporter_t));
    if (!exporter) return NULL;

    exporter->endpoint = strdup(endpoint);
    exporter->service_name = strdup(service_name);

    if (parse_endpoint(endpoint, &exporter->host, &exporter->port) != 0) {
        free(exporter->endpoint);
        free(exporter->service_name);
        free(exporter);
        return NULL;
    }

    /* Create channel (reuse existing gRPC init from trace exporter) */
    char target[256];
    snprintf(target, sizeof(target), "%s:%s", exporter->host, exporter->port);

    grpc_channel_credentials *creds = grpc_insecure_credentials_create();
    exporter->channel = grpc_channel_create(target, creds, NULL);
    grpc_channel_credentials_release(creds);
    if (!exporter->channel) {
        fprintf(stderr, "[OTLP-LOGS] Failed to create channel to %s\n", target);
        free(exporter->endpoint);
        free(exporter->service_name);
        free(exporter->host);
        free(exporter->port);
        free(exporter);
        return NULL;
    }

    /* Create completion queue */
    exporter->cq = grpc_completion_queue_create_for_next(NULL);

    /* Initialize mutex */
    pthread_mutex_init(&exporter->mutex, NULL);

    /* Start background thread */
    exporter->running = 1;
    pthread_create(&exporter->export_thread, NULL, export_thread_func, exporter);

    return exporter;
}

int otlp_export_log(otlp_log_exporter_t *exporter, const otlp_log_record_t *record) {
    if (!exporter || !record) return -1;

    pthread_mutex_lock(&exporter->mutex);

    if (exporter->pending_count >= MAX_BATCH_SIZE) {
        pthread_mutex_unlock(&exporter->mutex);
        fprintf(stderr, "[OTLP-LOGS] Batch full, dropping log record\n");
        return -1;
    }

    /* Copy record data */
    otlp_log_record_t *copy = calloc(1, sizeof(otlp_log_record_t));
    copy->trace_id = record->trace_id ? strdup(record->trace_id) : NULL;
    copy->span_id = record->span_id ? strdup(record->span_id) : NULL;
    copy->severity = record->severity;
    copy->body = strdup(record->body ? record->body : "");
    copy->timestamp_nanos = record->timestamp_nanos;

    /* Copy attributes */
    if (record->attribute_count > 0 && record->attributes) {
        copy->attributes = calloc(record->attribute_count, sizeof(log_attribute_t));
        copy->attribute_count = record->attribute_count;
        for (size_t i = 0; i < record->attribute_count; i++) {
            copy->attributes[i].key = strdup(record->attributes[i].key);
            copy->attributes[i].string_value = strdup(record->attributes[i].string_value);
        }
    }

    exporter->pending_records[exporter->pending_count++] = copy;

    pthread_mutex_unlock(&exporter->mutex);

    return 0;
}

int otlp_log_exporter_flush(otlp_log_exporter_t *exporter) {
    if (!exporter) return -1;

    pthread_mutex_lock(&exporter->mutex);

    if (exporter->pending_count > 0) {
        otlp_log_record_t *batch[MAX_BATCH_SIZE];
        size_t batch_count = exporter->pending_count;

        for (size_t i = 0; i < batch_count; i++) {
            batch[i] = exporter->pending_records[i];
            exporter->pending_records[i] = NULL;
        }
        exporter->pending_count = 0;

        pthread_mutex_unlock(&exporter->mutex);

        do_export(exporter, batch, batch_count);

        for (size_t i = 0; i < batch_count; i++) {
            free((void*)batch[i]->trace_id);
            free((void*)batch[i]->span_id);
            free((void*)batch[i]->body);
            if (batch[i]->attributes) {
                for (size_t j = 0; j < batch[i]->attribute_count; j++) {
                    free((void*)batch[i]->attributes[j].key);
                    free((void*)batch[i]->attributes[j].string_value);
                }
                free(batch[i]->attributes);
            }
            free(batch[i]);
        }
    } else {
        pthread_mutex_unlock(&exporter->mutex);
    }

    return 0;
}

void otlp_log_exporter_destroy(otlp_log_exporter_t *exporter) {
    if (!exporter) return;

    /* Stop background thread */
    exporter->running = 0;
    pthread_join(exporter->export_thread, NULL);

    /* Flush any remaining records */
    otlp_log_exporter_flush(exporter);

    /* Cleanup gRPC */
    grpc_channel_destroy(exporter->channel);
    grpc_completion_queue_shutdown(exporter->cq);

    grpc_event ev;
    do {
        ev = grpc_completion_queue_next(exporter->cq, gpr_inf_future(GPR_CLOCK_REALTIME), NULL);
    } while (ev.type != GRPC_QUEUE_SHUTDOWN);

    grpc_completion_queue_destroy(exporter->cq);

    /* Free memory */
    pthread_mutex_destroy(&exporter->mutex);
    free(exporter->endpoint);
    free(exporter->service_name);
    free(exporter->host);
    free(exporter->port);
    free(exporter);
}
