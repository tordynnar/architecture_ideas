/*
 * OTLP Metrics Exporter - Pure C Implementation
 *
 * Exports OpenTelemetry metrics via OTLP/gRPC protocol
 * using the gRPC C core library.
 */

#ifndef OTLP_METRICS_EXPORTER_H
#define OTLP_METRICS_EXPORTER_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Metric types */
typedef enum {
    METRIC_TYPE_COUNTER,
    METRIC_TYPE_GAUGE,
    METRIC_TYPE_HISTOGRAM
} metric_type_t;

/* Metric attribute */
typedef struct {
    const char *key;
    const char *string_value;
} metric_attribute_t;

/* Data point for counters/gauges */
typedef struct {
    uint64_t timestamp_nanos;
    union {
        int64_t int_value;
        double double_value;
    };
    int is_double;
    metric_attribute_t *attributes;
    size_t attribute_count;
} metric_data_point_t;

/* Histogram data point */
typedef struct {
    uint64_t timestamp_nanos;
    uint64_t count;
    double sum;
    double *bucket_counts;
    double *explicit_bounds;
    size_t bucket_count;
    metric_attribute_t *attributes;
    size_t attribute_count;
} histogram_data_point_t;

/* Metric definition */
typedef struct {
    const char *name;
    const char *description;
    const char *unit;
    metric_type_t type;

    /* For counter/gauge */
    metric_data_point_t *data_points;
    size_t data_point_count;

    /* For histogram */
    histogram_data_point_t *histogram_points;
    size_t histogram_point_count;
} otlp_metric_t;

/* Opaque exporter handle */
typedef struct otlp_metrics_exporter otlp_metrics_exporter_t;

/*
 * Create a new OTLP metrics exporter
 *
 * @param endpoint  OTLP collector endpoint (e.g., "http://otel-collector:4317")
 * @param service_name  Name of this service for resource attributes
 * @return  Exporter handle, or NULL on failure
 */
otlp_metrics_exporter_t* otlp_metrics_exporter_create(const char *endpoint, const char *service_name);

/*
 * Export metrics to the collector
 *
 * @param exporter  Exporter handle
 * @param metrics   Array of metrics to export
 * @param count     Number of metrics
 * @return  0 on success, -1 on failure
 */
int otlp_export_metrics(otlp_metrics_exporter_t *exporter, const otlp_metric_t *metrics, size_t count);

/*
 * Destroy the exporter and free resources
 *
 * @param exporter  Exporter handle
 */
void otlp_metrics_exporter_destroy(otlp_metrics_exporter_t *exporter);

#ifdef __cplusplus
}
#endif

#endif /* OTLP_METRICS_EXPORTER_H */
