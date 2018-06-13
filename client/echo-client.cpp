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

#include <rte_memory.h>
#include <rte_memzone.h>
#include <rte_launch.h>
#include <rte_eal.h>
#include <rte_per_lcore.h>
#include <rte_lcore.h>
#include <rte_debug.h>
#include <rte_ethdev.h>
#include <vector>
#include "../shared/cluster-cfg.h"
#include "../shared/pkt-utils.h"

#define NUM_MBUFS 8191
#define MBUF_CACHE_SIZE 250
#define RX_RING_SIZE 512
#define TX_RING_SIZE 512
#define BATCH_SIZE 36
#define rte_eth_dev_count_avail rte_eth_dev_count
enum benchmark_phase
{
    BENCHMARK_WARMUP,
    BENCHMARK_RUNNING,
    BENCHMARK_COOLDOWN,
    BENCHMARK_DONE,
} __attribute__((aligned(64)));

struct lcore_args
{
    std::vector<MACAddress> srcMacs;
    std::vector<IP> srcIPs;
    enum pkt_type type;
    uint8_t tid;
    volatile enum benchmark_phase *phase;
    struct rte_mempool *pool;
} __attribute__((packed));

struct settings
{
    uint32_t warmup_time;
    uint32_t run_time;
    uint32_t cooldown_time;
} __attribute__((packed));

uint64_t tot_proc_pkts = 0, tot_elapsed = 0;
/*static inline void 
pkt_dump(struct rte_mbuf *buf)
{
    printf("Packet info:\n");
    rte_pktmbuf_dump(stdout, buf, rte_pktmbuf_pkt_len(buf));
}*/

