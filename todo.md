# Observability TODO

## Completed Items

### Service E (C++) - Full Telemetry ✅

All telemetry now working:
- **Traces**: Exporting to Jaeger via OTLP/gRPC
- **Logs**: Exporting to Elasticsearch via OTLP/gRPC
- **Metrics**: Exporting to Prometheus via OTLP/gRPC

**Implementation**:
- Multi-stage Dockerfile builds OpenTelemetry C++ SDK 1.13.0 from source
- Uses ccache mounts for incremental builds (rebuilds take ~7 seconds)
- Full `main.cpp` with traces, logs, and metrics instrumentation

**Files**:
- `services/service-e/Dockerfile` - multi-stage build with OTel C++ SDK
- `services/service-e/main.cpp` - full OpenTelemetry instrumentation

---

### Service F (C) - Traces and Logs ✅

- **Traces**: Working via custom OTLP/gRPC exporter
- **Logs**: Working via custom OTLP/gRPC exporter
- **Metrics**: ❌ Not supported (protobuf-c incompatible with proto3 optional fields)

**Implementation**:
- Custom OTLP log exporter using gRPC C core library
- Background thread batches and exports logs via OTLP/gRPC
- Metrics exporter written but excluded from build due to protobuf-c limitations

**Files**:
- `services/service-f/src/main.c` - uses OTLP log exporter
- `services/service-f/src/otlp_exporter.c` - trace exporter
- `services/service-f/src/otlp_log_exporter.c` - log exporter
- `services/service-f/src/otlp_metrics_exporter.c` - metrics exporter (not used)

---

## Outstanding Items

### Service F Metrics

**Issue**: protobuf-c does not support proto3 `optional` fields used in OpenTelemetry metrics protos.

**Potential Fixes**:
1. Wait for protobuf-c to add proto3 optional support
2. Use an older OpenTelemetry proto version without optional fields (none exist)
3. Fork and modify OTLP metrics proto to remove optional fields
4. Rewrite Service F in a language with better protobuf support

**Priority**: Low - Service F has traces and logs which cover most observability needs.

---

## Current Telemetry Coverage

| Service | Language | Traces | Logs | Metrics |
|---------|----------|--------|------|---------|
| service-a | Go | ✅ | ✅ | ✅ |
| service-b | Rust | ✅ | ✅ | ✅ |
| service-c | Python | ✅ | ✅ | ✅ |
| service-d | C# | ✅ | ✅ | ✅ |
| service-e | C++ | ✅ | ✅ | ✅ |
| service-f | C | ✅ | ✅ | ❌ |
