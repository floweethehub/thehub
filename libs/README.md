# Flowee libs

There are a series of libraries that are shipped by Flowee, which are all
in production by one or more of its applications.

Most of these libraries are also installed and shipped by deb/aur packages.
You can use them from cmake.


* crypto  
    Basic crypto primitives. AES/SHA256.
* interfaces  
    Provides enums and interfaces.
* utils  
    This is the most useful one, lots of Bitcoin Cash util classes.
* networkmanager  
    A series of classes to do network communications with BCH and/or Flowee apps.
* p2p  
    A specific set of libs that implements the BCH p2p network communications.
* utxo  
   This is a library that supplies a sha256 specific database.

* httpengine  
    A webserver based on QtNetwork libs.
* apputils  
    Standalone-apps utils, depends on Qt libs.

**Internal libraries**:

Only used by some apps in this repo, they are not installed on `make install`.

* server  
    All the classes shared between hub/hub-qt/hub-cli that didn't make it to a lib.
* api  
    A network-manager extension that implements the Hub APIs.

### Stability

While Flowee is in major version `1` the libraries are shipped to be
statically linked because while the code is stable and known to work,
the APIs can be changed in a binary incompatible and (much rarer)
source incompatible way.
Users are isolated from binary incompatible changes this way.

We have a [developer-notes](../support/developer-notes.md) doc which
specifies a coding style, which is at this time mostly used for new code
and a wish for old code. Many APIs have been written and have been stable
for a lot of years and they may not at this time follow those coding
guidelines. We apologize for this and point out that slow changes and
stability are more important than strict confirmation to some rules.
Even though those rules will make new users more comfortable. Slow
changes and stability.

### License

All libraries are GPLv3, except for the httpengine one which is LGPL
as it is derived from the qhttpengine open source project.

The original Satoshi codebase was MIT licensed. This has been combined with
copyrighted works from Tom Zander which are GPLv3 licensed with the result
that those files are new licensed under the GPLv3.

