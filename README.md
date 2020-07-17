# Overview

This tool can run DPDK to probe latency on Azure and EC2 clouds through echoing UDP packets.

On both clouds, a VM of at least two ports are needed. One for probing, and the other for communication.
On Azure, make sure Accelerated Networking on your instance is enabled. To check that, issue `lspci` and make sure you see a Mellanox device in your PCI list.
On EC2, make sure Enhanced Networking is enabled, and Enhanced Networking Adapater is installed. To check that, make sure the kernel module `ena` is loaded and functioning.

# Installation

This tool requires Python.

Use the `build.sh` script provided to build the tool. Make sure you change the script to reflect the correct directories that DPDK is installed in.

# Running

On Azure, no binding of ports is required.
On EC2, use the `ec2_initdpdk.sh` to perform port binding.
In both cases, a `[cloud]_initdpdk.sh` is provided to perform the initialization tasks.

The tool, once built, will be in the client folder, named `echo-client`.

## Coordination 

To generate larger-scale probing heatmap, the tool automatically finds pairs of VMs to probe in the most efficient manner to possible. The tool coordinates the VMs to finish probing each other (a total of N(N-1) pairs) in N-1 rounds, with each round invoking N nodes, and each nodes participates in only 1 probe at a time to avoid interference.

The coordination is done through a redis service. 

## Example

An example invocation is provided as follow. Run this on all nodes.

`sudo ./echo-client -c 0x3 " + dpdkechoAdditionalEALArgs + " -- --srcIp " + srcIp + " --srcMac " + srcMac + " --dstIps " + dips + " --dstMacs " + \
        dmacs + " --samples " + str(PROBE_SAMPLES) + " --sid " + sid + " --dids " + didss + " --output " + output + \
        " --rendezvous " + rendezvous + " --processBanner \"" + \
        banners[args.benchmark].strip() + "\""`
        
Where `srcIp` and `srcMac` is a node's *communication* IP address and MAC.  `dstIps` and `dstMacs` are lists of *probing* IP addresses and MACs for all the ranks (including this node's). The order must match. `samples` is the number of probes to run per pair. `sid` is a nickname (identifier) for the current node. It is usually set to a hostname. `dids` include all the nicknames of all nodes. `output` is where the output file will be written. `rendezvous` is where the redis resides, it expects a format of: `rendezvous = "%s:%s:%s:%s:%s" % (redisIp, redisPort, "probe-prefix", world_size, myRank)`. `processBanner` is an arbitrary string that appears on top of all the output file.
.

Credits to Ming Liu for the first version of this tool, and various stackoverflow sources for snippets.
