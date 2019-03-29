FROM debian:stretch-slim
RUN groupadd -r flowee && useradd -r -m -d /etc/flowee -g flowee flowee && \
    mkdir -pm 775 /etc/flowee && chown -R flowee:flowee /etc/flowee && \
    mkdir -pm 750 /data && chown -R flowee:flowee /data
ADD --chown=flowee:flowee conf /etc/flowee
ADD --chown=flowee:flowee bin /usr/bin

USER flowee
CMD ["/usr/bin/hub"]

EXPOSE 8332 8334 18332 18333 1235 11235
LABEL maintainer="Tom Zander <tomz@freedommail.ch>"