static inline int
ports_init(struct lcore_args *largs,
           uint8_t threadnum)
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
    struct ether_addr myaddr;
    uint16_t port;

    nb_ports = rte_eth_dev_count_avail();
    printf("Number of ports of the server is %" PRIu8 "\n", nb_ports);

    for (i = 0; i < threadnum; i++)
    {
        largs[i].tid = i;
        sprintf(bufpool_name, "bufpool_%d", i);
        largs[i].pool = rte_pktmbuf_pool_create(bufpool_name,
                                                NUM_MBUFS * threadnum, MBUF_CACHE_SIZE, 0,
                                                RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
        if (largs[i].pool == NULL)
        {
            rte_exit(EXIT_FAILURE, "Error: rte_pktmbuf_pool_create failed\n");
        }
        //largs[i].src_id = (int *)malloc(sizeof(int) * nb_ports);
        largs[i].srcIPs.resize(nb_ports);
        largs[i].srcMacs.resize(nb_ports);
        for (port = 0; port < nb_ports; port++)
        {
            rte_eth_macaddr_get(port, &myaddr);
            largs[i].src_id[port] = get_endhost_id(myaddr);
        }
    }

    for (port = 0; port < nb_ports; port++)
    {
        rx_rings = tx_rings = threadnum;
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
lcore_execute(__attribute__((unused)) void *arg)
{
    int n;
    struct lcore_args *myarg;
    uint8_t queue;
    struct rte_mempool *pool;
    volatile enum benchmark_phase *phase;
    struct rte_mbuf *bufs[BATCH_SIZE];
    uint16_t bsz, i, port, nb_ports;
    char *pkt_ptr;
    struct timeval start, end;
    uint64_t elapsed;

    myarg = (struct lcore_args *)arg;
    queue = myarg->tid;
    pool = myarg->pool;
    phase = myarg->phase;
    bsz = BATCH_SIZE;
    nb_ports = rte_eth_dev_count_avail();
    int pktCntr = 0;
    do
    {
        gettimeofday(&start, NULL);

        for (port = 0; port < nb_ports; port++)
        {
            /* Receive and process responses */
            do
            {
                if ((n = rte_eth_rx_burst(port, queue, bufs, bsz)) < 0)
                {
                    rte_exit(EXIT_FAILURE, "Error: rte_eth_rx_burst failed\n");
                }

                for (i = 0; i < n; i++)
                {
                    if ((*phase == BENCHMARK_RUNNING) &&
                        pkt_client_process(bufs[i], myarg->type))
                    {
                        __sync_fetch_and_add(&tot_proc_pkts, 1);
                    }
                }

                for (i = 0; i < n; i++)
                {
                    rte_pktmbuf_free(bufs[i]);
                }

            } while (n == bsz); // More packets in the RX queue

            /* Prepare and send requests */
            for (i = 0; i < bsz; i++)
            {
                if ((bufs[i] = rte_pktmbuf_alloc(pool)) == NULL)
                {
                    break;
                }
            }
            n = i;
            for (i = 0; i < n; i++)
            {
                pkt_ptr = rte_pktmbuf_append(bufs[i], pkt_size(myarg->type));
                pkt_header_build(pkt_ptr, myarg->src_id[port], myarg->des_id,
                                 myarg->type, queue);
                pkt_set_attribute(bufs[i]);
                pkt_client_data_build(pkt_ptr, myarg->type);
                //pkt_dump(bufs[i]);
            }
            i = rte_eth_tx_burst(port, queue, bufs, n);
            pktCntr+=i;
            /* free non-sent buffers */
            for (; i < n; i++)
            {
                rte_pktmbuf_free(bufs[i]);
            }
        }

        gettimeofday(&end, NULL);
        elapsed = (end.tv_sec - start.tv_sec) * 1000000 +
                  (end.tv_usec - start.tv_usec);

        if (*phase == BENCHMARK_RUNNING)
        {
            tot_elapsed += elapsed;
        }

    } while (*phase != BENCHMARK_DONE);
    printf("worker %" PRIu8 " done. counter = %d\n", myarg->tid, pktCntr);

    return 0;
}

int main(int argc, char **argv)
{

    int ret, i;
    unsigned lcore_id;
    uint8_t threadnum;
    struct lcore_args *largs;
    volatile enum benchmark_phase phase;
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
    if (argc != 4)
    {
        printf("Usage: %s <type> <dest IP> <dest MAC>\n", argv[0]);
        rte_exit(EXIT_FAILURE, "Error: invalid arguments\n");
    }
    InitializePayloadConstants();
    /* Initialize NIC ports */
    threadnum = rte_lcore_count() - 1;
    largs = (lcore_args *)calloc(threadnum, sizeof(*largs));
    for (i = 0; i < threadnum; i++)
    {
        largs[i].tid = i;
        largs[i].phase = &phase;
        largs[i].type = (pkt_type)atoi(argv[1]);
        largs[i].des_id = atoi(argv[2]);
    }
    ret = ports_init(largs, threadnum);
    if(ret != 0)
    {
        printf("port init failed. %s.\n", rte_strerror(rte_errno));
    }

    /* Start applications */
    printf("Starting Workers\n");
    phase = BENCHMARK_WARMUP;
    if (mysettings.warmup_time)
    {
        sleep(mysettings.warmup_time);
        printf("Warmup done\n");
    }

    /* call lcore_execute() on every slave lcore */
    RTE_LCORE_FOREACH_SLAVE(lcore_id)
    {
        rte_eal_remote_launch(lcore_execute, (void *)(largs + lcore_id - 1),
                              lcore_id);
    }

    phase = BENCHMARK_RUNNING;
    sleep(mysettings.run_time);

    if (mysettings.cooldown_time)
    {
        printf("Starting cooldown\n");
        phase = BENCHMARK_COOLDOWN;
        sleep(mysettings.cooldown_time);
    }

    phase = BENCHMARK_DONE;
    printf("Benchmark done\n");

    rte_eal_mp_wait_lcore();
    /* print status */
    printf("Latency is %lf us\n", (tot_elapsed + 0.0) / (tot_proc_pkts + 0.0));
    printf("Throughput is %lf reqs/s\n", (tot_proc_pkts + 0.0) /
                                             (mysettings.run_time + 0.0));

    for (i = 0; i < threadnum; i++)
    {
        free(largs->src_id);
    }
    free(largs);
    return 0;
}
