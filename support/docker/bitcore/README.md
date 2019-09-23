This doc is about the Docker container for "flowee/bitcore".

Other Docker containers [in this repo](../README.md)

The Bitcore-proxy container requires both a flowee/hub and a flowee/indexer
to connect to for its backing data store. The advantage is that containers
for bitcore-proxy stay simple and small. No need for any volumes (storage).

As detailed in the [generic readme](../README.md), it is known to work that
you port-forward the API on services like *the Hub* and *Indexer* for
others to connect to it by nothing but the hosts network address. Either a
DNS entry, or a direct IP address. Use this in the *FLOWEE_HUB* env var
below.

A simple setup example:

    docker run -e FLOWEE_HUB=192.168.1.2 -d -p 3000:3000 \
            -e FLOWEE_INDEXER=192.168.1.2 flowee/bitcore-proxy

### Available options (env-vars)

* FLOWEE_INDEXER *list of indexers to enable. For instance: txid, address,
  spent. Default is just txid*

* FLOWEE_HUB *The hostname or IP and optionally port to connect to the Hub
  (see [hub](../hub/README.md) container*

* FLOWEE_LOGLEVEL *allows you to change the log-level. Recognized options
  are info, quiet or silent*

