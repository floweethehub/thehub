This doc is about the Docker container for "flowee/pos-cli".

Other Docker containers [in this repo](../README.md)

The pos-cli is a simple and fast command that can be used as a basic
backing of a **Point-of-Sale** setup.

pos-cli is not a service, it follows the so called '[entry-point](https://blog.codeship.com/understanding-dockers-cmd-and-entrypoint-instructions/)' design
which in essence means it behaves just like a command-line application but
in a Docker container.

Running a basic command goes like this;

```
docker run --rm flowee/pos-cli

Usage: /usr/bin/pos [options] [address]

Options:
  -h, --help           Displays this help.
  --version            Display version
  --connect <Hostname> server location and port
  --verbose, -v        Be more verbose
  --quiet, -q          Be quiet, only errors are shown

Arguments:
  [address]            Addresses to listen to

```

The usage of `--rm` on the commandline is useful because it removes the
container after it exists.

Use the `--connect=` argument to point to a running *the Hub* and and pass
in any number of addresses to the command;
```
docker run --rm flowee/pos-cli --connect=192.168.1.2 qpgn5ka4jptc98a9ftycvujxx33e79nxuqlz5mvxns

18:14:52 [7000] Remote server version: "Flowee:1 (2019-7.1)" 
```

This command will not return until you hit Ctrl-C, and any activity on that
address will be printed with a notification line.

Currently notifications will be given when a transaction paying this
transaction is seen entering mempool (or present in mempool during startup).

Notifications for transactions paying the addresses monitored that are being
mined will be notified.

Notifications will be given when a double spend involving our transaction
has been noticed.

