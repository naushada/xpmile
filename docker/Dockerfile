# ============================================================
# Stage 1: Build C++ backend
# ============================================================
FROM ubuntu:focal AS cpp-builder

ENV TZ=Asia/Calcutta DEBIAN_FRONTEND=noninteractive
RUN ln -snf /usr/share/zoneinfo/$TZ /etc/localtime && echo $TZ > /etc/timezone

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    cmake \
    build-essential \
    libboost-all-dev \
    libssl-dev \
    libzstd-dev \
    libsasl2-dev \
    zlib1g-dev \
    wget \
    git \
    && rm -rf /var/lib/apt/lists/*

# Build ACE/TAO
ENV ACE_SRC=/root/ACE_wrappers ACE_PREFIX=/usr/local/ACE_TAO-7.0.0
WORKDIR /root
RUN wget -q https://github.com/DOCGroup/ACE_TAO/releases/download/ACE%2BTAO-7_0_0/ACE+TAO-7.0.0.tar.gz \
    && tar -xzf ACE+TAO-7.0.0.tar.gz \
    && rm ACE+TAO-7.0.0.tar.gz \
    && echo '#include "ace/config-linux.h"' > /root/ACE_wrappers/ace/config.h \
    && echo 'include $(ACE_SRC)/include/makeinclude/platform_linux.GNU' \
          > /root/ACE_wrappers/include/makeinclude/platform_macros.GNU
WORKDIR /root/ACE_wrappers
RUN make install ssl=1 INSTALL_PREFIX=${ACE_PREFIX} ACE_ROOT=${ACE_SRC} SSL_ROOT=/usr/include/openssl \
    && ldconfig

# Build mongo-c-driver
WORKDIR /root
RUN git clone -b 1.19.1 --depth 1 https://github.com/mongodb/mongo-c-driver.git mongo-c-driver
WORKDIR /root/mongo-c-driver/build
RUN cmake .. -DCMAKE_BUILD_TYPE=Release \
             -DCMAKE_INSTALL_PREFIX=/usr/local \
             -DENABLE_TESTS=OFF \
             -DENABLE_EXAMPLES=OFF \
    && make -j$(nproc) && make install && ldconfig

# Build mongo-cxx-driver
WORKDIR /root
RUN git clone -b releases/v3.6 --depth 1 https://github.com/mongodb/mongo-cxx-driver.git mongo-cxx-driver
WORKDIR /root/mongo-cxx-driver/build
RUN cmake .. \
    -DBSONCXX_POLY_USE_MNMLSTC=1 \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr/local \
    -DBUILD_TESTING=OFF \
    && make -j2 && make install && ldconfig

# Build googletest
WORKDIR /root
RUN git clone --depth 1 https://github.com/google/googletest.git
WORKDIR /root/googletest/build
RUN cmake .. && make -j$(nproc) install && ldconfig

# Build uniservice
# -j2: webservice.cc is large enough to OOM the builder at full nproc
# -fconcepts: suppresses the set_state(auto) warning from emailservice.hpp
COPY backend/ /src/backend/
WORKDIR /src/backend/build
RUN cmake .. -DCMAKE_CXX_FLAGS="-fconcepts" && make -j2

# ============================================================
# Stage 2: Build Angular UI
# ============================================================
FROM node:18-alpine AS ui-builder
ENV NG_BUILD_MAX_WORKERS=1
ENV NODE_OPTIONS=--max_old_space_size=1536

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
COPY --from=cpp-builder /src/backend/build/uniservice /opt/xAPP/granada/uniservice

# Angular distribution
RUN mkdir -p /opt/xAPP/webgui
COPY --from=ui-builder /ui/dist/webui /opt/xAPP/webgui/webui

# Set CWD so that relative paths ../webgui/webui/ resolve correctly
WORKDIR /opt/xAPP/granada

# CMD_ARGS: --server-ip <ip> --server-port <port> --server-worker <n>
#           --mongo-db-name <name> --mongo-db-connection-pool <n> --mongo-db-uri <uri>
ENV ARGS="--server-worker 5"
ENV PORT=8080

CMD /opt/xAPP/granada/uniservice --server-port ${PORT} ${ARGS}
