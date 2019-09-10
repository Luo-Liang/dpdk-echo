/*
 * Cluster configuration header
 */
#ifndef _CLUSTER_CFG_H
#define _CLUSTER_CFG_H

#include <rte_ethdev.h>
#include <rte_ether.h>
#include <vector>
#include <rte_memory.h>
#include <rte_memzone.h>
#include <rte_launch.h>
#include <rte_eal.h>
#include <rte_per_lcore.h>
#include <rte_lcore.h>
#include <rte_debug.h>
#include <string>
#include <assert.h>
#include <algorithm>
#include <unordered_map>
#include "argparse.h"
#include <iostream>
#include <fstream>

#define NUM_MBUFS 65535
#define MBUF_CACHE_SIZE 13107
#define RX_RING_SIZE 512
#define TX_RING_SIZE 512
#define BATCH_SIZE 36
#define rte_eth_dev_count_avail rte_eth_dev_count_avail
#define IPV4_ADDR_LEN 4

enum pkt_type
{
	ECHO_REQ,
	ECHO_RES,
	ECHO_IRRELEVANT
};

struct endhost
{
	uint8_t mac[ETHER_ADDR_LEN];
	uint8_t ip[IPV4_ADDR_LEN];
} __attribute__((packed));

struct lcore_args
{
	endhost src;
	std::vector<endhost> dsts;
	//the index of this arg in largs*, where a master is also included at 0.
	//volatile enum benchmark_phase *phase;
	struct rte_mempool* pool;
	std::vector<std::vector<uint64_t>> samples;
	int counter;
	uint32_t associatedPort;
	//std::vector<uint32_t> coreIdx2LCoreId;
	bool master;
	bool AzureSupport;
	int interval;
	bool verbose;
	bool selfProbe;
	int ID;
}; //__attribute__((packed));
int port_init(lcore_args* larg, std::string srcIp, std::string srcMac, std::vector<std::string> blockedSrcMac);

void CoreIdxMap(std::unordered_map<int, int>& lCore2Idx,
	std::unordered_map<int, int>& idx2LCoreId);

void EmitFile(std::string output,
	std::string sid,
	std::string did,
	std::vector<uint64_t>& samples);
#endif /* _CLUSTER_CFG_H */
