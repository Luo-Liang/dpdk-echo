/*-
 *   BSD LICENSE
 *
 *   Copyright(c) 2010-2014 Intel Corporation. All rights reserved.
 *   All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Intel Corporation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#define __STDC_FORMAT_MACROS 1

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <sys/queue.h>
#include <unistd.h>
#include <sys/time.h>
#include <string>
#include <rte_memory.h>
#include <rte_memzone.h>
#include <rte_launch.h>
#include <rte_eal.h>
#include <rte_per_lcore.h>
#include <rte_lcore.h>
#include <rte_debug.h>
#include <rte_ethdev.h>
#include <vector>
#include <unordered_map>
#include <iostream>
#include <fstream>
#include <unistd.h>
#include <chrono>
#include <hiredis/hiredis.h>
#include <string>

#include "../shared/rendezvous.h"
#include "../shared/dpdk-helpers.h"
#include "../shared/pkt-utils.h"
#include "../shared/argparse.h"

// enum benchmark_phase
// {
//     BENCHMARK_WARMUP,
//     BENCHMARK_RUNNING,
//     BENCHMARK_COOLDOWN,
//     BENCHMARK_DONE,
// } __attribute__((aligned(64)));

uint64_t tot_proc_pkts = 0, tot_elapsed = 0;
const int MAX_INTERVAL = 1000;
/*static inline void
pkt_dump(struct rte_mbuf *buf)
{
	printf("Packet info:\n");
	rte_pktmbuf_dump(stdout, buf, rte_pktmbuf_pkt_len(buf));
}*/

// size_t getDuration(std::chrono::time_point<std::chrono::steady_clock> &finish, std::chrono::time_point<std::chrono::steady_clock> &start)
// {
//     return std::chrono::duration_cast<std::chrono::nanoseconds>(finish - start).count();
// }

int ProbeSelfLatency(void *arg)
{
	//printf("here1");
	auto myarg = (lcore_args *)arg;
	auto pool = myarg->pool;
	auto port = myarg->associatedPort;
	auto pBuf = rte_pktmbuf_alloc(pool);
	auto queue = 0;
	const int PROBE_COUNT = 1000;
	int selfProbeCount = PROBE_COUNT;
	if (pBuf == NULL)
	{
		rte_exit(EXIT_FAILURE, "Error: pktmbuf pool allocation failed for self test probe.");
	}
	//printf("here3");
	rte_mbuf_refcnt_set(pBuf, selfProbeCount);
	auto pkt_ptr = rte_pktmbuf_append(pBuf, pkt_size());
	unsigned short seq = 0;
	unsigned short round = 0;
	pkt_build(pkt_ptr, myarg->src, myarg->src, pkt_type::ECHO_REQ, seq, round);
	//pkt_dump(pBuf);
	//printf("here2");
	struct rte_mbuf *rbufs[BATCH_SIZE];

	auto start = std::chrono::high_resolution_clock::now();
	auto end = std::chrono::high_resolution_clock::now();

	size_t elapsed = 0;
	uint32_t selfProbeIP = ip_2_uint32(myarg->src.ip);
	int counts = 0;
	while (selfProbeCount > 0)
	{
		start = std::chrono::high_resolution_clock::now();
		if (0 == rte_eth_tx_burst(port, queue, &pBuf, 1))
		{
			rte_exit(EXIT_FAILURE, "Error: cannot tx_burst packets self test burst failure");
		}
		//else if(myarg->verbose)
		// {
		//    printf("[%d] self probe sent\n", myarg->ID);
		//    pkt_dump(pBuf);
		//  }
		bool found = false;
		while (found == false)
		{
			int recv = 0;
			if ((recv = rte_eth_rx_burst(port, queue, rbufs, BATCH_SIZE)) < 0)
			{
				rte_exit(EXIT_FAILURE, "Error: rte_eth_rx_burst failed in self probe\n");
			}
			end = std::chrono::high_resolution_clock::now();
			for (int i = 0; i < recv; i++)
			{
				if (found == false and pkt_type::ECHO_REQ == pkt_process(rbufs[i], selfProbeIP, seq, round))
				{
					found = true;
					selfProbeCount--;
					counts++;
					auto diff = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
					elapsed += diff;
				}
				else if (myarg->verbose)
				{
					printf("[%d]self probe received unknown message. seq = %d. round = %d\n", myarg->ID, seq, round);
					pkt_dump(rbufs[i]);
				}
				rte_pktmbuf_free(rbufs[i]);
			}
			size_t timeDelta = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
			if (found == false && timeDelta > 1000000)
			{
				//1ms sec is long enough for us to tell the packet is lost.
				found = true;
				//this will trigger a resend.
				selfProbeCount--;
				//myarg->samples.push_back(timeDelta);
				//}
			}
		}
	}
	int ret = counts == 0 ? 0 : (int)(1.0 * elapsed / counts);
	printf("[%d] self probe latency = %d. %d/%d.\n", myarg->ID, ret, counts, PROBE_COUNT);
	return ret;
}
NonblockingSingleBarrier *rendezvous;

