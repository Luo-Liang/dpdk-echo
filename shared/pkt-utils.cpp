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

std::string dbgStringFromIP(uint8_t ip[4])
{
	std::string ret = std::to_string(ip[0]) + "." + std::to_string(ip[1]) + "." + std::to_string(ip[2]) + "." + std::to_string(ip[3]);
	return ret;
}

std::string dbgStringFromMAC(uint8_t mac[6])
{
	std::string ret = std::to_string(mac[0]) + ":" + std::to_string(mac[1]) + ":" + std::to_string(mac[2]) + ":" + std::to_string(mac[3]) + ":" + std::to_string(mac[4]) + ":" + std::to_string(mac[5]);
	return ret;
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
std::string reqContents;
int ECHO_PAYLOAD_LEN = 5;
void InitializePayloadRequest(int len)
{
	assert(reqContents.size() == 0);
	ECHO_PAYLOAD_LEN = len;
	std::string templatedStr = "REQPLINK";
	for (int i = 0; i < ECHO_PAYLOAD_LEN; i++)
	{
		reqContents += templatedStr.at(i % templatedStr.size());
	}
	//ECHO_PAYLOAD_LEN = pLen;
}

std::string responseContents;
void InitializePayloadResponse()
{
	assert(responseContents.size() == 0);
	assert(ECHO_PAYLOAD_LEN > 0);
	std::string templatedStr = "RESPONSEPLINK";
	for (int i = 0; i < ECHO_PAYLOAD_LEN; i++)
	{
		responseContents += templatedStr.at(i % templatedStr.size());
	}
	//ECHO_PAYLOAD_LEN = pLen;
}

#define ECHO_PAYLOAD_MAXLEN 2000
struct echo_hdr
{
	struct common_hdr pro_hdr;
	unsigned short SEQ;
	unsigned short ROUND;
	char payload[ECHO_PAYLOAD_MAXLEN];
} __attribute__((packed));

uint16_t
pkt_size()
{
	uint16_t ret;
	ret = ETHER_HEADER_LEN + IP_HEADER_LEN + UDP_HEADER_LEN;
	ret += ECHO_PAYLOAD_LEN;
	ret += sizeof(int);
	return ret;
}

uint32_t
ip_2_uint32(uint8_t ip[])
{
	uint32_t myip = 0;
	myip = (ip[3] << 24) + (ip[2] << 16) + (ip[1] << 8) + ip[0];

	return myip;
}

void pkt_build(char *pkt_ptr,
			   endhost &src,
			   endhost &des,
			   pkt_type type, unsigned short sequenceNumber, unsigned short round)
{
	common_hdr *myhdr = (struct common_hdr *)pkt_ptr;

	// Ethernet header
	rte_memcpy(myhdr->ether.d_addr.addr_bytes, des.mac, ETHER_ADDR_LEN);
	rte_memcpy(myhdr->ether.s_addr.addr_bytes, src.mac, ETHER_ADDR_LEN);
	myhdr->ether.ether_type = htons(ETHER_TYPE_IPv4);
	// IP header
	myhdr->ip.version_ihl = 0x45;
	myhdr->ip.total_length = htons(pkt_size() - ETHER_HEADER_LEN);
	myhdr->ip.packet_id = htons(sequenceNumber ^ round);
	myhdr->ip.fragment_offset = htons(1 << 14);
	myhdr->ip.time_to_live = 64;
	myhdr->ip.next_proto_id = IPPROTO_UDP;
	myhdr->ip.src_addr = ip_2_uint32(src.ip);
	myhdr->ip.dst_addr = ip_2_uint32(des.ip);
	myhdr->ip.hdr_checksum = 0;
	myhdr->ip.hdr_checksum = rte_ipv4_cksum(&myhdr->ip);
	//}
	//printf("building a udp packet from ip = %d.%d.%d.%d to %d.%d.%d.%d\n", mysrc->ip[0], mysrc->ip[1], mysrc->ip[2], mysrc->ip[3], mydes->ip[0], mydes->ip[1], mydes->ip[2], mydes->ip[3]);
	// UDP header
	myhdr->udp.src_port = htons(UDP_SRC_PORT);
	myhdr->udp.dst_port = htons(UDP_DES_PORT);
	myhdr->udp.dgram_len = htons(pkt_size() - ETHER_HEADER_LEN - IP_HEADER_LEN); // -
	//UDP_HEADER_LEN;
	if (type == pkt_type::ECHO_REQ)
	{
		pkt_prepare_request(pkt_ptr, sequenceNumber, round);
	}
	else if (type == pkt_type::ECHO_RES)
	{
		pkt_prepare_reponse(pkt_ptr, sequenceNumber, round);
	}
	else
	{
		assert(false);
	}
	myhdr->udp.dgram_cksum = 0;
	myhdr->udp.dgram_cksum = rte_ipv4_udptcp_cksum(&myhdr->ip, &myhdr->udp); // | 0; // uhdr.uh_sum = htons(0xba29);
																			 //}
																			 //myhdr->udp.dgram_cksum = udp_checksum(&uhdr, myhdr->ip.src_addr, myhdr->ip.dst_addr);
	myhdr->ip.hdr_checksum = 0;
	myhdr->ip.hdr_checksum = rte_ipv4_cksum(&myhdr->ip);																		 //printf("ip checksum = %d, udp checksum = %d\n", myhdr->ip.hdr_checksum, myhdr->udp.dgram_cksum);
}

void pkt_set_attribute(struct rte_mbuf *buf)
{
        buf->ol_flags = PKT_TX_IPV4;
	buf->l2_len = sizeof(struct ether_hdr);
	buf->l3_len = sizeof(struct ipv4_hdr);
}

void pkt_prepare_request(char *pkt_ptr, unsigned short sequence, unsigned short round)
{
	echo_hdr *mypkt = (echo_hdr *)pkt_ptr;
	mypkt->SEQ = sequence;
	mypkt->ROUND = round;
	rte_memcpy(mypkt->payload, reqContents.c_str(), ECHO_PAYLOAD_LEN);
}

//seq is only populated if a valid response is received.
pkt_type pkt_process(rte_mbuf *buf, uint32_t dstip, uint32_t& srcip, unsigned short &seq, unsigned short &round)
{
	echo_hdr *mypkt = rte_pktmbuf_mtod(buf, echo_hdr *);
	seq = mypkt->SEQ;
	round = mypkt->ROUND;
	srcip = mypkt->pro_hdr.ip.src_addr;
	if (mypkt->pro_hdr.ip.dst_addr == ip )
	{
		if (memcmp(mypkt->payload, reqContents.c_str(), ECHO_PAYLOAD_LEN) == 0)
		{
			return pkt_type::ECHO_REQ;
		}
		else if (memcmp(mypkt->payload, responseContents.c_str(), ECHO_PAYLOAD_LEN) == 0)
		{
			return pkt_type::ECHO_RES;
		}
		else
		{
			return pkt_type::ECHO_IRRELEVANT;
		}
	}
	else
	{
		return pkt_type::ECHO_IRRELEVANT;
	}
}

void pkt_prepare_reponse(char *pkt_ptr, unsigned short sequence, unsigned short round)
{
	echo_hdr *mypkt = (echo_hdr *)pkt_ptr;
	mypkt->SEQ = sequence;
	mypkt->ROUND = round;
	rte_memcpy(mypkt->payload, responseContents.c_str(), ECHO_PAYLOAD_LEN);
}

void pkt_dump(struct rte_mbuf *buf)
{
	printf("Packet info:\n");
	rte_pktmbuf_dump(stdout, buf, rte_pktmbuf_pkt_len(buf));
}
