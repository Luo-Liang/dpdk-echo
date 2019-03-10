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

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <sys/queue.h>
#include <unistd.h>
#include <sys/time.h>

#include <rte_memory.h>
#include <rte_memzone.h>
#include <rte_launch.h>
#include <rte_eal.h>
#include <rte_per_lcore.h>
#include <rte_lcore.h>
#include <rte_debug.h>
#include <rte_ethdev.h>

#include "../shared/dpdk-helpers.h"
#include "../shared/pkt-utils.h"
#include "../shared/argparse.h"

//this is the maximum we can have.
#define JITTER_TEST_RECV_BATCH_SIZE 65535

static int
lcore_jitter(__attribute__((unused)) void *arg)
{
    auto myarg = (struct lcore_args *)arg;
    auto queue = 0;
    int bsz = JITTER_TEST_RECV_BATCH_SIZE;
    printf("Server worker %" PRIu8 " started\n", myarg->tid);
    int drops[JITTER_TEST_RECV_BATCH_SIZE];
    rte_mbuf *bufs[JITTER_TEST_RECV_BATCH_SIZE];
    timeval now, prev, start;
    gettimeofday(&start, NULL);
    int prevReading = -1;
    assert(myarg->associatedPorts.size() == 1);
    int port = myarg->associatedPorts[0];
    myarg->counter = 0;
    while (true)
    {
        int recved = 0;
        gettimeofday(&now, NULL);
        if ((recved = rte_eth_rx_burst(port, queue, bufs, bsz)) < 0)
        {
            rte_exit(EXIT_FAILURE, "Error: rte_eth_rx_burst failed\n");
        }

        if (prevReading > 0 && prevReading != JITTER_TEST_RECV_BATCH_SIZE && recved != 0)
        {

            long diff = (now.tv_sec - prev.tv_sec) * 1000000L + (now.tv_usec - prev.tv_usec);
            if (diff > 0)
            {
                myarg->samples.push_back(diff);
            }
            else
            {
                //printf("recved = %d. diff= %llu\n", recved, diff);
            }
            //p;
        }

        for(int i = 0 ; i < recved; i++)
        {
            rte_pktmbuf_free(bufs[i]);
        }

        //printf("recv burst size = %d\n", recved);
        //i need to measure.
        //i have to hope my core loops faster than recver.
        //this is done by ensuring two
        //non-full consec recv calls exist. and we extract the jitter.

        if (now.tv_sec - start.tv_sec > 10)
        {
            break;
        }
        myarg->counter += recved;
        prevReading = recved;
        prev = now;
    }
    printf("total recved = %llu\n", myarg->counter);
}

static int
lcore_execute(__attribute__((unused)) void *arg)
{
    int n;
    struct lcore_args *myarg;
    uint8_t queue;
    struct rte_mbuf *bufs[BATCH_SIZE];
    struct rte_mbuf *response[BATCH_SIZE];
    int drops[BATCH_SIZE];
    uint16_t bsz, i, j, port;
    myarg = (struct lcore_args *)arg;
    queue = 0; //myarg->tid;
    bsz = BATCH_SIZE;

    printf("Server worker %" PRIu8 " started\n", myarg->tid);
    while (myarg->counter > 0)
    {
        for (int port : myarg->associatedPorts)
        {
            /* Receive and process requests */
            if ((n = rte_eth_rx_burst(port, queue, bufs, bsz)) < 0)
            {
                rte_exit(EXIT_FAILURE, "Error: rte_eth_rx_burst failed\n");
            }
            for (i = 0; i < n; i++)
            {
                drops[i] = pkt_server_process(bufs[i], myarg->type);
            }

            for (i = 0, j = 0; i < n; i++)
            {
                if (drops[i])
                {
                    rte_pktmbuf_free(bufs[i]);
                }
                else
                {
                    response[j++] = bufs[i];
                    pkt_set_attribute(response[i], myarg->AzureSupport);
                }
            }

            i = 0;
            while (i < j)
            {
                n = rte_eth_tx_burst(port, queue, response + i, j - i);
                myarg->counter -= j - i;
                i += n;
            }

            //
        }
    }

    return 0;
}

