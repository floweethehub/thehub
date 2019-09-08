This is about the Docker images created for Flowee

* [the Hub](hub/README.md) ([online](https://hub.docker.com/r/flowee/hub))
* [Indexer](indexer/README.md) ([online](https://hub.docker.com/r/flowee/indexer))
* [pos-cli](indexer/pos-cli) ([online](https://hub.docker.com/r/flowee/pos-cli))

Flowee the Hub provides a lot of services and the most convenient way to
run, maintain and expand your service level is by marrying Flowee with
[Docker](https://docker.com) containers.

Both in Flowee as well as in Docker there is a way of thinking where you
can start one unit and it becomes easy to connect this to with services or to
connect your application to them.

Additionally, adding new instances (called containers) of the application
is made simple using the Docker user interface or one of its front-ends
(like [portainer](http://portainer.io)).


## Flowee services network together

The strength of Flowee applications comes from deploying the set of
services you need, each in its own Docker container, and networking them
together. You can start a new indexer anywhere on your network and as such
making great uptime one step closer.

Networking in Docker needs a bit of understanding. The main mistake made is
to use localhost for connecting to another container just because they are
running on the same host machine.

You need to understand that each Docker container is like a virtual machine
and 'localhost' is completely local to one container. So your Indexer's
localhost is a different one from your Hub's localhost.

The known to work approach is for each Flowee container (hub, indexer, etc)
to expose the API as a port on your host machine and then let others
connect to your host machine. Either directly on the current IP address, or
if your host machine is reachable by (local) DNS, use that name.

If you use a name make sure it actually resolves using DNS, not via your
hosts config (/etc/hosts) as that is not available to docker containers.


Lets run through an example;

For instance your machine is called *DockerServer* and has IP address
192.168.0.2 on your subnet. Other machines in your subnet can 'ping' your
machine using *DockerServer* as a name.

Starting Hub with the docker argument `-p 1235:1245` makes its API service
available for all machines on the subnet, including all docker containers.

Starting Indexer with the docker argument `-e FLOWEE_HUB=DockerServer` will
make the indexer connect properly.

If your IP is stable, you can try to pass the IP address instead:
`-e FLOWEE_HUB=192.168.0.2`.

