FROM maven:3.9-eclipse-temurin-17 AS builder

WORKDIR /build

# Cache dependencies first
COPY pom.xml .
RUN mvn dependency:go-offline -q

# Build (skips UI tests that need a browser; unit/WireMock tests still run)
COPY src ./src
COPY frontend ./frontend
RUN mvn package -Pproduction -q

# ---- Runtime image ----
FROM eclipse-temurin:17-jre

WORKDIR /app

COPY --from=builder /build/target/onprem-*.jar app.jar

# Backend URL — override at docker run time:
#   -e XPMILE_BACKEND_BASE_URL=https://marvel-3a78bd953f5f.herokuapp.com
#   -e XPMILE_BACKEND_BASE_URL=http://<on-prem-host>:8080
ENV XPMILE_BACKEND_BASE_URL=http://localhost:8080

EXPOSE 8090

ENTRYPOINT ["sh", "-c", "java -jar app.jar --xpmile.backend.base-url=${XPMILE_BACKEND_BASE_URL}"]