int main(int argc, char **argv)
{
    int ret, i;
    unsigned lcore_id;
    uint8_t threadnum;
    struct lcore_args *largs;

    /* Initialize the Environment Abstraction Layer (EAL) */
    ret = rte_eal_init(argc, argv);
    if (ret < 0)
    {
        rte_exit(EXIT_FAILURE, "Error: cannot init EAL\n");
    }
    argc -= ret;
    argv += ret;

    /* Initialize application args */

    InitializePayloadConstants();

    ArgumentParser ap;
    ap.addArgument("--ips", '+', false);
    ap.addArgument("--macs", '+', false);
    ap.addArgument("--blocked", true);
    ap.addArgument("--az", 1, true);
    ap.addArgument("--samples", 1, false);
    ap.addArgument("--benchmark", 1, false);
    ap.addArgument("--output", 1, true);
    ap.parse(argc, (const char **)argv);
    std::vector<std::string> ips = ap.retrieve<std::vector<std::string>>("ips");
    std::vector<std::string> macs = ap.retrieve<std::vector<std::string>>("macs");
    std::vector<std::string> blockedIfs;
    if (ap.count("blocked") > 0)
    {
        blockedIfs = ap.retrieve<std::vector<std::string>>("blocked");
    }
    macs = ap.retrieve<std::vector<std::string>>("macs");
    if (ips.size() != macs.size())
    {
        rte_exit(EXIT_FAILURE, "specify same number of ips and macs.");
    }
    /* Initialize NIC ports */
    threadnum = rte_lcore_count();
    if (threadnum < 2)
    {
        rte_exit(EXIT_FAILURE, "use -c -l?! give more cores.");
    }

    if (0 != rte_get_master_lcore())
    {
        rte_exit(EXIT_FAILURE, "master core must be 0. now is %d", rte_get_master_lcore());
    }
    largs = (lcore_args *)calloc(threadnum, sizeof(lcore_args));
    std::unordered_map<int, int> lCore2Idx;
    std::unordered_map<int, int> Idx2LCore;
    CoreIdxMap(lCore2Idx, Idx2LCore);
    bool MSFTAZ = false;
    if (ap.count("az") > 0)
    {
        MSFTAZ = false;
    }

    size_t samples = 1;
    samples = atoi(ap.retrieve<std::string>("samples").c_str());

    for (int idx = 0; idx < threadnum; idx++)
    {
        int CORE = Idx2LCore.at(idx);
        largs[idx].CoreID = CORE;
        largs[idx].tid = idx;
        largs[idx].type = pkt_type::ECHO; //(pkt_type)atoi(argv[1]);
        largs[idx].counter = samples;
        largs[idx].master = rte_get_master_lcore() == largs[idx].CoreID;
        largs[idx].AzureSupport = MSFTAZ;
    }

    if (0 != ports_init(largs, threadnum, ips, macs, blockedIfs))
    {
        rte_exit(EXIT_FAILURE, "ports_init failed with %s", rte_strerror(rte_errno));
    }

    if (ap.retrieve<std::string>("benchmark") == "jitter")
    {
        /* call lcore_execute() on every slave lcore */
        RTE_LCORE_FOREACH_SLAVE(lcore_id)
        {
            rte_eal_remote_launch(lcore_jitter, (void *)(&largs[lCore2Idx.at(lcore_id)]), lcore_id);
        }
    }
    else
    {
        //run
        RTE_LCORE_FOREACH_SLAVE(lcore_id)
        {
            rte_eal_remote_launch(lcore_execute, (void *)(&largs[lCore2Idx.at(lcore_id)]), lcore_id);
        }
    }

    printf("Master core performs maintainence\n");
    fflush(stdout);
    rte_eal_mp_wait_lcore();
    //EmitFile(ap, largs, threadnum);
    free(largs);
    return 0;
}
