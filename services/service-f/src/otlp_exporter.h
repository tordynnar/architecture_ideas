/*
 * OTLP Trace Exporter - Pure C Implementation
 *
 * Exports OpenTelemetry traces via OTLP/gRPC protocol
 * using the gRPC C core library.
 */

#ifndef OTLP_EXPORTER_H
#define OTLP_EXPORTER_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Span kind enumeration (matches OTLP spec) */
typedef enum {
    SPAN_KIND_UNSPECIFIED = 0,
    SPAN_KIND_INTERNAL = 1,
    SPAN_KIND_SERVER = 2,
    SPAN_KIND_CLIENT = 3,
    SPAN_KIND_PRODUCER = 4,
    SPAN_KIND_CONSUMER = 5
} span_kind_t;

/* Span status code */
typedef enum {
    SPAN_STATUS_UNSET = 0,
    SPAN_STATUS_OK = 1,
    SPAN_STATUS_ERROR = 2
} span_status_code_t;

/* Span attribute */
typedef struct {
    const char *key;
    const char *string_value;  /* For simplicity, only string values */
} span_attribute_t;

/* Span data structure */
typedef struct {
    const char *trace_id;         /* 32-char hex string */
    const char *span_id;          /* 16-char hex string */
    const char *parent_span_id;   /* 16-char hex string, NULL if root */
    const char *name;             /* Span name */
    span_kind_t kind;
    uint64_t start_time_nanos;
    uint64_t end_time_nanos;
    span_status_code_t status_code;
    const char *status_message;

    span_attribute_t *attributes;
    size_t attribute_count;
} otlp_span_t;

/* Opaque exporter handle */
typedef struct otlp_exporter otlp_exporter_t;

/*
 * Create a new OTLP exporter
 *
 * @param endpoint  OTLP collector endpoint (e.g., "http://otel-collector:4317")
 * @param service_name  Name of this service for resource attributes
 * @return  Exporter handle, or NULL on failure
 */
otlp_exporter_t* otlp_exporter_create(const char *endpoint, const char *service_name);

/*
 * Export a span to the collector
 *
 * @param exporter  Exporter handle
 * @param span      Span data to export
 * @return  0 on success, -1 on failure
 */
int otlp_export_span(otlp_exporter_t *exporter, const otlp_span_t *span);

/*
 * Flush any pending spans
 *
 * @param exporter  Exporter handle
 * @return  0 on success, -1 on failure
 */
int otlp_exporter_flush(otlp_exporter_t *exporter);

/*
 * Destroy the exporter and free resources
 *
 * @param exporter  Exporter handle
 */
void otlp_exporter_destroy(otlp_exporter_t *exporter);

#ifdef __cplusplus
}
#endif

#endif /* OTLP_EXPORTER_H */
