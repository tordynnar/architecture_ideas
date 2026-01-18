FROM python:3.11-slim

WORKDIR /app

# Install dependencies
COPY services/service-c/requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

# Copy proto files and generate Python code
COPY proto/ ./proto/
RUN python -m grpc_tools.protoc \
    -I./proto \
    --python_out=. \
    --grpc_python_out=. \
    ./proto/common.proto ./proto/services.proto

# Copy application code
COPY services/service-c/main.py .

ENV GRPC_PORT=50053
ENV SERVICE_D_ADDR=service-d:50054
ENV SERVICE_F_ADDR=service-f:50056
ENV OTEL_EXPORTER_OTLP_ENDPOINT=http://otel-collector:4317
ENV OTEL_SERVICE_NAME=service-c

EXPOSE 50053

CMD ["python", "main.py"]
