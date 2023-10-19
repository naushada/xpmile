FROM ubuntu:focal
ENV TZ=Asia/Calcutta
RUN ln -snf /usr/share/zoneinfo/$TZ /etc/localtime && echo $TZ > /etc/timezone
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get -y upgrade
RUN apt-get install -y --no-install-recommends \
    ca-cacert \
    cmake \
    build-essential \
    libboost-all-dev \
    libssl-dev \
    wget \
    zlib1g-dev

RUN apt-get update
RUN apt-get -y upgrade
RUN apt-get -y install libboost-all-dev
RUN apt-get -y install libbson-dev
RUN apt-get -y install libzstd-dev
RUN apt-get -y install git

#WORKDIR /root
# SSL
#RUN git clone -b OpenSSL_1_1_1-stable https://github.com/openssl/openssl.git
#WORKDIR /root/openssl
#RUN ./config --prefix=/usr/local/openssl-1.1.1-stable --openssldir=/usr/local/openssl-1.1.1-stable
#RUN make install


WORKDIR /root
RUN git clone https://github.com/google/googletest.git 
RUN cd googletest 
RUN mkdir build
RUN cd build
WORKDIR /root/googletest/build
RUN cmake ..
RUN make install && ldconfig

# get and build ACE
WORKDIR /root
RUN wget https://github.com/DOCGroup/ACE_TAO/releases/download/ACE%2BTAO-7_0_0/ACE+TAO-7.0.0.tar.gz
RUN tar -xzvf ACE+TAO-7.0.0.tar.gz
ENV ACE_SRC=/root/ACE_wrappers ACE_PREFIX=/usr/local/ACE_TAO-7.0.0
RUN echo '#include "ace/config-linux.h"' > ACE_wrappers/ace/config.h
RUN echo 'include $(ACE_SRC)/include/makeinclude/platform_linux.GNU' > $ACE_SRC/include/makeinclude/platform_macros.GNU
WORKDIR /root/ACE_wrappers
RUN make install ssl=1 INSTALL_PREFIX=${ACE_PREFIX} ACE_ROOT=${ACE_SRC} SSL_ROOT=/usr/include/opensll
RUN ldconfig

WORKDIR /root/mongo-c
#RUN apt-get -y install mongodb-server-core
RUN git clone -b r1.19 https://github.com/mongodb/mongo-c-driver.git

RUN cd mongo-c-driver
WORKDIR /root/mongo-c/mongo-c-driver/build
RUN cmake ..
RUN make && make install

WORKDIR /root/mongo-cxx
RUN git clone -b releases/v3.6 https://github.com/mongodb/mongo-cxx-driver.git
RUN cd mongo-cxx-driver

WORKDIR /root/mongo-cxx/mongo-cxx-driver/build
RUN cmake .. -DBSONCXX_POLY_USE_MNMLSTC=1 -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local
RUN make && make install
RUN ldconfig

WORKDIR /root
RUN git clone https://github.com/naushada/xpmile.git
WORKDIR /root/xpmile/backend
RUN mkdir build
WORKDIR /root/xpmile/backend/build
RUN cmake .. && make

#node installation
#FROM node:latest AS gui-build
RUN apt-get -y update
RUN apt-get -y upgrade
#RUN apt-get -y install build-essential
#RUN apt-get -y install nodejs npm

WORKDIR /root/xpmile/ui
#RUN mkdir webgui && cd webgui
#RUN mkdir webclient && cd webclient

#WORKDIR /root/webgui/webclient
#RUN git clone https://github.com/naushada/lms.git webui
#RUN cd webui


########## installing dependencies node_module ######################
RUN apt-get -y install curl && \
	curl -fsSL https://deb.nodesource.com/setup_lts.x | bash - && \
    	apt-get -y install nodejs
#RUN curl -sL https://deb.nodesource.com/setup_14.x | bash -
#RUN apt-get -y install nodejs

WORKDIR /root/xpmile/ui

RUN npm install
RUN npm update
######## copy some packages from local to container
##############################


##### Compile the Angular webgui #################
RUN npm install -g @angular/cli
#RUN ng add @angular/material
RUN npm install xlsx
RUN npm install file-saver
RUN npm install jspdf
RUN npm install jsbarcode
RUN npm install pdfmake
#RUN npm install @cds/angular --save
#RUN npm install @cds/react --save
#RUN ng add @clr/angular
RUN npm install @clr/icons @clr/angular @clr/ui @cds/core
ENV NODE_OPTIONS=--max_old_space_size=4096
WORKDIR /root/xpmile/ui
RUN ng build --configuration production --aot --base-href /webui/

RUN cd /opt
RUN mkdir xAPP
RUN cd xAPP
RUN mkdir webgui
RUN cd webgui
WORKDIR /opt/xAPP/webgui
RUN cp -r /root/xpmile/ui/webui/dist/ui .

WORKDIR /opt/xAPP
RUN mkdir granada
RUN cd granada
WORKDIR /opt/xAPP/granada

# copy from previoud build stage
RUN cp /root/granada/ix86_64x/uniservice .

# CMD_ARGS will be : --server-ip <ip> --server-port <port> --server-worker <number of worker> --mongo-db-name <name> --mongo-db-connection-pool <conn-pool> --mongo-db-uri <uri>
ENV ARGS="--server-worker 5"
ENV PORT=8080
CMD "/opt/xAPP/granada/uniservice" --server-port ${PORT} ${ARGS}