void requestBuffers(rte_mempool *pool, int samples, rte_mbuf **&mBufs, char **&pBufs)
{
	mBufs = (rte_mbuf **)malloc(sizeof(rte_mbuf *) * samples);
	pBufs = (char **)malloc(sizeof(char *) * samples);

	if (0 != rte_pktmbuf_alloc_bulk(pool, mBufs, samples))
	{
		rte_exit(EXIT_FAILURE, "Error: pktmbuf pool allocation failed.");
	}

	for (int i = 0; i < samples; i++)
	{

		pBufs[i] = rte_pktmbuf_append(mBufs[i], pkt_size());
	}
}

static int lcore_execute(void *arg)
{
	struct lcore_args *myarg;
	uint8_t queue;
	//volatile enum benchmark_phase *phase;
	//receive buffers.
	struct rte_mbuf *rbufs[BATCH_SIZE];
	auto start = std::chrono::high_resolution_clock::now();
	auto end = std::chrono::high_resolution_clock::now();
	auto now = std::chrono::high_resolution_clock::now();
	uint64_t elapsed;
	const int TIME_OUT_COUNTS = 5;
	myarg = (struct lcore_args *)arg;
	queue = 0; //myarg->tid; one port is only touched by one processor for dpdk-echo.
	//one port probably needs to be touched by multiple procs in real app.
	//phase = myarg->phase;
	//bsz = BATCH_SIZE;
	uint32_t expectedMyIp = ip_2_uint32(myarg->src.ip);
	int samples = myarg->counter;

	rte_mbuf **reqMBufs;
	rte_mbuf **resMBufs;
	char **reqBufs = NULL;
	char **resBufs = NULL;
	requestBuffers(myarg->pool, samples, reqMBufs, reqBufs);
	requestBuffers(myarg->pool, samples, resMBufs, resBufs);
	//last round is just sending to self.

	std::vector<endhost> recvOrder = myarg->dsts;
	std::reverse(recvOrder.begin(), recvOrder.end() - 1);
	if (myarg->verbose)
	{
		std::string overall = "[" + std::to_string(myarg->ID) + "] Routing Information";
		overall += "\n    dest:\n";
		std::string sendSeq = "        ";
		for (int i = 0; i < (int)myarg->dsts.size(); i++)
		{
			sendSeq += "," + dbgStringFromIP(myarg->dsts.at(i).ip) + "(" + dbgStringFromMAC(myarg->dsts.at(i).mac) + ")";
		}
		overall += sendSeq;
		overall += "\n    recv:\n";
		std::string recvSeq = "        ";
		for (int i = 0; i < (int)recvOrder.size(); i++)
		{
			recvSeq += "," + dbgStringFromIP(recvOrder.at(i).ip) + "(" + dbgStringFromMAC(recvOrder.at(i).mac) + ")";
		}
		overall += recvSeq;
		printf("%s", overall.c_str());
	}
	for (unsigned short round = 0; round < myarg->dsts.size() - 1; round++)
	{
		//build packet.
		for (int i = 0; i < samples; i++)
		{
			pkt_build(reqBufs[i], myarg->src, myarg->dsts.at(round), pkt_type::ECHO_REQ, i, round);
			rte_mbuf_refcnt_set(reqMBufs[i], UINT16_MAX);
			pkt_set_attribute(reqMBufs[i]);
			//response ip is not same as dest ip
			pkt_build(resBufs[i], myarg->src, recvOrder.at(round), pkt_type::ECHO_RES, i, round);
			rte_mbuf_refcnt_set(resMBufs[i], UINT16_MAX);
			pkt_set_attribute(resMBufs[i]);
		}

		int consecTimeouts = 0;
		rendezvous->SynchronousBarrier(CxxxxStringFormat("initialize round %d", round));
		int pid = 0;
		auto sendMoreProbe = (pid < myarg->counter && consecTimeouts < TIME_OUT_COUNTS);
		std::string barrierName = CxxxxStringFormat("round %d", round);

		while (sendMoreProbe || rendezvous->NonBlockingQueryBarrier(barrierName) == false)
		{
			auto port = myarg->associatedPort;
			/* Receive and process responses */
			//send a single packet and wait for response.
			/* Prepare and send requests */
			//do this only if not enough sample is collected.
			if (sendMoreProbe)
			{
				if (pid >= samples)
				{
					rte_exit(EXIT_FAILURE, "error. pid must be less than sample [%d]. r=%d. pid=%d. samples=%d\n", myarg->ID, round, pid, samples);
				}
				//pkt_dump(bufs[i]);
				start = std::chrono::high_resolution_clock::now();
				if (0 == rte_eth_tx_burst(port, queue, &reqMBufs[pid], 1))
				{
					rte_exit(EXIT_FAILURE, "Error: cannot tx_burst packets");
				}
				else if (myarg->verbose)
				{
					printf("[%d][round %d] echo request sent. pid = %d.\n", myarg->ID, round, pid);
					pkt_dump(reqMBufs[pid]);
				}
			}
			/* free non-sent buffers */
			bool found = false;
			while ((found == false && sendMoreProbe == true) || (sendMoreProbe == false && rendezvous->NonBlockingQueryBarrier(barrierName) == false))
			{
				int recv = 0;
				if ((recv = rte_eth_rx_burst(port, queue, rbufs, BATCH_SIZE)) < 0)
				{
					rte_exit(EXIT_FAILURE, "Error: rte_eth_rx_burst failed\n");
				}
				end = std::chrono::high_resolution_clock::now();
				for (int i = 0; i < recv; i++)
				{
					unsigned short seq = 0;
					unsigned short r = 0;
					auto type = pkt_process(rbufs[i], expectedMyIp, seq, r);
					if (type == ECHO_RES)
					{
						if (sendMoreProbe && seq == pid)
						{
							//it is a response. I can record time.
							consecTimeouts = 0;
							found = true;
							//__sync_fetch_and_add(&tot_proc_pkts, 1);
							elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count(); //getDuration(end, start);
							myarg->samples.at(round).push_back(elapsed);
							if (myarg->verbose)
							{
								printf("[%d][round %d] echo response received. %d us. seq = %d. r = %d\n", myarg->ID, round, (uint32_t)elapsed, seq, r);
								pkt_dump(rbufs[i]);
							}
							sendMoreProbe = (pid < myarg->counter - 1);
							if (sendMoreProbe == false)
							{
								//a flip of truth value means a submission to the barrier
								rendezvous->SubmitBarrier(barrierName);
								printf("[information][ID=%d][round=%d] finished with a qualified response. %d/%d\n", myarg->ID, round, (int)myarg->samples.at(round).size(), samples);
							}
							else
							{
								pid++;
							}
						}
						else if (myarg->verbose)
						{
							printf("[%d][round %d] echo response received but not expected. seq = %d. expecting = %d (may be garbage). r = %d\n", myarg->ID, round, seq, pid, r);
							pkt_dump(rbufs[i]);
						}
					}
					else if (type == ECHO_REQ)
					{

						if (myarg->verbose)
						{
							printf("[%d][round %d] echo request received. seq = %d. r = %d. \n", myarg->ID, round, seq, r); //, (uint32_t)elapsed);
							pkt_dump(rbufs[i]);
						}
						//someone else's request. Send response.
						//let dpdk decide whether to batch or not
						if (0 == rte_eth_tx_burst(port, queue, &resMBufs[seq], 1))
						{
							rte_exit(EXIT_FAILURE, "Error: response send failed\n");
						}
						if (myarg->verbose)
						{
							printf("[%d][round %d] echo request responded. seq = %d. r = %d\n", myarg->ID, round, seq, r); //, (uint32_t)elapsed);
							pkt_dump(resMBufs[pid]);
						}
					}
					else
					{
						printf("[%d][round %d] unknown packet received. \n", myarg->ID, round); //, (uint32_t)elapsed);
						pkt_dump(resMBufs[pid]);
					}
					rte_pktmbuf_free(rbufs[i]);
				}

				//set a 1s timeout.
				if (sendMoreProbe && found == false)
				{
					//timeout and recovery only relevant if more packets are sent.
					const size_t TIME_OUT = 1000000000ULL;
					size_t timeDelta = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count(); // getDuration(end, start);
					if (timeDelta > TIME_OUT)
					{
						//1ms is long enough for us to tell the packet is lost.
						found = true;
						consecTimeouts++;
						//this will trigger a resend.
						if (myarg->verbose)
						{
							printf("[%d][round %d] request timeout pid=%d. consecTimeouts=%d. %d/%d\nLost packet:", myarg->ID, round, pid, consecTimeouts, (int)myarg->samples.at(round).size(), samples); //, (uint32_t)elapsed);
							pkt_dump(reqMBufs[pid]);
						}

						//myarg->samples.push_back(TIME_OUT);
						//choosing median. penalizing drops.
						//myarg->samples.push_back(1000);
						//}
						sendMoreProbe = (pid < myarg->counter - 1 && consecTimeouts < TIME_OUT_COUNTS);
						if (sendMoreProbe == false)
						{
							//a flip of truth value means a submission to the barrier
							std::string barrierName = CxxxxStringFormat("round %d", round);
							rendezvous->SubmitBarrier(barrierName);
							printf("[information][ID=%d][round=%d] finished with a timeout. %d/%d\n", myarg->ID, round, (int)myarg->samples.at(round).size(), samples);
						}
						else
						{
							pid++;
						}
					}
				}
			}
			now = std::chrono::high_resolution_clock::now();
			while (std::chrono::duration_cast<std::chrono::nanoseconds>(now - start).count() < myarg->interval)
			{
				now = std::chrono::high_resolution_clock::now();
			}
		}
	}
	//printf("Thread %d has finished executing.\n", myarg->tid);
	return 0;
}

