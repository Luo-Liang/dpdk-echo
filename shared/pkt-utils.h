/*
 * Packet utilities header
 */
#ifndef _PKT_UTILS_H
#define _PKT_UTILS_H

#include <stdint.h>
#include <rte_mbuf.h>
#include <rte_ip.h>
#include <string>
#include <cassert>
#include <cstdio>
#include <rte_udp.h>
#include "dpdk-helpers.h"

#define ECHO_PAYLOAD_MAXLEN 2000

struct udphdr
{
  u_short uh_sport; /* source port */
  u_short uh_dport; /* destination port */
  short uh_ulen;    /* udp length */
  u_short uh_sum;   /* udp checksum */
};

union ETHERIP
{
	uint8_t ips[4];
	uint32_t ip;
}__attribute__((packed));


/* Common Header */
struct common_hdr
{
	struct ether_hdr ether;
	struct ipv4_hdr ip;
	struct udp_hdr udp;
} __attribute__((packed));


struct echo_hdr
{
	struct common_hdr pro_hdr;
	unsigned short SEQ;
	unsigned short ROUND;
	char payload[ECHO_PAYLOAD_MAXLEN];
} __attribute__((packed));


void MACFromString(std::string str, uint8_t bytes[6]);
void IPFromString(std::string str, uint8_t bytes[4]);

std::string dbgStringFromIP(uint8_t ip[4]);
std::string dbgStringFromMAC(uint8_t mac[6]);

uint16_t pkt_size();
uint16_t pkt_build(char *pkt_ptr,
			   endhost &src,
			   endhost &des,
			   pkt_type type, 
	       unsigned short  sequenceNumber,
	       unsigned short round);

void pkt_set_attribute(struct rte_mbuf *buf);
uint16_t udp_checksum(udphdr *, uint32_t, uint32_t);
void pkt_prepare_reponse(char* pkt_ptr, unsigned short, unsigned short);
//expectedRemote is compared only if a packet of REQ is determined.
//checksum is compared only if a packet of RES is determined.
pkt_type pkt_process(rte_mbuf *buf, uint32_t expectedRemoteRequesterIP, uint16_t checksumRES, unsigned short &seq, unsigned short &round);
void pkt_prepare_request(char* pkt_ptr, unsigned short, unsigned short);


void pkt_dump(struct rte_mbuf *buf);
uint32_t
ip_2_uint32(uint8_t ip[]);
void InitializePayloadRequest(int);
void InitializePayloadResponse();
#endif /* _PKT_UTILS_H */
