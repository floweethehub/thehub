This doc is about the Docker container for "flowee/hub".

More docs;  [online](https://flowee.org/hub)

Other Docker containers [in this repo](../README.md)

The Hub is the main clearing house between the evil and unpredictable outside world and your internal network. We don't trust the outside world, but we can trust the data that the Hub gives us thanks to the innovation of Satoshi Nakamoto and Bitcoin Cash.

Starting the Hub requires at least 1 volume. This volume will gather around 200GB of data, so be aware of this.

```
docker volume create flowee_hub_data
docker run -d -v flowee_hub_data:/data -p 1235:1235 flowee/hub
```


## Separate blocks and main data

The Hub prefers SSD based storage on /data. To keep space-usage on that volume down, you can optionally also provide a volume on /blocks, which can be HHD (slower).
The blocks will take approx 200GB, the rest will be approx 15GB.

In the next example a second mapping will be made from your hosts
`/mnt/ssd/flowee directory` directly into the docker volume. Change this to
map to your fast-storage location.

```
docker volume create flowee_hub_data
docker run -d -v flowee_hub_data:/blocks -v /mnt/ssd/flowee:/data -p 1235:1235 flowee/hub
```


Options (set as env variables);

* FLOWEE_NETWORK `allows you to choose other networks, options are regtest, testnet and testnet4 and scalenet`

* FLOWEE_RPC_PASSWORD `The content of the 'cookie' password file (advanced). Please note that this is for the old style RPC, not Flowee's APIs`

* FLOWEE_LOGLEVEL `allows you to change the log-level. Recognized options are info, quiet or silent`



