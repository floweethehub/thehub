# TxVulcano is a test-mode random transaction generation application.

This application is both a test application for Flowee the Hub's APIs and a
useful way to generate sizeable blocks for propagation and mining tests.

Flowee the Hub by default enables its APIs without password access, but
only when you run both the Hub and the client applications on the same
host. It uses the loopback device for communication.

To run the Hub and the txVulcano on different machines start the Hub with
the config option (or command-line argument) `apilisten`, followed with
the IP address of the local network card it should listen on.
The argument can be passed multiple times if you want the Hub to listen on
multiple network cards.

It is essential to understand that creating a huge amount of transactions,
and mining those blocks in the Hub can only happen in the `regtest`
development mode. The main reason of this is cost. Mostly cost of mining.

The suggested way to start the Hub is then;

    `hub -regtest -blockmaxsize=500000000`

The txVulcano application has several option as well, but generally the
defaults will be fine for a testrun. But please do check out the `--help`
option for what is possible.

The simplest way to start creating transactions and mining blocks is to
run;
   `txVulcano localhost`

where 'localhost' is the hostname or IP address where the Hub can be found.

# wallet

The txVulcano application will create a simple wallet file named 'mywallet'
that is stored in the directory you started the vulcano application from.
This allows multiple runs of the vulcano app without the need to first mine
a lot of empty blocks just to get some coin we can spent.


Enjoy playing with this and please let me know if it was useful for you!

Tom
