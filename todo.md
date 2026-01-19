# Observability TODO

## Outstanding Items

### Service E (C++) - Missing All Telemetry

- **Traces**: Not exporting to Jaeger
- **Logs**: Not exporting to Elasticsearch
- **Metrics**: Not exporting to Prometheus

**Root Cause**: The Dockerfile uses `main_simple.cpp` which has no OpenTelemetry instrumentation. The full `main.cpp` exists with OTel support but requires building the OpenTelemetry C++ SDK from source.

**Fix Required**:
1. Update Dockerfile to build OpenTelemetry C++ SDK (complex, multi-hour build)
2. Switch from `main_simple.cpp` to `main.cpp`
3. Link against OTel C++ libraries

**Files**:
- `services/service-e/Dockerfile` - needs OTel C++ SDK build steps
- `services/service-e/main.cpp` - has OTel code, currently unused

---

### Service F (C) - Missing Logs and Metrics

- **Traces**: Working (custom OTLP exporter)
- **Logs**: Outputs to stdout in JSON format with trace context, but not OTLP
- **Metrics**: Not implemented

**Root Cause**: Pure C implementation with no official OpenTelemetry C SDK for logs/metrics.

**Fix Options**:
1. Add sidecar log collector (e.g., Fluent Bit) to forward stdout logs to OTel collector
2. Implement custom OTLP log exporter in C (similar to existing trace exporter)
3. Rewrite in a language with better OTel support

**Files**:
- `services/service-f/src/main.c` - has JSON logging with trace context
- `services/service-f/src/otlp_exporter.c` - custom trace exporter (could extend for logs)

---

## Current Telemetry Coverage

| Service | Language | Traces | Logs | Metrics |
|---------|----------|--------|------|---------|
| service-a | Go | ✅ | ✅ | ✅ |
| service-b | Rust | ✅ | ✅ | ✅ |
| service-c | Python | ✅ | ✅ | ✅ |
| service-d | C# | ✅ | ✅ | ✅ |
| service-e | C++ | ❌ | ❌ | ❌ |
| service-f | C | ✅ | ❌ | ❌ |
