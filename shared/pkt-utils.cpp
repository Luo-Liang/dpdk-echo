/*
 * Packet utilities
 */
#include <stdint.h>

#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_udp.h>
#include <rte_memcpy.h>
#include "dpdk-helpers.h"
#include "pkt-utils.h"
#include <string>
#include <stdexcept>

/* Marcos */
#define ETHER_HEADER_LEN 14
#define IP_HEADER_LEN 20
#define UDP_HEADER_LEN 8
#define UDP_SRC_PORT 1234
#define UDP_DES_PORT 5678

void MACFromString(std::string str, uint8_t Bytes[6])
{
    int bytes[6];
    if (std::sscanf(str.c_str(),
                    "%02x:%02x:%02x:%02x:%02x:%02x",
                    &bytes[0], &bytes[1], &bytes[2],
                    &bytes[3], &bytes[4], &bytes[5]) != 6)
    {
        throw std::runtime_error(str + std::string(" is an invalid MAC address"));
    }
    for (int i = 0; i < 6; i++)
    {
        Bytes[i] = (uint8_t)bytes[i];
    }
}

void IPFromString(std::string str, uint8_t Bytes[4])
{
    int bytes[4];
    if (4 != sscanf(str.c_str(), "%d.%d.%d.%d", bytes, bytes + 1, bytes + 2, bytes + 3))
    {
        throw std::runtime_error(str + std::string(" is an invalid IP address"));
    }

    for (int i = 0; i < 4; i++)
    {
        Bytes[i] = (uint8_t)bytes[i];
    }
}

/* Common Header */
struct common_hdr
{
    struct ether_hdr ether;
    struct ipv4_hdr ip;
    struct udp_hdr udp;
} __attribute__((packed));

/* Application Headers */
//#define ECHO_PAYLOAD_LEN 5
//int ECHO_PAYLOAD_LEN = 0;
std::string contents;
int ECHO_PAYLOAD_LEN = 5;
void InitializePayloadConstants(int len)
{
    assert(contents.size() == 0);
    ECHO_PAYLOAD_LEN = len;
    std::string templatedStr = "PLINK TECHNOLOGIES";
    for (int i = 0; i < ECHO_PAYLOAD_LEN; i++)
    {
        contents += templatedStr.at(i % templatedStr.size());
    }
    //ECHO_PAYLOAD_LEN = pLen;
}

#define ECHO_PAYLOAD_MAXLEN 2000
struct echo_hdr
{
    struct common_hdr pro_hdr;
    char payload[ECHO_PAYLOAD_MAXLEN];
} __attribute__((packed));

uint16_t
pkt_size(enum pkt_type type)
{
    uint16_t ret;

    ret = ETHER_HEADER_LEN + IP_HEADER_LEN + UDP_HEADER_LEN;

    switch (type)
    {
    case ECHO:
        ret += ECHO_PAYLOAD_LEN;
        break;
    default:
        break;
    }

    return ret;
}

uint32_t
ip_2_uint32(uint8_t ip[])
{
    uint32_t myip = 0;
    myip = (ip[3] << 24) + (ip[2] << 16) + (ip[1] << 8) + ip[0];

    return myip;
}

static inline void
pkt_swap_address(struct common_hdr *comhdr)
{
    uint8_t tmp_mac[ETHER_ADDR_LEN];
    uint32_t tmp_ip;
    uint16_t tmp_udp;

    // Destination addr copy
    rte_memcpy(tmp_mac, comhdr->ether.d_addr.addr_bytes, ETHER_ADDR_LEN);
    tmp_ip = comhdr->ip.dst_addr;
    tmp_udp = comhdr->udp.dst_port;

    // SRC -> DST
    rte_memcpy(comhdr->ether.d_addr.addr_bytes, comhdr->ether.s_addr.addr_bytes,
               ETHER_ADDR_LEN);
    comhdr->ip.dst_addr = comhdr->ip.src_addr;
    comhdr->udp.dst_port = comhdr->udp.src_port;

    // DST -> SRC
    rte_memcpy(comhdr->ether.s_addr.addr_bytes, tmp_mac, ETHER_ADDR_LEN);
    comhdr->ip.src_addr = tmp_ip;
    comhdr->udp.src_port = tmp_udp;

    // Clear old checksumcomhdr->ip);
    //comhdr->ip.hdr_checksum = 0;
    //comhdr->udp.dgram_cksum = 0;
    //comhdr->udp.dgram_cksum = rte_ipv4_udptcp_cksum(&comhdr->ip, &comhdr->udp);
}

