FROM debian:stable
LABEL maintainer="Tom Zander <tomz@freedommail.ch>"
RUN apt-get -y update && apt-get install -y --no-install-recommends \
    libboost-chrono1.67.0 \
    libboost-iostreams1.67.0 \
    libboost-filesystem1.67.0 \
    libboost-atomic1.67.0 \
    libboost-thread1.67.0 \
    libboost-regex1.67.0 \
    libboost-date-time1.67.0 \
    libboost-program-options1.67.0 \
    libqt5network5 && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/*

ADD bin /usr/bin
CMD ["/usr/bin/startBitcoreProxy"]
