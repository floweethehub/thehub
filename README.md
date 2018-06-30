Flowee the Hub repository
=========================

[![pipeline status](https://gitlab.com/FloweeTheHub/thehub/badges/master/pipeline.svg)](https://gitlab.com/FloweeTheHub/thehub/commits/master)

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

# Installation

Flowee is still being developed and is usage in production will cause you pain.

The fastest way to try Flowee is by installing docker. See more details [here](support/docker/hub).

To compile and install Flowee on Ubuntu, install the dependencies

`sudo apt install libevent-dev libboost-all-dev libminiupnpc-dev qt5-default protobuf-compiler`

Then clone the repo and use cmake to create the makefile

`cmake CMakeLists.txt`

`make`

`make install`

# More details

* http://www.flowee.org
* https://gitlab.com/FloweeTheHub/thehub

# Social

* Twitter: https://twitter.com/floweethehub
* Discord: https://discord.gg/KF38tTA
* Email group: https://groups.google.com/d/forum/flowee
