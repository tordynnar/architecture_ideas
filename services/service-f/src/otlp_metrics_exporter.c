/*
 * OTLP Metrics Exporter - Pure C Implementation
 *
 * This implementation uses gRPC C core to export metrics via OTLP/gRPC.
 */

#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include "otlp_metrics_exporter.h"

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
#include "opentelemetry/proto/metrics/v1/metrics.pb-c.h"
#include "opentelemetry/proto/collector/metrics/v1/metrics_service.pb-c.h"
#include "opentelemetry/proto/resource/v1/resource.pb-c.h"
#include "opentelemetry/proto/common/v1/common.pb-c.h"

/* Exporter internal structure */
struct otlp_metrics_exporter {
    char *endpoint;
    char *host;
    char *port;
    char *service_name;
    grpc_channel *channel;
    grpc_completion_queue *cq;
};

/* Parse endpoint URL */
static int parse_endpoint(const char *endpoint, char **host, char **port) {
    const char *ptr = endpoint;

    if (strncmp(ptr, "http://", 7) == 0) {
        ptr += 7;
    } else if (strncmp(ptr, "https://", 8) == 0) {
        ptr += 8;
    }

    const char *colon = strchr(ptr, ':');
    if (colon) {
        size_t host_len = colon - ptr;
        *host = malloc(host_len + 1);
        strncpy(*host, ptr, host_len);
        (*host)[host_len] = '\0';
        *port = strdup(colon + 1);
    } else {
        *host = strdup(ptr);
        *port = strdup("4317");
    }

    return 0;
}

otlp_metrics_exporter_t* otlp_metrics_exporter_create(const char *endpoint, const char *service_name) {
    otlp_metrics_exporter_t *exporter = calloc(1, sizeof(otlp_metrics_exporter_t));
    if (!exporter) return NULL;

    exporter->endpoint = strdup(endpoint);
    exporter->service_name = strdup(service_name);

    if (parse_endpoint(endpoint, &exporter->host, &exporter->port) != 0) {
        free(exporter->endpoint);
        free(exporter->service_name);
        free(exporter);
        return NULL;
    }

    char target[256];
    snprintf(target, sizeof(target), "%s:%s", exporter->host, exporter->port);

    grpc_channel_credentials *creds = grpc_insecure_credentials_create();
    exporter->channel = grpc_channel_create(target, creds, NULL);
    grpc_channel_credentials_release(creds);
    if (!exporter->channel) {
        fprintf(stderr, "[OTLP-METRICS] Failed to create channel to %s\n", target);
        free(exporter->endpoint);
        free(exporter->service_name);
        free(exporter->host);
        free(exporter->port);
        free(exporter);
        return NULL;
    }

    exporter->cq = grpc_completion_queue_create_for_next(NULL);

    return exporter;
}

