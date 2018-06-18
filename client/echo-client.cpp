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
#include "../shared/cluster-cfg.h"
#include "../shared/pkt-utils.h"
#include "../shared/argparse.h"

#define NUM_MBUFS 8191
#define MBUF_CACHE_SIZE 250
#define RX_RING_SIZE 512
#define TX_RING_SIZE 512
#define BATCH_SIZE 36
#define rte_eth_dev_count_avail rte_eth_dev_count
// enum benchmark_phase
// {
//     BENCHMARK_WARMUP,
//     BENCHMARK_RUNNING,
//     BENCHMARK_COOLDOWN,
//     BENCHMARK_DONE,
// } __attribute__((aligned(64)));

struct lcore_args
{
    std::vector<endhost> srcs;
    endhost dst;
    enum pkt_type type;
    //the index of this arg in largs*, where a master is also included at 0.
    uint8_t tid;
    //volatile enum benchmark_phase *phase;
    struct rte_mempool *pool;
    std::vector<uint64_t> samples;
    size_t counter;
    std::vector<uint32_t> associatedPorts;
    //std::vector<uint32_t> coreIdx2LCoreId;
    uint32_t CoreID;
}; //__attribute__((packed));

struct settings
{
    uint32_t warmup_time;
    uint32_t run_time;
    uint32_t cooldown_time;
} __attribute__((packed));

uint64_t tot_proc_pkts = 0, tot_elapsed = 0;
std::unordered_map<uint32_t, uint32_t> lCore2Idx;
/*static inline void 
pkt_dump(struct rte_mbuf *buf)
{
    printf("Packet info:\n");
    rte_pktmbuf_dump(stdout, buf, rte_pktmbuf_pkt_len(buf));
}*/


//largs is the FIRST WORKER, aka real largs + 1
static inline int
ports_init(struct lcore_args *largs,
           uint8_t workerCnt,
           std::vector<std::string> suppliedIPs,
           std::vector<std::string> blockedSrcMac)
{
    rte_eth_conf port_conf_default;
    memset(&port_conf_default, 0, sizeof(rte_eth_conf));
    port_conf_default.rxmode.mq_mode = ETH_MQ_RX_RSS;
    //port_conf_default.rxmode.max_rx_pkt_len = ETHER_MAX_LEN;
    //port_conf_default.rxmode.split_hdr_size = 0;
    //port_conf_default.rxmode.ignore_offload_bitfield = 1;
    //port_conf_default.rxmode.offloads = (DEV_RX_OFFLOAD_CRC_STRIP | DEV_RX_OFFLOAD_CHECKSUM);

    //port_conf_default.rx_adv_conf.rss_conf.rss_key = NULL;
    //port_conf_default.rx_adv_conf.rss_conf.rss_hf = ETH_RSS_IP;

    port_conf_default.txmode.mq_mode = ETH_MQ_TX_NONE;
    struct rte_eth_conf port_conf = port_conf_default;
    uint8_t q, rx_rings, tx_rings, nb_ports;
    int retval, i;
    char bufpool_name[32];

    nb_ports = rte_eth_dev_count_avail();
    printf("Number of ports of the server is %" PRIu8 "\n", nb_ports);
    assert(nb_ports <= suppliedIPs.size());
    std::vector<int> portids;
    //now assign port to cores.
    for (int i = 0; i < nb_ports; i++)
    {
        ether_addr tmp;
        rte_eth_macaddr_get(i, &tmp);
        char macStr[18];
        snprintf(macStr, sizeof(macStr), "%02x:%02x:%02x:%02x:%02x:%02x",
                 tmp.addr_bytes, tmp.addr_bytes + 1, tmp.addr_bytes + 2, tmp.addr_bytes + 3, tmp.addr_bytes + 4, tmp.addr_bytes + 5);
        std::string macString(macStr);
        if (std::find(blockedSrcMac.begin(), blockedSrcMac.end(), macString) != blockedSrcMac.end())
        {
            // this interface is blocked.
            continue;
        }
        //skip largs[0], which is for master.
        auto targetThread = i % workerCnt;
        portids.push_back(i);
        largs[targetThread].associatedPorts.push_back(i);
    }

    for (i = 0; i < workerCnt; i++)
    {
        sprintf(bufpool_name, "bufpool_%d", i);
        largs[i].pool = rte_pktmbuf_pool_create(bufpool_name,
                                                NUM_MBUFS, MBUF_CACHE_SIZE, 0,
                                                RTE_MBUF_DEFAULT_BUF_SIZE, largs[i].CoreID);
        if (largs[i].pool == NULL)
        {
            rte_exit(EXIT_FAILURE, "Error: rte_pktmbuf_pool_create failed\n");
        }
        //largs[i].src_id = (int *)malloc(sizeof(int) * nb_ports);
        largs[i].srcs.resize(largs[i].associatedPorts.size());
        //largs[i].srcMacs.resize(nb_ports);
        for (size_t pidx = 0; pidx < largs[i].associatedPorts.size(); pidx++)
        {
            auto port  = largs[i].associatedPorts.at(pidx);
            rte_eth_macaddr_get(port, (ether_addr *)largs[i].srcs.at(pidx).mac);
            //since nb_ports < suppliedIp.size, assign port-th to suppliedIps
            IPFromString(suppliedIPs.at(port), largs[i].srcs.at(pidx).ip);
            //largs[i].srcMacs.push_back( = get_endhost_id(myaddr);
        }
    }

    for (int port : portids)
    {
        //one queue is sufficient for echo?
        rx_rings = tx_rings = 1;
        retval = rte_eth_dev_configure(port, rx_rings, tx_rings, &port_conf);
        if (retval != 0)
        {
            printf("port init failed. %s. retval = %d\n", rte_strerror(rte_errno), retval);
            return retval;
        }

        rte_eth_rxconf rxqConf;

        //rte_eth_conf* pConf;
        //rte_eth_dev* pDev = &rte_eth_devices[port];
        rte_eth_dev_info devInfo;
        rte_eth_dev_info_get(port, &devInfo);
        rxqConf = devInfo.default_rxconf;
        //pConf = &pDev->data->dev_conf;
        //rxqConf.offloads = pConf->rxmode.offloads;
        /* Configure the Ethernet device of a given port */

        /* Allocate and set up RX queues for a given Ethernet port */
        for (q = 0; q < rx_rings; q++)
        {
            retval = rte_eth_rx_queue_setup(port, q, RX_RING_SIZE,
                                            rte_eth_dev_socket_id(port), &rxqConf, largs[q].pool);
            if (retval < 0)
            {
                return retval;
            }
        }

        rte_eth_txconf txqConf;
        txqConf = devInfo.default_txconf;
        //txqConf.txq_flags = ETH_TXQ_FLAGS_IGNORE;
        //txqConf.offloads = port_conf.txmode.offloads;
        /* Allocate and set up TX queues for a given Ethernet port */
        for (q = 0; q < tx_rings; q++)
        {
            retval = rte_eth_tx_queue_setup(port, q, TX_RING_SIZE,
                                            rte_eth_dev_socket_id(port), &txqConf);
            if (retval < 0)
            {
                return retval;
            }
        }

        /* Start the Ethernet port */
        retval = rte_eth_dev_start(port);
        if (retval < 0)
        {
            return retval;
        }

        /* Enable RX in promiscuous mode for the Ethernet device */
        rte_eth_promiscuous_enable(port);
    }

    return 0;
}

