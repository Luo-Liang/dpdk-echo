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
#include "dpdk-helpers.h"

struct udphdr
{
  u_short uh_sport; /* source port */
  u_short uh_dport; /* destination port */
  short uh_ulen;    /* udp length */
  u_short uh_sum;   /* udp checksum */
};

void MACFromString(std::string str, uint8_t bytes[6]);
void IPFromString(std::string str, uint8_t bytes[4]);

uint16_t pkt_size();
void pkt_build(char *pkt_ptr,
			   endhost &src,
			   endhost &des,
			   pkt_type type, 
	       unsigned short  sequenceNumber,
	       unsigned short round);

void pkt_set_attribute(struct rte_mbuf *buf);
uint16_t udp_checksum(udphdr *, uint32_t, uint32_t);
void pkt_prepare_reponse(char* pkt_ptr, unsigned short, unsigned short);
pkt_type pkt_process(rte_mbuf *buf, uint32_t ip, unsigned short&, unsigned short&);
void pkt_prepare_request(char* pkt_ptr, unsigned short, unsigned short);


void pkt_dump(struct rte_mbuf *buf);
uint32_t
ip_2_uint32(uint8_t ip[]);
void InitializePayloadRequest(int);
void InitializePayloadResponse();
#endif /* _PKT_UTILS_H */
