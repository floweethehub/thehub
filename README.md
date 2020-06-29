Flowee the Hub repository
=========================

[![pipeline status](https://gitlab.com/FloweeTheHub/thehub/badges/master/pipeline.svg)](https://gitlab.com/FloweeTheHub/thehub/commits/master)


# Flowee is a series of applications to get the most out of Bitcoin Cash (BCH)

See more details on https://flowee.org/about/


This repository contains

* the Hub: the headless (server) Bitcoin Cash node software
* Indexer: Provides various data-stores to speed up lookups on the blockchain.
* Libraries shared between many of the Flowee applications.
* hub-cli: the command-line interface to the Hub server.
* bitcore-proxy: a client of hub+indexer to generate the bitcore APIs
* txVulcano: Transaction generator testing application.
* unspentdb: Application to introspect and optimize your UTXO database.
* Hub-qt: a test (gui) version of the Hub
* pos / cashier: a beta version of the point-of-sale application.

# Building a Hub that connects you to Bitcoin Cash

Bitcoin Cash is still a mysterious black box to most people and companies
that would potentially want to use it, or build applications with.

Flowee has been built to help you use Bitcoin Cash. Bring you a platform to
stand on when you simply use or when you build with Bitcoin Cash.

The codebase is derived from the one that originally was created by Satoshi
Nakamoto, the inventor of Bitcoin. This way you know you won't get
compatibility issues. Flowee is adjusted for greatly increased speed and
reliability as well as for scaling up to much larger blocks than the
competition.

# Installation

Binary packages are build by the continues integration system from GitLab.
Both debian packages and a simple zip file with executables are available
as artifacts for each 'pipeline':

https://gitlab.com/FloweeTheHub/thehub/-/pipelines?scope=branches

There is an ArchLinux AUR here: https://aur.archlinux.org/packages/flowee/

The simplest way to compile is by doing so in a docker container:
See more details [here](support/docker/hub).

**To compile and install Flowee** on Ubuntu, install the dependencies

`sudo apt install libevent-dev libboost-all-dev libminiupnpc-dev qt5-default libprotobuf-dev pkgconf`

To compile and install Flowee on MacOS, install the dependencies

`brew install cmake libevent boost miniupnpc qt pkg-config`

Then clone the repo and use cmake to create the makefile

```
mkdir thehub/build
cd thehub/build
cmake CMakeLists.txt ..
make
make install
```

The fastest way to try Flowee is by running a pre-compiled docker
container:
https://flowee.org/docs/deployment/


# More details

* https://flowee.org
* https://gitlab.com/FloweeTheHub/thehub

# Social

* Twitter: https://twitter.com/floweethehub
