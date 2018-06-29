This is about the docker file created for Flowee the Hub.

Flowee the Hub provides a lot of services and the most convenient way to
run, maintain and expand your service level is by marrying Flowee with
dockers.

Both in Flowee as well as in Docker there is a way of thinking where you
can start one unit (in this case the Hub) and it becomes easy to connect
this to other services or to make the Flowee the Hub be a service for your
application.

Starting it;
here I'll demonstrate how you can run Flowee the Hub as a single docker on
a local machine. Please refer to docker.com for more info if you want to
do service-duplication, fail-over and other cloud-stack setups.


```
  docker volume create cookies
  docker volume create blockdata
  docker run \
    --name hubtest \
    --mount source=cookies,target=/etc/flowee/cookies \
    --mount source=blockdata,target=/data \
    hub:master

```

What this does is the following;
You create two docker volumes. These store the data that you want to keep
between docker restarts and data you want to share with other dockers.
The `cookies` volume will contain the authentication cookies that other
dockers need to login and use the APIs of Flowee the Hub.

The `blockdata` dir contains the block data. Please notice that this will
contain all historical data. Make sure you have some 200GB available for
it.

# Remote-control of Flowee the Hub

We ship the `hub-cli` application inside the docker. This means that you
can execute it in a running docker container quite simply. If your docker
isn't named 'hubtest'. Use `docker container ls` to get the ID.


```
  docker container exec hubtest hub-cli help
```

From your own docker application you should also mount the cookies volume
which is a required piece of info allowing usage of the API or the (legacy)
RPC interfaces.

