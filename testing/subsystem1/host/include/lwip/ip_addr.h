#ifndef HOST_IP_ADDR_H
#define HOST_IP_ADDR_H
#include "apps/mqtt.h"
typedef struct { u32_t addr; } ip_addr_t;
#define ip_2_ip4(addr) (addr)
#define IPADDR_TYPE_V4 0
#define IP_SET_TYPE(addr, type) ((void)(addr), (void)(type))
#endif
