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

# Phase E (SAML) XML-DSig needs an XML library. apt packages, not
# compiled-from-source, so no bootstrap rebuild is required.
# -o Acquire::Check-Date=false tolerates build-host clock skew (the repo
# Release files still read as "not valid yet" otherwise). Packages stay
# GPG- and hash-verified; only the Release-file freshness check is relaxed.
RUN apt-get -o Acquire::Check-Date=false update \
 && apt-get install -y --no-install-recommends \
        pkg-config libxml2-dev libxmlsec1-dev libxmlsec1-openssl \
 && rm -rf /var/lib/apt/lists/*

# Build uniservice
# -j2: webservice.cpp is large enough that full parallelism OOMs the builder
# -fconcepts: suppresses the set_state(auto) warning from emailservice.hpp
COPY CMakeLists.txt /src/
COPY modules/ /src/modules/
COPY test/ /src/test/
COPY docker/mongo-init.js /src/docker/

# Mint a fresh CA + server + client cert family per build (mirrors
# onprem-pbx Dockerfile.cloud). No host-side ca.key required — the
# previous pattern needed certs/ca.key for `openssl x509 -req -CAkey`,
# but ca.key is gitignored, so CI never had it. Now the CA is born
# inside this layer and ca.key is purged at the end of the same RUN
# so it never lands in any image filesystem.
#
# Output:
#   /src/certs/        — ca.crt + server.{crt,key}   (baked into runtime: /opt/xAPP/granada/certs/)
#   /src/agent-certs/  — ca.crt + client.{crt,key}   (staged for wsdbagent rotation; future
#                                                     deploy-heroku.sh cmd_extract_agent_certs
#                                                     pulls these out of the built image)
#
# Per-build CA → wsdbagent's trust anchor rotates with every deploy.
# Customers running an older ca.crt will fail TLS verification until
# their cert pair is refreshed (extract step + sidecar watcher land
# in follow-up work).
RUN mkdir -p /src/certs /src/agent-certs && cd /tmp && \
    openssl genrsa -out ca.key 4096 && \
    openssl req -new -x509 -days 3650 -key ca.key -out ca.crt \
        -subj "/O=xpmile/CN=xpmile-CA" && \
    openssl genrsa -out server.key 4096 && \
    openssl req -new -key server.key -out server.csr \
        -subj "/O=xpmile/CN=marvel.xpmile.com" && \
    openssl x509 -req -days 3650 -in server.csr \
        -CA ca.crt -CAkey ca.key -CAcreateserial -out server.crt && \
    openssl genrsa -out client.key 4096 && \
    openssl req -new -key client.key -out client.csr \
        -subj "/O=xpmile/CN=wsdbagent" && \
    openssl x509 -req -days 3650 -in client.csr \
        -CA ca.crt -CAkey ca.key -CAcreateserial -out client.crt && \
    cp ca.crt /src/certs/ca.crt && \
    mv server.crt server.key /src/certs/ && \
    cp ca.crt /src/agent-certs/ca.crt && \
    mv client.crt client.key /src/agent-certs/ && \
    rm -f ca.key ca.srl server.csr client.csr

WORKDIR /src/build
RUN cmake .. -DCMAKE_CXX_FLAGS="-fconcepts" && make -j2

# ============================================================
# Stage 2: Build Angular UI
# ============================================================
FROM docker.io/library/node:18-alpine AS ui-builder
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
# Stage 2b: Build the IdP login portal — ui-idp
# ============================================================
# Same node base, separate WORKDIR so `npm ci` doesn't collide with
# ui/. Built under `--base-href /idp/` so the SPA's <base href> +
# all asset URLs sit under /idp/* (mirrors the /webui/ pattern for
# marvel). No package-lock yet — first build will resolve from
# package.json; lockfile committed in a follow-up after a real
# `npm install` produces one.
#
# Per design Q1 (same image to both Heroku apps), this stage runs
# on EVERY build — both marvel and idp dynos ship the same image
# with both SPAs baked in. Marvel users never navigate to /idp/*,
# idp users never navigate to /webui/* — no conflict.
FROM docker.io/library/node:18-alpine AS ui-idp-builder
ENV NG_BUILD_MAX_WORKERS=1
ENV NODE_OPTIONS=--max_old_space_size=1024
ARG UI_BUST=0

WORKDIR /ui-idp
COPY ui-idp/package.json ./
# No package-lock.json yet — use `npm install` (slower, less reproducible)
# until the first build commits one. Switch to `npm ci` afterwards.
RUN npm install --no-audit --no-fund

COPY ui-idp/ ./
RUN npx ng build --configuration production --base-href /idp/ --progress=false

# ============================================================
# Stage 3: Minimal runtime image
# ============================================================
FROM docker.io/library/ubuntu:focal AS runtime

ENV TZ=Asia/Calcutta DEBIAN_FRONTEND=noninteractive
RUN ln -snf /usr/share/zoneinfo/$TZ /etc/localtime && echo $TZ > /etc/timezone

RUN apt-get -o Acquire::Check-Date=false update \
 && apt-get install -y --no-install-recommends \
    ca-certificates \
    libssl1.1 \
    libboost-filesystem1.71.0 \
    libboost-system1.71.0 \
    libboost-thread1.71.0 \
    libzstd1 \
    libsasl2-2 \
    zlib1g \
    libicu66 \
    libxml2 \
    libxmlsec1 \
    libxmlsec1-openssl \
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

# InnerTLS material baked into the runtime image. server.{crt,key}
# + ca.crt is what uniservice serves on the inner TLS port. The
# matching client cert family (ca.crt + client.{crt,key}) is staged
# at /opt/xAPP/granada/agent-certs/ so a future
# `deploy-heroku.sh cmd_extract_agent_certs` can `podman cp` it out
# of the built image for the on-prem wsdbagent stack.
RUN mkdir -p /opt/xAPP/granada/certs /opt/xAPP/granada/agent-certs
COPY --from=cpp-builder /src/certs/       /opt/xAPP/granada/certs/
COPY --from=cpp-builder /src/agent-certs/ /opt/xAPP/granada/agent-certs/

# Angular distributions — marvel SPA + IdP login portal.
# Same image deploys to both Heroku apps; each dyno effectively
# uses one of these two trees (marvel → /webui/, idp → /idp/).
RUN mkdir -p /opt/xAPP/webgui
COPY --from=ui-builder     /ui/dist/webui    /opt/xAPP/webgui/webui
COPY --from=ui-idp-builder /ui-idp/dist/ui-idp /opt/xAPP/webgui/idp

# Set CWD so that relative paths ../webgui/webui/ resolve correctly
WORKDIR /opt/xAPP/granada

# CMD_ARGS: --server-ip <ip> --server-port <port> --server-worker <n>
#           --mongo-db-name <name> --mongo-db-connection-pool <n> --mongo-db-uri <uri>
# Set REMOTE_DB=1 in the container environment to enable --remote-db mode.
ENV ARGS="--server-worker 5 --tls-cert /opt/xAPP/granada/certs/server.crt --tls-key /opt/xAPP/granada/certs/server.key --tls-ca /opt/xAPP/granada/certs/ca.crt"
ENV PORT=8080
ENV REMOTE_DB=""

CMD /opt/xAPP/granada/uniservice --server-port ${PORT} ${ARGS} ${REMOTE_DB:+--remote-db}
