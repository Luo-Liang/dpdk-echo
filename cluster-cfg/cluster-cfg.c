/*
 * Cluster configuration
 */
#include <stdint.h>
#include <string.h>
#include "cluster-cfg.h"

struct endhost cluster[] = {
    { // nyala
        .id = 0,
        .mac = {0x3c, 0xfd, 0xfe, 0xa1, 0x11, 0x2d},
        .ip = {0x0a, 0x64, 0x0a, 0xf}
    },

    { // okapi
        .id = 1,
        .mac = {0x3c, 0xfd, 0xfe, 0xaa, 0xde, 0xd1},
        .ip = {0x0a, 0x64, 0x0a, 0x10}
    },

    { // guanaco
        .id = 2,
        .mac = {0x3c, 0xfd, 0xfe, 0xaa, 0xd1, 0xe1},
        .ip = {0x0a, 0x64, 0x14, 0x08}
    },

    { // hippopotamus
        .id = 3,
        .mac = {0x68, 0x05, 0xca, 0x33, 0x13, 0x41},
        .ip = {0x0a, 0x64, 0x14, 0x09}
    },

    { // dikdik
        .id = 4,
        .mac = {0x3c, 0xfd, 0xfe, 0xad, 0x84, 0x8d},
        .ip = {0x0a, 0x64, 0x14, 0x05}
    },

    { // fossa
        .id = 5,
        .mac = {0x3c, 0xfd, 0xfe, 0xad, 0xfe, 0x05},
        .ip = {0x0a, 0x64, 0x14, 0x07}
    }
};

int
get_endhost_id (struct ether_addr addr)
{
    uint8_t i;

    for (i = 0; i < sizeof(cluster)/sizeof(struct endhost); i++) {
        if (!memcmp(cluster[i].mac, addr.addr_bytes, ETHER_ADDR_LEN)) {
            return cluster[i].id;
        }
    }

    return -1;
}

struct endhost*
get_endhost (int id)
{
    return &cluster[id];
}