int otlp_export_metrics(otlp_metrics_exporter_t *exporter, const otlp_metric_t *metrics, size_t count) {
    if (!exporter || !metrics || count == 0) return -1;

    /* Build ExportMetricsServiceRequest */
    Opentelemetry__Proto__Collector__Metrics__V1__ExportMetricsServiceRequest request =
        OPENTELEMETRY__PROTO__COLLECTOR__METRICS__V1__EXPORT_METRICS_SERVICE_REQUEST__INIT;

    /* Create ResourceMetrics */
    Opentelemetry__Proto__Metrics__V1__ResourceMetrics resource_metrics =
        OPENTELEMETRY__PROTO__METRICS__V1__RESOURCE_METRICS__INIT;

    /* Create Resource */
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

    resource_metrics.resource = &resource;

    /* Create ScopeMetrics */
    Opentelemetry__Proto__Metrics__V1__ScopeMetrics scope_metrics =
        OPENTELEMETRY__PROTO__METRICS__V1__SCOPE_METRICS__INIT;

    Opentelemetry__Proto__Common__V1__InstrumentationScope scope =
        OPENTELEMETRY__PROTO__COMMON__V1__INSTRUMENTATION_SCOPE__INIT;
    scope.name = "service-f-c";
    scope.version = "1.0.0";
    scope_metrics.scope = &scope;

    /* Convert metrics */
    Opentelemetry__Proto__Metrics__V1__Metric **proto_metrics =
        malloc(count * sizeof(Opentelemetry__Proto__Metrics__V1__Metric*));

    /* Arrays for cleanup */
    void **cleanup_list = malloc(count * 100 * sizeof(void*));
    size_t cleanup_count = 0;

    for (size_t i = 0; i < count; i++) {
        const otlp_metric_t *metric = &metrics[i];

        proto_metrics[i] = malloc(sizeof(Opentelemetry__Proto__Metrics__V1__Metric));
        opentelemetry__proto__metrics__v1__metric__init(proto_metrics[i]);
        cleanup_list[cleanup_count++] = proto_metrics[i];

        proto_metrics[i]->name = (char*)metric->name;
        proto_metrics[i]->description = (char*)metric->description;
        proto_metrics[i]->unit = (char*)metric->unit;

        if (metric->type == METRIC_TYPE_COUNTER && metric->data_point_count > 0) {
            /* Sum metric (counter) */
            Opentelemetry__Proto__Metrics__V1__Sum *sum = malloc(sizeof(Opentelemetry__Proto__Metrics__V1__Sum));
            opentelemetry__proto__metrics__v1__sum__init(sum);
            cleanup_list[cleanup_count++] = sum;

            sum->is_monotonic = 1;
            sum->aggregation_temporality = OPENTELEMETRY__PROTO__METRICS__V1__AGGREGATION_TEMPORALITY__AGGREGATION_TEMPORALITY_CUMULATIVE;

            Opentelemetry__Proto__Metrics__V1__NumberDataPoint **data_points =
                malloc(metric->data_point_count * sizeof(Opentelemetry__Proto__Metrics__V1__NumberDataPoint*));
            cleanup_list[cleanup_count++] = data_points;

            for (size_t j = 0; j < metric->data_point_count; j++) {
                const metric_data_point_t *dp = &metric->data_points[j];

                data_points[j] = malloc(sizeof(Opentelemetry__Proto__Metrics__V1__NumberDataPoint));
                opentelemetry__proto__metrics__v1__number_data_point__init(data_points[j]);
                cleanup_list[cleanup_count++] = data_points[j];

                data_points[j]->time_unix_nano = dp->timestamp_nanos;

                if (dp->is_double) {
                    data_points[j]->value_case = OPENTELEMETRY__PROTO__METRICS__V1__NUMBER_DATA_POINT__VALUE_AS_DOUBLE;
                    data_points[j]->as_double = dp->double_value;
                } else {
                    data_points[j]->value_case = OPENTELEMETRY__PROTO__METRICS__V1__NUMBER_DATA_POINT__VALUE_AS_INT;
                    data_points[j]->as_int = dp->int_value;
                }

                /* Add attributes */
                if (dp->attribute_count > 0 && dp->attributes) {
                    Opentelemetry__Proto__Common__V1__KeyValue **kv_attrs =
                        malloc(dp->attribute_count * sizeof(Opentelemetry__Proto__Common__V1__KeyValue*));
                    cleanup_list[cleanup_count++] = kv_attrs;

                    for (size_t k = 0; k < dp->attribute_count; k++) {
                        kv_attrs[k] = malloc(sizeof(Opentelemetry__Proto__Common__V1__KeyValue));
                        opentelemetry__proto__common__v1__key_value__init(kv_attrs[k]);
                        cleanup_list[cleanup_count++] = kv_attrs[k];

                        Opentelemetry__Proto__Common__V1__AnyValue *val = malloc(sizeof(Opentelemetry__Proto__Common__V1__AnyValue));
                        opentelemetry__proto__common__v1__any_value__init(val);
                        cleanup_list[cleanup_count++] = val;

                        kv_attrs[k]->key = (char*)dp->attributes[k].key;
                        val->value_case = OPENTELEMETRY__PROTO__COMMON__V1__ANY_VALUE__VALUE_STRING_VALUE;
                        val->string_value = (char*)dp->attributes[k].string_value;
                        kv_attrs[k]->value = val;
                    }

                    data_points[j]->attributes = kv_attrs;
                    data_points[j]->n_attributes = dp->attribute_count;
                }
            }

            sum->data_points = data_points;
            sum->n_data_points = metric->data_point_count;

            proto_metrics[i]->data_case = OPENTELEMETRY__PROTO__METRICS__V1__METRIC__DATA_SUM;
            proto_metrics[i]->sum = sum;

        } else if (metric->type == METRIC_TYPE_GAUGE && metric->data_point_count > 0) {
            /* Gauge metric */
            Opentelemetry__Proto__Metrics__V1__Gauge *gauge = malloc(sizeof(Opentelemetry__Proto__Metrics__V1__Gauge));
            opentelemetry__proto__metrics__v1__gauge__init(gauge);
            cleanup_list[cleanup_count++] = gauge;

            Opentelemetry__Proto__Metrics__V1__NumberDataPoint **data_points =
                malloc(metric->data_point_count * sizeof(Opentelemetry__Proto__Metrics__V1__NumberDataPoint*));
            cleanup_list[cleanup_count++] = data_points;

            for (size_t j = 0; j < metric->data_point_count; j++) {
                const metric_data_point_t *dp = &metric->data_points[j];

                data_points[j] = malloc(sizeof(Opentelemetry__Proto__Metrics__V1__NumberDataPoint));
                opentelemetry__proto__metrics__v1__number_data_point__init(data_points[j]);
                cleanup_list[cleanup_count++] = data_points[j];

                data_points[j]->time_unix_nano = dp->timestamp_nanos;

                if (dp->is_double) {
                    data_points[j]->value_case = OPENTELEMETRY__PROTO__METRICS__V1__NUMBER_DATA_POINT__VALUE_AS_DOUBLE;
                    data_points[j]->as_double = dp->double_value;
                } else {
                    data_points[j]->value_case = OPENTELEMETRY__PROTO__METRICS__V1__NUMBER_DATA_POINT__VALUE_AS_INT;
                    data_points[j]->as_int = dp->int_value;
                }
            }

            gauge->data_points = data_points;
            gauge->n_data_points = metric->data_point_count;

            proto_metrics[i]->data_case = OPENTELEMETRY__PROTO__METRICS__V1__METRIC__DATA_GAUGE;
            proto_metrics[i]->gauge = gauge;

        } else if (metric->type == METRIC_TYPE_HISTOGRAM && metric->histogram_point_count > 0) {
            /* Histogram metric */
            Opentelemetry__Proto__Metrics__V1__Histogram *histogram = malloc(sizeof(Opentelemetry__Proto__Metrics__V1__Histogram));
            opentelemetry__proto__metrics__v1__histogram__init(histogram);
            cleanup_list[cleanup_count++] = histogram;

            histogram->aggregation_temporality = OPENTELEMETRY__PROTO__METRICS__V1__AGGREGATION_TEMPORALITY__AGGREGATION_TEMPORALITY_CUMULATIVE;

            Opentelemetry__Proto__Metrics__V1__HistogramDataPoint **data_points =
                malloc(metric->histogram_point_count * sizeof(Opentelemetry__Proto__Metrics__V1__HistogramDataPoint*));
            cleanup_list[cleanup_count++] = data_points;

            for (size_t j = 0; j < metric->histogram_point_count; j++) {
                const histogram_data_point_t *dp = &metric->histogram_points[j];

                data_points[j] = malloc(sizeof(Opentelemetry__Proto__Metrics__V1__HistogramDataPoint));
                opentelemetry__proto__metrics__v1__histogram_data_point__init(data_points[j]);
                cleanup_list[cleanup_count++] = data_points[j];

                data_points[j]->time_unix_nano = dp->timestamp_nanos;
                data_points[j]->count = dp->count;
                data_points[j]->sum = dp->sum;
                data_points[j]->has_sum = 1;

                if (dp->bucket_count > 0 && dp->bucket_counts) {
                    uint64_t *bucket_counts = malloc(dp->bucket_count * sizeof(uint64_t));
                    cleanup_list[cleanup_count++] = bucket_counts;
                    for (size_t k = 0; k < dp->bucket_count; k++) {
                        bucket_counts[k] = (uint64_t)dp->bucket_counts[k];
                    }
                    data_points[j]->bucket_counts = bucket_counts;
                    data_points[j]->n_bucket_counts = dp->bucket_count;

                    if (dp->explicit_bounds) {
                        data_points[j]->explicit_bounds = dp->explicit_bounds;
                        data_points[j]->n_explicit_bounds = dp->bucket_count > 0 ? dp->bucket_count - 1 : 0;
                    }
                }
            }

            histogram->data_points = data_points;
            histogram->n_data_points = metric->histogram_point_count;

            proto_metrics[i]->data_case = OPENTELEMETRY__PROTO__METRICS__V1__METRIC__DATA_HISTOGRAM;
            proto_metrics[i]->histogram = histogram;
        }
    }

    scope_metrics.metrics = proto_metrics;
    scope_metrics.n_metrics = count;

    Opentelemetry__Proto__Metrics__V1__ScopeMetrics *scope_metrics_arr[1] = { &scope_metrics };
    resource_metrics.scope_metrics = scope_metrics_arr;
    resource_metrics.n_scope_metrics = 1;

    Opentelemetry__Proto__Metrics__V1__ResourceMetrics *resource_metrics_arr[1] = { &resource_metrics };
    request.resource_metrics = resource_metrics_arr;
    request.n_resource_metrics = 1;

    /* Serialize request */
    size_t request_len = opentelemetry__proto__collector__metrics__v1__export_metrics_service_request__get_packed_size(&request);
    uint8_t *request_buf = malloc(request_len);
    opentelemetry__proto__collector__metrics__v1__export_metrics_service_request__pack(&request, request_buf);

    /* Make gRPC call */
    grpc_slice method_slice = grpc_slice_from_static_string(
        "/opentelemetry.proto.collector.metrics.v1.MetricsService/Export");

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

    int result = 0;
    if (!call) {
        fprintf(stderr, "[OTLP-METRICS] Failed to create call\n");
        result = -1;
        goto cleanup;
    }

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

    grpc_call_error err = grpc_call_start_batch(call, ops, 6, (void*)(intptr_t)300, NULL);
    if (err != GRPC_CALL_OK) {
        fprintf(stderr, "[OTLP-METRICS] Failed to start batch: %d\n", err);
        grpc_call_unref(call);
        result = -1;
        goto cleanup;
    }

    gpr_timespec deadline = gpr_time_add(
        gpr_now(GPR_CLOCK_REALTIME),
        gpr_time_from_seconds(5, GPR_TIMESPAN)
    );

    grpc_event ev = grpc_completion_queue_next(exporter->cq, deadline, NULL);

    if (ev.type == GRPC_OP_COMPLETE && ev.success) {
        if (status_code != GRPC_STATUS_OK) {
            char *details = grpc_slice_to_c_string(status_details);
            fprintf(stderr, "[OTLP-METRICS] Export failed: %d - %s\n", status_code, details);
            gpr_free(details);
            result = -1;
        }
    } else {
        fprintf(stderr, "[OTLP-METRICS] Export timed out or failed\n");
        result = -1;
    }

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
    for (size_t i = 0; i < cleanup_count; i++) {
        free(cleanup_list[i]);
    }
    free(cleanup_list);
    free(proto_metrics);
    free(request_buf);

    return result;
}

void otlp_metrics_exporter_destroy(otlp_metrics_exporter_t *exporter) {
    if (!exporter) return;

    grpc_channel_destroy(exporter->channel);
    grpc_completion_queue_shutdown(exporter->cq);

    grpc_event ev;
    do {
        ev = grpc_completion_queue_next(exporter->cq, gpr_inf_future(GPR_CLOCK_REALTIME), NULL);
    } while (ev.type != GRPC_QUEUE_SHUTDOWN);

    grpc_completion_queue_destroy(exporter->cq);

    free(exporter->endpoint);
    free(exporter->service_name);
    free(exporter->host);
    free(exporter->port);
    free(exporter);
}
