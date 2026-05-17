# ============================================================
# Stage 1: Build C++ backend (uniservice)
# ============================================================
# BUILDER_IMAGE overrides the FROM tag so the same Dockerfile works in
# three contexts:
#   - dev:  localhost/xpmile-cpp-builder:bootstrap   (built by run.sh /
#                                                     deploy-heroku.sh)
#   - CI:   docker.io/naushada/xpmile-cpp-builder:bootstrap (multi-arch
#                                                            image the
#                                                            publish
#                                                            workflow
#                                                            builds first)
#   - prod: same as CI — pulled from Docker Hub if not present locally
ARG BUILDER_IMAGE=localhost/xpmile-cpp-builder:bootstrap
FROM ${BUILDER_IMAGE} AS cpp-builder

# Build uniservice
# -j2: webservice.cpp is large enough that full parallelism OOMs the builder
# -fconcepts: suppresses the set_state(auto) warning from emailservice.hpp
COPY CMakeLists.txt /src/
COPY modules/ /src/modules/
COPY test/ /src/test/
COPY docker/mongo-init.js /src/docker/

# Copy repo certs and generate Heroku server cert from the same CA.
# The CA (ca.crt / ca.key) lives in the repo so wsdbagent can verify the
# server using a committed trust anchor.
COPY certs/ /tmp/repo-certs/
RUN mkdir -p /src/certs && \
    openssl genrsa -out /src/certs/server.key 2048 && \
    openssl req -new -key /src/certs/server.key \
        -out /src/certs/server.csr \
        -subj "/O=xpmile/CN=marvel.xpmile.com" && \
    openssl x509 -req -days 365 -in /src/certs/server.csr \
        -CA /tmp/repo-certs/ca.crt -CAkey /tmp/repo-certs/ca.key -CAcreateserial \
        -out /src/certs/server.crt && \
    cp /tmp/repo-certs/ca.crt /src/certs/ca.crt && \
    rm -f /src/certs/server.csr /src/certs/ca.srl /tmp/repo-certs/ca.key

WORKDIR /src/build
RUN cmake .. -DCMAKE_CXX_FLAGS="-fconcepts" && make -j2

# ============================================================
# Stage 2: Build Angular UI
# ============================================================
FROM node:18-alpine AS ui-builder
ENV NG_BUILD_MAX_WORKERS=1
ENV NODE_OPTIONS=--max_old_space_size=1536

# Pass --build-arg UI_BUST=$(date +%s) to force this stage to re-run
# without invalidating the slower C++ stage above.
ARG UI_BUST=0

WORKDIR /ui
# Copy manifests first so npm install layer is cached separately from source
COPY ui/package.json ui/package-lock.json ./
RUN npm ci

COPY ui/ ./
RUN npx ng build --configuration production --aot --base-href /webui/ --progress=false --optimization=false --build-optimizer=false --extract-licenses=false

# ============================================================
# Stage 3: Minimal runtime image
# ============================================================
FROM ubuntu:focal AS runtime

ENV TZ=Asia/Calcutta DEBIAN_FRONTEND=noninteractive
RUN ln -snf /usr/share/zoneinfo/$TZ /etc/localtime && echo $TZ > /etc/timezone

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    libssl1.1 \
    libboost-filesystem1.71.0 \
    libboost-system1.71.0 \
    libboost-thread1.71.0 \
    libzstd1 \
    libsasl2-2 \
    zlib1g \
    libicu66 \
    && rm -rf /var/lib/apt/lists/*

# ACE/TAO shared libraries (installed at non-standard prefix)
COPY --from=cpp-builder /usr/local/ACE_TAO-7.0.0/lib/ /usr/local/ACE_TAO-7.0.0/lib/
RUN echo "/usr/local/ACE_TAO-7.0.0/lib" > /etc/ld.so.conf.d/ace-tao.conf

# MongoDB C/C++ driver shared libraries
COPY --from=cpp-builder /usr/local/lib/libmongoc-1.0.so* /usr/local/lib/
COPY --from=cpp-builder /usr/local/lib/libbson-1.0.so* /usr/local/lib/
COPY --from=cpp-builder /usr/local/lib/libmongocxx.so* /usr/local/lib/
COPY --from=cpp-builder /usr/local/lib/libbsoncxx.so* /usr/local/lib/

RUN ldconfig

# Application binary
RUN mkdir -p /opt/xAPP/granada
COPY --from=cpp-builder /src/build/uniservice /opt/xAPP/granada/uniservice

# Self-signed certs for inner TLS (generated in cpp-builder stage)
RUN mkdir -p /opt/xAPP/granada/certs
COPY --from=cpp-builder /src/certs/ /opt/xAPP/granada/certs/

# Angular distribution
RUN mkdir -p /opt/xAPP/webgui
COPY --from=ui-builder /ui/dist/webui /opt/xAPP/webgui/webui

# Set CWD so that relative paths ../webgui/webui/ resolve correctly
WORKDIR /opt/xAPP/granada

# CMD_ARGS: --server-ip <ip> --server-port <port> --server-worker <n>
#           --mongo-db-name <name> --mongo-db-connection-pool <n> --mongo-db-uri <uri>
# Set REMOTE_DB=1 in the container environment to enable --remote-db mode.
ENV ARGS="--server-worker 5 --tls-cert /opt/xAPP/granada/certs/server.crt --tls-key /opt/xAPP/granada/certs/server.key --tls-ca /opt/xAPP/granada/certs/ca.crt"
ENV PORT=8080
ENV REMOTE_DB=""

CMD /opt/xAPP/granada/uniservice --server-port ${PORT} ${ARGS} ${REMOTE_DB:+--remote-db}
