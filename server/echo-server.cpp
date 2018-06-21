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

static int
lcore_execute(__attribute__((unused)) void *arg)
{
    int n;
    struct lcore_args *myarg;
    uint8_t queue;
    struct rte_mbuf *bufs[BATCH_SIZE];
    struct rte_mbuf *response[BATCH_SIZE];
    int drops[BATCH_SIZE];
    uint16_t bsz, i, j, port, nb_ports;
    myarg = (struct lcore_args *)arg;
    queue = 0; //myarg->tid;
    bsz = BATCH_SIZE;
    nb_ports = rte_eth_dev_count_avail();

    printf("Server worker %" PRIu8 " started\n", myarg->tid);

    do
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
                    pkt_set_attribute(response[i]);

                }
            }

            i = 0;
            while (i < j)
            {
                n = rte_eth_tx_burst(port, queue, response + i, j - i);
                i += n;
            }

            //
        }

    } while (1);

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
    ap.addArgument("--blocked", true);
    ap.addArgument("--az", 1, true);
    ap.parse(argc, (const char**)argv);
    std::vector<std::string> ips = ap.retrieve<std::vector<std::string>>("ips");
    std::vector<std::string> macs;
    if (ap.count("blocked") > 0)
    {
        macs = ap.retrieve<std::vector<std::string>>("blocked");
    }
    /* Initialize NIC ports */
    threadnum = rte_lcore_count();
    if(threadnum < 2) 
    {
        rte_exit(EXIT_FAILURE, "use -c -l?! give more cores.");
    }

    if (0 != rte_get_master_lcore())
    {
        rte_exit(EXIT_FAILURE, "master core must be 0. now is %d", rte_get_master_lcore());
    }
    largs = (lcore_args *)calloc(threadnum, sizeof(lcore_args));
    std::unordered_map<int,int> lCore2Idx;
    std::unordered_map<int,int> Idx2LCore;
    CoreIdxMap(lCore2Idx, Idx2LCore);
    bool MSFTAZ = false;
    if(ap.count("az") > 0)
    {
        MSFTAZ = true;
    }
    for (int idx = 0; idx < threadnum; idx++)
    {
        int CORE = Idx2LCore.at(idx);
        largs[idx].CoreID = CORE;
        largs[idx].tid = idx;
        largs[idx].type = pkt_type::ECHO; //(pkt_type)atoi(argv[1]);
        largs[idx].counter = INT_MAX;
        largs[idx].master = rte_get_master_lcore() == largs[idx].CoreID;
        largs[idx].AzureSupport = MSFTAZ;
    }

    if (0 != ports_init(largs, threadnum , ips, macs))
    {
        rte_exit(EXIT_FAILURE, "ports_init failed with %s", rte_strerror(rte_errno));
    }

    /* call lcore_execute() on every slave lcore */
    RTE_LCORE_FOREACH_SLAVE(lcore_id)
    {
        rte_eal_remote_launch(lcore_execute, (void *)(&largs[lCore2Idx.at(lcore_id)]),
                              lcore_id);
    }

    printf("Master core performs maintainence\n");
    rte_eal_mp_wait_lcore();

    free(largs);
    return 0;
}
