FROM mcr.microsoft.com/dotnet/sdk:8.0 AS build
WORKDIR /src

# Copy proto files first (to match the relative path in csproj)
COPY proto/ ./proto/

# Copy project file
COPY services/service-d/ServiceD.csproj ./services/service-d/

# Copy source
COPY services/service-d/ ./services/service-d/

# Build from the service directory
WORKDIR /src/services/service-d
RUN dotnet restore && dotnet publish -c Release -o /app/publish

# Runtime image
FROM mcr.microsoft.com/dotnet/aspnet:8.0
WORKDIR /app

COPY --from=build /app/publish .

ENV ASPNETCORE_URLS=http://+:50054
ENV GRPC_PORT=50054
ENV ERROR_RATE=0.20
ENV OTEL_EXPORTER_OTLP_ENDPOINT=http://otel-collector:4317
ENV OTEL_SERVICE_NAME=service-d

EXPOSE 50054

ENTRYPOINT ["dotnet", "ServiceD.dll"]
