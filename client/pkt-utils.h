/*
 * Packet utilities header
 */
#ifndef _PKT_UTILS_H
#define _PKT_UTILS_H

#include <stdint.h>
#include <rte_mbuf.h>

enum pkt_type {
    ECHO,
};

struct udphdr {
  u_short uh_sport;/* source port */
  u_short uh_dport;/* destination port */
  short uh_ulen;/* udp length */
  u_short uh_sum;/* udp checksum */
};


uint16_t pkt_size (enum pkt_type type);
void pkt_header_build(char *pkt_ptr, int src_id, int des_id, 
                      enum pkt_type type, uint8_t tid);
void pkt_set_attribute(struct rte_mbuf *buf);
void pkt_client_data_build(char *pkt_ptr, enum pkt_type type);
int pkt_client_process(struct rte_mbuf *buf, enum pkt_type type);
uint16_t udp_checksum(udphdr*, uint32_t, uint32_t);
#endif /* _PKT_UTILS_H */
