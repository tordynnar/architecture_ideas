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

### Service F (C) - Full Telemetry ✅

All telemetry now working:
- **Traces**: Working via custom OTLP/gRPC exporter
- **Logs**: Working via custom OTLP/gRPC exporter
- **Metrics**: Working via custom OTLP/gRPC exporter

**Implementation**:
- Custom OTLP exporters using gRPC C core library
- Patched protobuf-c to support proto3 optional fields (required for OTLP metrics proto)
- Background thread batches and exports metrics every 10 seconds

**Proto3 Optional Field Support**:
The OTLP metrics proto uses proto3 `optional` fields which protobuf-c doesn't natively support.
This was solved by patching protobuf-c's code generator to return `FEATURE_PROTO3_OPTIONAL`:

```cpp
// Added to protoc-c/c_generator.h CGenerator class
uint64_t GetSupportedFeatures() const override { return FEATURE_PROTO3_OPTIONAL; }
```

**Note**: While protobuf-c can now compile proto3 optional fields, it doesn't generate `has_*`
presence-tracking fields. The metrics exporter works around this by always setting values.

**Files**:
- `services/service-f/Dockerfile` - includes protobuf-c patch for proto3 optional
- `services/service-f/src/main.c` - uses all three OTLP exporters
- `services/service-f/src/otlp_exporter.c` - trace exporter
- `services/service-f/src/otlp_log_exporter.c` - log exporter
- `services/service-f/src/otlp_metrics_exporter.c` - metrics exporter

---

## Current Telemetry Coverage

| Service | Language | Traces | Logs | Metrics |
|---------|----------|--------|------|---------|
| service-a | Go | ✅ | ✅ | ✅ |
| service-b | Rust | ✅ | ✅ | ✅ |
| service-c | Python | ✅ | ✅ | ✅ |
| service-d | C# | ✅ | ✅ | ✅ |
| service-e | C++ | ✅ | ✅ | ✅ |
| service-f | C | ✅ | ✅ | ✅ |

**All services now have full observability coverage!**