static int
lcore_execute(void *arg)
{
    struct lcore_args *myarg;
    uint8_t queue;
    struct rte_mempool *pool;
    //volatile enum benchmark_phase *phase;
    struct rte_mbuf *bufs[BATCH_SIZE];
    char *pkt_ptr;
    struct timeval start, end;
    uint64_t elapsed;

    myarg = (struct lcore_args *)arg;
    queue = 0; //myarg->tid; one port is only touched by one processor for dpdk-echo.
    //one port probably needs to be touched by multiple procs in real app.
    pool = myarg->pool;
    //phase = myarg->phase;
    //bsz = BATCH_SIZE;
    while (myarg->samples.size() < myarg->counter)
    {
        for (auto port : myarg->associatedPorts)
        {
            /* Receive and process responses */

            //send a single packet and wait for response.

            /* Prepare and send requests */
            if ((bufs[0] = rte_pktmbuf_alloc(pool)) == NULL)
            {
                rte_exit(EXIT_FAILURE, "Error: pktmbuf pool allocation failed.");
            }

            pkt_ptr = rte_pktmbuf_append(bufs[0], pkt_size(myarg->type));
            pkt_build(pkt_ptr, myarg->srcs.at(port), myarg->dst,
                      myarg->type, queue);
            pkt_set_attribute(bufs[0]);

            //pkt_dump(bufs[i]);
            gettimeofday(&start, NULL);
            if (0 > rte_eth_tx_burst(port, queue, bufs, 1))
            {
                rte_exit(EXIT_FAILURE, "Error: cannot tx_burst packets");
            }
            /* free non-sent buffers */
            bool found = false;
            while (found == false)
            {
                int recv = 0;
                if ((recv = rte_eth_rx_burst(port, queue, bufs, BATCH_SIZE)) < 0)
                {
                    rte_exit(EXIT_FAILURE, "Error: rte_eth_rx_burst failed\n");
                }
                gettimeofday(&end, NULL);
                for (int i = 0; i < recv; i++)
                {
                    if (pkt_client_process(bufs[i], myarg->type))
                    {
                        found = true;
                        //__sync_fetch_and_add(&tot_proc_pkts, 1);
                        elapsed = (end.tv_sec - start.tv_sec) * 1000000 +
                                  (end.tv_usec - start.tv_usec);
                        myarg->samples.push_back(elapsed);
                    }
                }

                for (int i = 0; i < recv; i++)
                {
                    rte_pktmbuf_free(bufs[i]);
                }
            }
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
    struct settings mysettings = {
        .warmup_time = 5,
        .run_time = 10,
        .cooldown_time = 5,
    };

    /* Initialize the Environment Abstraction Layer (EAL) */
    ret = rte_eal_init(argc, argv);
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
    ap.addArgument("--srcIps", '+', false);
    ap.addArgument("--dstIp", 1, false);
    ap.addArgument("--dstMac", 1, false);
    ap.addArgument("--samples", 1, false);
    ap.addArgument("--sid", 1, true);
    ap.addArgument("--did", 1, true);
    ap.addArgument("--blocked", true);
    ap.addFinalArgument("output", 1, true);

    ap.parse(argc, (const char **)argv);

    std::vector<std::string> srcips = ap.retrieve<std::vector<std::string>>("srcIps");
    endhost destination;
    destination.id = 9367;
    IPFromString(ap.retrieve<std::string>("dstIp"), destination.ip);
    MACFromString(ap.retrieve<std::string>("dstMac"), destination.mac);

    size_t samples = ap.retrieve<size_t>("samples");
    InitializePayloadConstants();
    /* Initialize NIC ports */
    threadnum = rte_lcore_count();
    largs = (lcore_args *)calloc(threadnum, sizeof(*largs));

    size_t activatedCoreCntr = 0;

    for (int CORE = 0;; i++)
    {
        if (CORE == rte_get_master_lcore())
        {
            if(CORE != 0)
            {
                rte_exit(EXIT_FAILURE, "master core must be 0. now is %d", CORE);
            }
            continue;
        }
        if (rte_lcore_is_enabled(CORE))
        {
            //get its index.
            uint32_t idx = rte_lcore_index(CORE);
            lCore2Idx[CORE] = idx;
            if (idx >= threadnum)
            {
                rte_exit(EXIT_FAILURE, "%d must be less than threadnum.", idx);
            }
            largs[idx].CoreID = CORE;
            largs[idx].tid = idx;
            largs[idx].type = pkt_type::ECHO; //(pkt_type)atoi(argv[1]);
            largs[idx].dst = destination;
            largs[idx].counter = samples;
        }
        if (activatedCoreCntr == threadnum - 1)
        {
            break;
        }
    }
    std::vector<std::string> blockedIFs;
    if(ap.count("blocked") > 0)
    {
        blockedIFs = ap.retrieve<std::vector<std::string>>("blocked");
    }
    ret = ports_init(largs + 1, threadnum - 1, srcips, blockedIFs);
    if (ret != 0)
    {
        printf("port init failed. %s.\n", rte_strerror(rte_errno));
    }

    /* Start applications */
    printf("Starting Workers\n");
    // phase = BENCHMARK_WARMUP;
    // if (mysettings.warmup_time)
    // {
    //     sleep(mysettings.warmup_time);
    //     printf("Warmup done\n");
    // }

    /* call lcore_execute() on every slave lcore */
    RTE_LCORE_FOREACH_SLAVE(lcore_id)
    {
        rte_eal_remote_launch(lcore_execute, (void *)(&largs[lCore2Idx.at(lcore_id)]),
                              lcore_id);
    }

    //sleep(mysettings.run_time);

    // if (mysettings.cooldown_time)
    // {
    //     printf("Starting cooldown\n");
    //     phase = BENCHMARK_COOLDOWN;
    //     sleep(mysettings.cooldown_time);
    // }

    // printf("Benchmark done\n");

    rte_eal_mp_wait_lcore();

    /* print status */
    if (ap.count("output") > 0)
    {
        if (ap.count("sid") == 0 || ap.count("did") == 0)
        {
            rte_exit(EXIT_FAILURE, "if output is specified, sid and did must also be specified");
        }
        auto file = ap.retrieve<std::string>("output");
        std::vector<uint64_t> consolidated;
        std::string appHeader("BENCHMARK:DPDK_ECHO;SELF_TEST_OPTION:FALSE;DIMENSION:${totalClients};VALUE:AVG;PREPROCESS:0");
        std::ofstream ofile;
        ofile.open(file);
        for (int i = 0; i < threadnum; i++)
        {
            for (auto t : largs[i].samples)
            {
                //from, to, ping result
                ofile << ap.retrieve<std::string>("sid") << ","
                      << ap.retrieve<std::string>("did") << ","
                      << t;
            }
        }
        ofile.close();
    }
    free(largs);
    return 0;
}
