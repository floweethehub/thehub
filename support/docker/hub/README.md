This is about the docker file created for Flowee the Hub.

Flowee the Hub provides a lot of services and the most convenient way to
run, maintain and expand your service level is by marrying Flowee with
dockers.

Both in Flowee as well as in Docker there is a way of thinking where you
can start one unit (in most cases the Hub) and it becomes easy to connect
this to with services or to connect your application to them.

Starting it;
here I'll demonstrate how you can run Flowee the Hub as a single docker
your docker server. Please refer to docker.com for more info if you want to
do service-duplication, fail-over and other cloud-stack setups.


```
  docker volume create blockdata
  docker run \
    --name hubtest \
    --mount source=blockdata,target=/data \
    flowee/hub:master
```

What this does is the following;
You create a docker volume. These store the data that you want to keep
between docker restarts and data you want to share with the main system.

The `blockdata` source dir contains the block data. Please notice that this will
contain all historical data. Make sure you have some 200GB available for
it.

the "blockdata" dir will contain directories like `blocks`, which hold
the actual historical data (180GB or so) and a very important directory is
the `unspent` subdir that holds the UTXO database data (15GB or so).  
For performance reasons it is highly recommended to put the `unspent` dir
on fast storage. Like an SSD. For instance by using `ln` to symlink it.

# Remote-control of Flowee the Hub

We ship the `hub-cli` application inside the docker. This means that you
can execute it in a running docker container quite simply. Replace the
example hubtest from the command below with your local docker name. You
can find that name using `docker container ls` to get the ID.

```
  docker container exec hubtest hub-cli help
```
