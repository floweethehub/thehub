FROM debian:stable
LABEL maintainer="Tom Zander <tom@flowee.org>"
RUN apt-get -y update && apt-get install -y --no-install-recommends \
    libboost-chrono1.67.0 \
    libboost-iostreams1.67.0 \
    libboost-filesystem1.67.0 \
    libboost-atomic1.67.0 \
    libboost-thread1.67.0 \
    libboost-regex1.67.0 \
    libboost-date-time1.67.0 \
    libboost-program-options1.67.0 \
    libevent-2.1-6 \
    libevent-pthreads-2.1-6 \
    libczmq4 \
    libminiupnpc17 \
    libqt5network5 && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/*

ADD bin /usr/bin
CMD ["/usr/bin/startHub"]

EXPOSE 8332 8334 18332 18333 1235 11235
