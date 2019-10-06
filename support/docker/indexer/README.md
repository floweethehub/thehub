This doc is about the Docker container for "flowee/indexer".

More docs;  [online](https://flowee.org/indexer)

Other Docker containers [in this repo](../README.md)

The Indexer container requires a flowee/hub container to connect to in
order to get populated and updated as new data comes in.

As detailed in the [generic readme](../README.md), it is known to work that
you port-forward the API on services like *the Hub* and *Indexer* for
others to connect to it by nothing but the hosts network address. Either a
DNS entry, or a direct IP address. Use this in the *FLOWEE_HUB* env var
below.

### Some examples;

**Simple standard setup, only TXID indexer**

    docker volume create flowee_indexer
    docker run -e FLOWEE_HUB=192.168.0.2 -d -v flowee_indexer:/data -p 1234:1234 flowee/indexer

**Specify the indexers to run:**

Notice that adding an indexer while keeping the volume the same will just
make that one indexer 'catch-up' first.

    docker volume create flowee_indexer # if not already done before
    docker run -e FLOWEE_HUB=192.168.0.2 \
        -e FLOWEE_INDEXERS="txid, spentDB"
        -d -v flowee_indexer:/data -p 1234:1234 flowee/indexer

**Running the address indexer against Postgres:**

    # first start and run postgres
    docker volume create flowee_addressDB
    docker run -v flowee_addressDB:/var/lib/postgresql/data \
        -e POSTGRES_USER=flowee \
        -e POSTGRES_PASSWORD=SECRET \
        -d postgres

    # Ask docker which IP it assigned to the postgres container;
    # The previous command printed a long ID.
    docker inspect ID | grep IPAddress
    # Replace the following 172.17.0.6 with the response IP

    docker volume create flowee_indexer # if not already done before
    docker run -e FLOWEE_HUB=192.168.0.2 \
        -e FLOWEE_INDEXERS="txid, spentDB, addressDB"
        -e FLOWEE_DB="driver=postgres,hostname=172.17.0.6,db=flowee,username=flowee,password=SECRET"
        -d -v flowee_indexer:/data -p 1234:1234 flowee/indexer


### Available options (env-vars)

* FLOWEE_INDEXERS *list of indexers to enable. For instance: txid, address,
  spent. Default is just txid*

* FLOWEE_DB *comma separated list of configs to connect to the SQL Database
  service. We support driver, hostname, db, username and password.*
example:

        driver=postgres,hostname=172.17.0.10,db=flowee,username=flowee_indexer,password=SECRET

* FLOWEE_HUB *The hostname or IP and optionally port to connect to the Hub
  (see [hub](../hub/README.md) container*)

* FLOWEE_LOGLEVEL *allows you to change the log-level. Recognized options
  are info, quiet or silent*

