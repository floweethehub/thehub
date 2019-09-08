Flowee the Hub repository
=========================

[![pipeline status](https://gitlab.com/FloweeTheHub/thehub/badges/master/pipeline.svg)](https://gitlab.com/FloweeTheHub/thehub/commits/master)


# Flowee is a series of applications to get the most out of Bitcoin Cash (BCH)

See more details on https://flowee.org/about/


This repository contains

* the Hub: the headless (server) Bitcoin Cash node software
* Hub-qt: a GUI version of the Hub
* Indexer: Provides various data-stores to speed up lookups on the blockchain.
* pos / cachier: a beta version of the point-of-sale application.
* Libraries shared between many of the Flowee applications.

# Building a Hub that connects you to Bitcoin Cash

Bitcoin Cash is
still a mysterious black box to most companies that would potentially want
to use it, or build applications on top of.

Flowee is being built to help you connect to the Bitcoin Cash network and handle all
the validation, parsing and low level database access for all your Bitcoin
needs. Including many needs you didn't even know you had!

The codebase is derived from the one that originally was created by Satoshi
Nakamoto, the inventor of Bitcoin. This way you know you won't get
compatibility issues. Flowee is adjusted for greatly increased speed and
reliability as well as for scaling up to much larger blocks than the
competition.

The libs/httpengine directory is LGPL licensed as this is derived from the
qhttpengine open source project.

The original Satoshi codebase was MIT licensed. This has been combined with
copyrighted works from Tom Zander which are GPLv3 licensed with the result
that those files are new licensed under the GPLv3.

# Installation

The fastest way to try Flowee is by installing docker. See more details [here](support/docker/hub).

To compile and install Flowee on Ubuntu, install the dependencies

`sudo apt install libevent-dev libboost-all-dev libminiupnpc-dev qt5-default protobuf-compiler libprotobuf-dev pkgconf`

To compile and install Flowee on MacOS, install the dependencies

`brew install cmake libevent boost miniupnpc qt protobuf pkg-config`

Then clone the repo and use cmake to create the makefile

```
mkdir thehub/build
cd thehub/build
cmake CMakeLists.txt ..
make
make install
```

# More details

* https://flowee.org
* https://gitlab.com/FloweeTheHub/thehub

# Social

* Twitter: https://twitter.com/floweethehub