void pkt_build(char *pkt_ptr,
               endhost &src,
               endhost &des,
               enum pkt_type type,
               uint8_t tid,
               bool manualCksum)
{
    common_hdr *myhdr = (struct common_hdr *)pkt_ptr;

    // Ethernet header
    rte_memcpy(myhdr->ether.d_addr.addr_bytes, des.mac, ETHER_ADDR_LEN);
    rte_memcpy(myhdr->ether.s_addr.addr_bytes, src.mac, ETHER_ADDR_LEN);
    myhdr->ether.ether_type = htons(ETHER_TYPE_IPv4);
    // IP header
    myhdr->ip.version_ihl = 0x45;
    myhdr->ip.total_length = htons(pkt_size(type) - ETHER_HEADER_LEN);
    myhdr->ip.packet_id = htons(44761);
    myhdr->ip.fragment_offset = htons(1 << 14);
    myhdr->ip.time_to_live = 64;
    myhdr->ip.next_proto_id = IPPROTO_UDP;
    myhdr->ip.src_addr = ip_2_uint32(src.ip);
    myhdr->ip.dst_addr = ip_2_uint32(des.ip);
    myhdr->ip.hdr_checksum = 0;
    //if (manualCksum)
    //{
        myhdr->ip.hdr_checksum = rte_ipv4_cksum(&myhdr->ip);
    //}
    //printf("building a udp packet from ip = %d.%d.%d.%d to %d.%d.%d.%d\n", mysrc->ip[0], mysrc->ip[1], mysrc->ip[2], mysrc->ip[3], mydes->ip[0], mydes->ip[1], mydes->ip[2], mydes->ip[3]);
    // UDP header
    myhdr->udp.src_port = htons(UDP_SRC_PORT + tid);
    myhdr->udp.dst_port = htons(UDP_DES_PORT);
    myhdr->udp.dgram_len = htons(pkt_size(type) - ETHER_HEADER_LEN - IP_HEADER_LEN); // -
        //UDP_HEADER_LEN;
    pkt_client_data_build(pkt_ptr, type);
    myhdr->udp.dgram_cksum = 0;
    //if(manualCksum)
    //{
        myhdr->udp.dgram_cksum = rte_ipv4_udptcp_cksum(&myhdr->ip, &myhdr->udp); // | 0; // uhdr.uh_sum = htons(0xba29);
    //}
    //myhdr->udp.dgram_cksum = udp_checksum(&uhdr, myhdr->ip.src_addr, myhdr->ip.dst_addr);
    //printf("ip checksum = %d, udp checksum = %d\n", myhdr->ip.hdr_checksum, myhdr->udp.dgram_cksum);
}

void pkt_set_attribute(struct rte_mbuf *buf, bool manualCksum)
{
    buf->ol_flags |= PKT_TX_IPV4;
    if(manualCksum == false)
    {
        //buf->ol_flags |= PKT_TX_IP_CKSUM; 
    }
    buf->l2_len = sizeof(struct ether_hdr);
    buf->l3_len = sizeof(struct ipv4_hdr);
}

void pkt_client_data_build(char *pkt_ptr,
                           enum pkt_type type)
{
    if (type == ECHO)
    {
        struct echo_hdr *mypkt = (struct echo_hdr *)pkt_ptr;
        rte_memcpy(mypkt->payload, contents.c_str(), ECHO_PAYLOAD_LEN);
    }
    else
    {
        // do nothing
    }
}

int pkt_client_process(struct rte_mbuf *buf,
                       enum pkt_type type,
                       uint32_t ip)
{
    int ret = 0;

    if (type == ECHO)
    {
        struct echo_hdr *mypkt;
        mypkt = rte_pktmbuf_mtod(buf, struct echo_hdr *);
         
        if (mypkt->pro_hdr.ip.src_addr == ip && !memcmp(mypkt->payload, contents.c_str(), ECHO_PAYLOAD_LEN))
        {
            ret = 1;
        }
    }
    else
    {
        // do nothing
    }

    return ret;
}

static void
pkt_server_data_build(char *payload,
                      enum pkt_type type)
{
    if (type == ECHO)
    {
        rte_memcpy(payload, contents.c_str(), ECHO_PAYLOAD_LEN);
    }
    else
    {
        // do nothing
    }
}

int pkt_server_process(struct rte_mbuf *buf,
                       enum pkt_type type)
{
    int ret = 1;

    if (type == ECHO)
    {
        struct echo_hdr *mypkt;

        mypkt = rte_pktmbuf_mtod(buf, struct echo_hdr *);
        if (!memcmp(mypkt->payload, contents.c_str(), ECHO_PAYLOAD_LEN))
        {
            pkt_swap_address(&mypkt->pro_hdr);
            //pkt_server_data_build(mypkt->payload, type);
            ret = 0;
        }
    }
    else
    {
        // do nothing
    }

    return ret;
}

void pkt_dump(struct rte_mbuf *buf)
{
    printf("Packet info:\n");
    rte_pktmbuf_dump(stdout, buf, rte_pktmbuf_pkt_len(buf));
}