int main(int argc, char **argv)
{
	struct lcore_args larg;
	/* Initialize the Environment Abstraction Layer (EAL) */
	int ret = rte_eal_init(argc, argv);
	if (ret < 0)
	{
		rte_exit(EXIT_FAILURE, "Error: cannot init EAL\n");
	}
	argc -= ret;
	argv += ret;

	/* Initialize application args */
	/*if (argc != 4)
	{
		printf("Usage: %s <type> <dest IP> <dest MAC>\n", argv[0]);
		rte_exit(EXIT_FAILURE, "Error: invalid arguments\n");
	}*/

	ArgumentParser ap;
	ap.addArgument("--srcIp", 1, false);
	ap.addArgument("--srcMac", 1, false);
	ap.addArgument("--dstIps", '+', false);
	ap.addArgument("--dstMacs", '+', false);
	ap.addArgument("--samples", 1, false);
	ap.addArgument("--sid", 1, false);
	ap.addArgument("--dids", '+', false);
	ap.addArgument("--blocked", true);
	ap.addArgument("--outputs", '+', false);
	//enable Windows Azure support
	ap.addArgument("--interval", 1, true);
	ap.addArgument("--az", 1, true);
	ap.addArgument("--verbose", 1, true);
	ap.addArgument("--payload", 1, true);
	ap.addArgument("--noSelfProbe", 1, true);
	ap.addArgument("--rendezvous", 1, false);
	//output dids
	//receive order. This can be irrelevant to dids.
	int counter = 10;
	while (counter > 0)
	{
		sleep(1);
		counter--;
	}
	ap.parse(argc, (const char **)argv);

	std::string localIP = ap.retrieve<std::string>("srcIp");
	std::string localMAC = ap.retrieve<std::string>("srcMac");

	size_t samples = atoi(ap.retrieve<std::string>("samples").c_str());
	if (samples == (size_t)(-1))
	{
		rte_exit(EXIT_FAILURE, "what is %s?", ap.retrieve<std::string>("samples").c_str());
	}

	int interval = 0;
	if (ap.count("interval") > 0)
	{
		interval = atoi(ap.retrieve<std::string>("interval").c_str());
	}

	int payloadLen = 5;

	if (ap.count("payload") > 0)
	{
		payloadLen = atoi(ap.retrieve<std::string>("payload").c_str());
	}

	InitializePayloadRequest(payloadLen);
	InitializePayloadResponse();
	/* Initialize NIC ports */
	bool MSFTAZ = false;
	if (ap.count("az") > 0)
	{
		MSFTAZ = false;
	}

	bool noSelfProbe = false;
	if (ap.count("noSelfProbe") > 0)
	{
		noSelfProbe = true;
	}

	bool verbose = false;
	if (ap.count("verbose") > 0)
	{
		verbose = true;
	}
	//for (int idx = 0; idx < threadnum; idx++)
	//{

	//}
	std::vector<std::string> blockedIFs;
	if (ap.count("blocked") > 0)
	{
		blockedIFs = ap.retrieve<std::vector<std::string>>("blocked");
	}

	std::string combo = ap.retrieve<std::string>("rendezvous");

	std::string host;
	uint16_t port;
	std::string prefix;
	int size;
	int rank;
	/* Start applications */
	//host,port,prefix,worldsize,rank
	ParseHostPortPrefixWorldSizeRank(combo, host, port, prefix, size, rank);
	//string ip, uint port, string pref = "PLINK"
	larg.worldSize = size;
	rendezvous = new NonblockingSingleBarrier(host, port, prefix, size);
	rendezvous->Connect();
	rendezvous->SynchronousBarrier("initial");
	larg.ID = rank;

	ret = port_init(&larg, localIP, localMAC, blockedIFs);
	if (ret != 0)
	{
		printf("port init failed. %s.\n", rte_strerror(rte_errno));
	}

	int selfLatency = 0;
	if (ap.count("noSelfProbe") == 0)
	{
		selfLatency = ProbeSelfLatency(&larg);
	}
	//contribute to self latency to redis.
	rendezvous->PushKey(CxxxxStringFormat("selfProbe%d", rank), std::to_string(selfLatency));
	rendezvous->SynchronousBarrier("selfProbeSubmission");

	auto outputs = ap.retrieve<std::vector<string>>("outputs");
	auto dstIps = ap.retrieve<std::vector<std::string>>("dstIps");
	auto dstMacs = ap.retrieve<std::vector<std::string>>("dstMacs");
	auto dids = ap.retrieve<std::vector<std::string>>("dids");
	if (dstIps.size() != dstMacs.size() || dstMacs.size() != dids.size() || dids.size() != outputs.size())
	{
		rte_exit(EXIT_FAILURE, "specify same number of destination ips and macs and remote ids.");
	}

	auto sid = ap.retrieve<std::string>("sid");

	larg.samples.resize(size, std::vector<uint64_t>());
	larg.counter = samples;
	larg.master = true; // rte_get_master_lcore() == largs[idx].CoreID;
	larg.AzureSupport = MSFTAZ;
	larg.interval = interval;
	larg.verbose = verbose;
	larg.selfProbe = !noSelfProbe;
	larg.dsts.resize(dstMacs.size());
	for (int i = 0; i < (int)dstMacs.size(); i++)
	{
		IPFromString(dstIps.at(i), larg.dsts.at(i).ip);
		MACFromString(dstMacs.at(i), larg.dsts.at(i).mac);
	}
	lcore_execute(&larg);

	printf("All threads have finished executing.\n");

	bool applySelfProbeAdjustment = !noSelfProbe;
	/* print status */

	if (applySelfProbeAdjustment)
	{
		for (int i = 0; i < size - 1; i++)
		{
			auto remote = (rank + i + 1) % size;
			auto remoteSelfLatency = atoi(rendezvous->waitForKey(CxxxxStringFormat("selfProbe%d", remote)).c_str());
			if(remoteSelfLatency == 0)
			{
				applySelfProbeAdjustment = false;
				printf("warning: self probe latency is turned on, but some remote(s) did not have qualified data.\n");
				break;
			}
		}
	}

	for (int i = 0; i < size - 1; i++)
	{
		if (applySelfProbeAdjustment)
		{
			auto remote = (rank + i + 1) % size;
			auto remoteSelfLatency = atoi(rendezvous->waitForKey(CxxxxStringFormat("selfProbe%d", remote)).c_str());
			for (int eleIdx = 0; eleIdx < (int)larg.samples.at(i).size(); eleIdx++)
			{
				larg.samples.at(i).at(eleIdx) -= (remoteSelfLatency + selfLatency);
			}
		}
		EmitFile(outputs.at(i), sid, dids.at(i), larg.samples.at(i));
	}
	//free(largs);
	return 0;
}
