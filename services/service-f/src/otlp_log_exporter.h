/*
 * OTLP Log Exporter - Pure C Implementation
 *
 * Exports OpenTelemetry logs via OTLP/gRPC protocol
 * using the gRPC C core library.
 */

#ifndef OTLP_LOG_EXPORTER_H
#define OTLP_LOG_EXPORTER_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Log severity levels (matches OTLP spec) */
typedef enum {
    LOG_SEVERITY_UNSPECIFIED = 0,
    LOG_SEVERITY_TRACE = 1,
    LOG_SEVERITY_DEBUG = 5,
    LOG_SEVERITY_INFO = 9,
    LOG_SEVERITY_WARN = 13,
    LOG_SEVERITY_ERROR = 17,
    LOG_SEVERITY_FATAL = 21
} log_severity_t;

/* Log attribute */
typedef struct {
    const char *key;
    const char *string_value;
} log_attribute_t;

/* Log record data structure */
typedef struct {
    const char *trace_id;         /* 32-char hex string, optional */
    const char *span_id;          /* 16-char hex string, optional */
    log_severity_t severity;
    const char *body;             /* Log message body */
    uint64_t timestamp_nanos;

    log_attribute_t *attributes;
    size_t attribute_count;
} otlp_log_record_t;

/* Opaque exporter handle */
typedef struct otlp_log_exporter otlp_log_exporter_t;

/*
 * Create a new OTLP log exporter
 *
 * @param endpoint  OTLP collector endpoint (e.g., "http://otel-collector:4317")
 * @param service_name  Name of this service for resource attributes
 * @return  Exporter handle, or NULL on failure
 */
otlp_log_exporter_t* otlp_log_exporter_create(const char *endpoint, const char *service_name);

/*
 * Export a log record to the collector
 *
 * @param exporter  Exporter handle
 * @param record    Log record data to export
 * @return  0 on success, -1 on failure
 */
int otlp_export_log(otlp_log_exporter_t *exporter, const otlp_log_record_t *record);

/*
 * Flush any pending log records
 *
 * @param exporter  Exporter handle
 * @return  0 on success, -1 on failure
 */
int otlp_log_exporter_flush(otlp_log_exporter_t *exporter);

/*
 * Destroy the exporter and free resources
 *
 * @param exporter  Exporter handle
 */
void otlp_log_exporter_destroy(otlp_log_exporter_t *exporter);

#ifdef __cplusplus
}
#endif

#endif /* OTLP_LOG_EXPORTER_H */
