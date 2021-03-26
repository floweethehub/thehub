FROM debian:stable
LABEL maintainer="Tom Zander <tom@flowee.org>"
RUN apt-get -y update && apt-get install -y --no-install-recommends \
    libssl1.1 && \
   apt-get clean && \
   rm -rf /var/lib/apt/lists/*

ADD bin /usr/bin
ENTRYPOINT ["/usr/bin/pos"]
