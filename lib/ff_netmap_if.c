/*****************************************************************************
*    Copyright (C) 2018  LiteSpeed Technologies, Inc.                        *
*                                                                            *
*    This program is free software: you can redistribute it and/or modify    *
*    it under the terms of the GNU General Public License as published by    *
*    the Free Software Foundation, either version 3 of the License, or       *
*    (at your option) any later version.                                     *
*                                                                            *
*    This program is distributed in the hope that it will be useful,         *
*    but WITHOUT ANY WARRANTY; without even the implied warranty of          *
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the            *
*    GNU General Public License for more details.                            *
*                                                                            *
*    You should have received a copy of the GNU General Public License       *
*    along with this program. If not, see http://www.gnu.org/licenses/.      *
*                                                                            *
******************************************************************************/

/**
 * Porting notes:
 *      - Not bothering with cores.
 *      - Sockets ids appear to be tied to cores, so I'm not doing that either.
 */
#include <ctype.h>
#include <sys/types.h>
//#include <sys/param.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/errno.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#include <netinet/in.h>

#include <libio.h>

#include <../freebsd/sys/ctype.h>
#undef __packed
#define __packed        __attribute__((__packed__))

#include <../freebsd/net/ethernet.h>

//#ifndef IFNAMSIZ
//#define IFNAMSIZ 16
//#endif

//#include "net/netmap.h"
#include "net/netmap_user.h"

#include "ff_event.h"
#include "ff_netmap_if.h"
#include "ff_config.h"
#include "ff_veth.h"
#include "ff_host_interface.h"
#include "ff_msg.h"
#include "ff_api.h"


int s_netmapfd = -1;

// Mellanox Linux's driver key
static uint8_t default_rsskey_40bytes[40] = {
    0xd1, 0x81, 0xc6, 0x2c, 0xf7, 0xf4, 0xdb, 0x5b,
    0x19, 0x83, 0xa2, 0xfc, 0x94, 0x3e, 0x1a, 0xdb,
    0xd9, 0x38, 0x9e, 0x6b, 0xd1, 0x03, 0x9c, 0x2c,
    0xa7, 0x44, 0x99, 0xad, 0x59, 0x3d, 0x56, 0xd9,
    0xf3, 0x25, 0x3c, 0x06, 0x2a, 0xdc, 0x1f, 0xfc
};

/** ff_dpfk_if_context is the main structure for everything (unlike in dpdk).
 *  Here what I'll do is allocate an array of these to match the number of 
 *  ports in the system and it can be accessed by port number index or any 
 *  number of other methods
 */

#define MAX_PKT_BURST 32
#define MAX_TX_BURST    (MAX_PKT_BURST / 2)
#define BURST_TX_DRAIN_US 100 /* TX drain every ~100us */
#define US_PER_S 1000000      /* Microseconds per second.  */
struct mbuf_table {
    uint16_t len;
    struct mbuf *m_table[MAX_PKT_BURST];
};

struct ff_netmap_if_context {
    void               *sc;     // veth_softc structure
    void               *ifp;    // In veth_softc structure, the ifnet structure
    uint16_t            port_id;
    struct ff_port_cfg *cfg;
    struct nmreq        req;
    struct netmap_if   *netmapif;
    struct netmap_ring *tx_ring;
    struct netmap_ring *rx_ring;
    char               *map;
    int                 active;
    //int                 tx_queue_id;
    //struct mbuf_table   tx_mbufs; 

    //struct ff_hw_features hw_features;
} ;//__rte_cache_aligned;

static struct ff_netmap_if_context *s_context = NULL;

static struct ff_top_args ff_top_status;
static struct ff_traffic_args ff_traffic;

enum FilterReturn {
    FILTER_UNKNOWN = -1,
    FILTER_ARP = 1,
    FILTER_KNI = 2,
};

/* Ethernet frame types */
#define ETHER_TYPE_IPv4 0x0800 /**< IPv4 Protocol. */
#define ETHER_TYPE_IPv6 0x86DD /**< IPv6 Protocol. */
#define ETHER_TYPE_ARP  0x0806 /**< Arp Protocol. */
#define ETHER_TYPE_RARP 0x8035 /**< Reverse Arp Protocol. */
#define ETHER_TYPE_VLAN 0x8100 /**< IEEE 802.1Q VLAN tagging. */
#define ETHER_TYPE_QINQ 0x88A8 /**< IEEE 802.1ad QinQ tagging. */
#define ETHER_TYPE_1588 0x88F7 /**< IEEE 802.1AS 1588 Precise Time Protocol. */
#define ETHER_TYPE_SLOW 0x8809 /**< Slow protocols (LACP and Marker). */
#define ETHER_TYPE_TEB  0x6558 /**< Transparent Ethernet Bridging. */
#define ETHER_TYPE_LLDP 0x88CC /**< LLDP Protocol. */

extern void ff_hardclock(void);

/*
static void
ff_hardclock_job(void) {
    ff_hardclock();
    ff_update_current_ts();
}
*/
struct ff_netmap_if_context *
ff_netmap_register_if(void *sc, void *ifp, struct ff_port_cfg *cfg)
{
    struct ff_netmap_if_context *ctx = &s_context[cfg->port_id];

    ctx->sc = sc;
    ctx->ifp = ifp;
    ctx->port_id = cfg->port_id;
    //ctx->hw_features = cfg->hw_features;
    strcpy(ctx->req.nr_name, cfg->name);
    ctx->req.nr_version = NETMAP_API;
    if (ioctl(s_netmapfd, NIOCREGIF, (void *)&ctx->req) == -1)
    {
        printf("Error initializing netmap port #%d, %s: %s\n", cfg->port_id,
               cfg->name, strerror(errno));
        return NULL;
    }
    ctx->map = ff_mmap(0, ctx->req.nr_memsize, ff_PROT_READ | ff_PROT_WRITE, 
                       ff_MAP_SHARED, s_netmapfd, 0);
    if (!ctx->map)
    {
        printf("Error creating netmap memory, port #%d, %s: %s\n", cfg->port_id,
               cfg->name, strerror(errno));
        return NULL;
    }
    ctx->netmapif = NETMAP_IF(ctx->map, ctx->req.nr_offset);
    ctx->tx_ring = NETMAP_TXRING(ctx->netmapif, 0);
    ctx->rx_ring = NETMAP_RXRING(ctx->netmapif, 0);
    ctx->active = 1;
    return ctx;
}

void
ff_netmap_deregister_if(struct ff_netmap_if_context *ctx)
{
    //free(ctx);
    // Don't see any netmap functions for this!
    if (ctx->map)
    {
        ff_munmap(ctx->map, ctx->req.nr_memsize);
        ctx->map = NULL;
    }
    ctx->map = NULL;
    ctx->active = 0;        
}

/*
static void
check_all_ports_link_status(void)
{
    // Don't try to bring them up here, we'll do that later
}
*/


static int
init_clock(void)
{
    ff_update_current_ts();

    return 0;
}

int
ff_netmap_init(int argc, char **argv)
{
    struct ff_netmap_if_context *ctx;
    if (s_netmapfd == -1)
    {
        s_netmapfd = open("/dev/netmap", O_RDWR);
        if (s_netmapfd == -1)
            return -1;
    }

    ctx = ff_malloc(sizeof(struct ff_netmap_if_context) * 
                    ff_global_cfg.netmap.nb_ports);
    if (ctx == NULL)
    {
        close(s_netmapfd);
        s_netmapfd = -1;
        return -1;
    }
    memset(ctx, 0, sizeof(struct ff_netmap_if_context) * 
                   ff_global_cfg.netmap.nb_ports);
    s_context = ctx;
    
    int i;
    for (i = 0; i < ff_global_cfg.netmap.nb_ports; ++i)
    {
        ctx[i].cfg = &ff_global_cfg.netmap.port_cfgs[i];
        ctx[i].port_id = i;
    }

    init_clock();

    return 0;
}


static char *netmap_read(struct ff_netmap_if_context *ctx, int *len)
{
    struct pollfd pfd;
    
    pfd.fd = s_netmapfd;
    pfd.events = POLLIN;
    pfd.revents = 0;
        
    if (poll(&pfd, 1, 0) == -1)
    {
        return NULL;
    }
        
    if (!(pfd.revents & POLLIN))
    {
        return NULL;
    }
    
    struct netmap_slot *slot = &ctx->rx_ring->slot[ctx->rx_ring->head];
    char *data = NETMAP_BUF(ctx->rx_ring,
                            slot->buf_idx);
    *len = slot->len;
    return data;
}


static void
ff_veth_input(const struct ff_netmap_if_context *ctx, char *data, int len)
{
    /* For testing purposes, receive and process only one packet at a time.  */
    if (!data)
        return;
    
    //struct netmap_slot *slot = &ctx->rx_ring->slot[ctx->rx_ring->head];
    void *hdr = ff_mbuf_gethdr(NULL, len, data, len, 0/*rx_csum*/);

    ff_veth_process_packet(ctx->ifp, hdr);
    
    ctx->rx_ring->head = nm_ring_next(ctx->rx_ring, ctx->rx_ring->head);
    // consume the packet
    ioctl(s_netmapfd, NIOCTXSYNC, 0);
}


static enum FilterReturn
protocol_filter(const void *data, uint16_t len)
{
    if(len < ETHER_HDR_LEN)
        return FILTER_UNKNOWN;

    const struct ether_header *hdr;
    hdr = (const struct ether_header *)data;

    if(ntohs(hdr->ether_type) == ETHER_TYPE_ARP)
        return FILTER_ARP;

#ifndef FF_KNI
    return FILTER_UNKNOWN;
#else
    if (!enable_kni) {
        return FILTER_UNKNOWN;
    }

    if(ntohs(hdr->ether_type) != ETHER_TYPE_IPv4)
        return FILTER_UNKNOWN;

    return ff_kni_proto_filter(data + ETHER_HDR_LEN,
        len - ETHER_HDR_LEN);
#endif
}


static inline void
process_packets(uint16_t port_id, const struct ff_netmap_if_context *ctx,
                char *data, int len)
{
    enum FilterReturn filter = protocol_filter(data, len);
    if (filter == FILTER_ARP) {
        // The original code has it deep copied.  For now just don't dequeue it

#ifdef FF_KNI
        if (enable_kni && rte_eal_process_type() == RTE_PROC_PRIMARY) {
            mbuf_pool = pktmbuf_pool[qconf->socket_id];
            mbuf_clone = pktmbuf_deep_clone(rtem, mbuf_pool);
            if(mbuf_clone) {
                ff_kni_enqueue(port_id, mbuf_clone);
            }
        }
#endif
        //..and ignore it for now...
        //ff_veth_input(ctx, data, len);
#ifdef FF_KNI
    } else if (enable_kni &&
        ((filter == FILTER_KNI && kni_accept) ||
         (filter == FILTER_UNKNOWN && !kni_accept)) ) {
        ff_kni_enqueue(port_id, rtem);
#endif
    } else {
        ff_veth_input(ctx, data, len);
    }
}


static inline void
handle_sysctl_msg(struct ff_msg *msg)
{
    int ret;
    ret = ff_sysctl(msg->sysctl.name, msg->sysctl.namelen, msg->sysctl.old, 
                    msg->sysctl.oldlenp, msg->sysctl.new, msg->sysctl.newlen);

    if (ret < 0) {
        msg->result = errno;
    } else {
        msg->result = 0;
    }
}

static inline void
handle_ioctl_msg(struct ff_msg *msg)
{
    int fd, ret;
    fd = ff_socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        ret = -1;
        goto done;
    }

    ret = ff_ioctl_freebsd(fd, msg->ioctl.cmd, msg->ioctl.data);

    ff_close(fd);

done:
    if (ret < 0) {
        msg->result = errno;
    } else {
        msg->result = 0;
    }
}

static inline void
handle_route_msg(struct ff_msg *msg)
{
    int ret = ff_rtioctl(msg->route.fib, msg->route.data, &msg->route.len, 
                         msg->route.maxlen);
    if (ret < 0) {
        msg->result = errno;
    } else {
        msg->result = 0;
    }
}

static inline void
handle_top_msg(struct ff_msg *msg)
{
    msg->top = ff_top_status;
    msg->result = 0;
}

#ifdef FF_NETGRAPH
static inline void
handle_ngctl_msg(struct ff_msg *msg)
{
    int ret = ff_ngctl(msg->ngctl.cmd, msg->ngctl.data);
    if (ret < 0) {
        msg->result = errno;
    } else {
        msg->result = 0;
        msg->ngctl.ret = ret;
    }
}
#endif

#ifdef FF_IPFW
static inline void
handle_ipfw_msg(struct ff_msg *msg)
{
    int fd, ret;
    fd = ff_socket(AF_INET, SOCK_RAW, IPPROTO_RAW);
    if (fd < 0) {
        ret = -1;
        goto done;
    }

    switch (msg->ipfw.cmd) {
        case FF_IPFW_GET:
            ret = ff_getsockopt_freebsd(fd, msg->ipfw.level, msg->ipfw.optname, 
                                        msg->ipfw.optval, msg->ipfw.optlen);
            break;
        case FF_IPFW_SET:
            ret = ff_setsockopt_freebsd(fd, msg->ipfw.level, msg->ipfw.optname, 
                                        msg->ipfw.optval, *(msg->ipfw.optlen)); 
            break;
        default:
            ret = -1;
            errno = ENOTSUP;
            break;
    }

    ff_close(fd);

done:
    if (ret < 0) {
        msg->result = errno;
    } else {
        msg->result = 0;
    }
}
#endif

static inline void
handle_traffic_msg(struct ff_msg *msg)
{
    msg->traffic = ff_traffic;
    msg->result = 0;
}

static inline void
handle_default_msg(struct ff_msg *msg)
{
    msg->result = ENOTSUP;
}

static inline void
handle_msg(struct ff_msg *msg, uint16_t proc_id)
{
    switch (msg->msg_type) {
        case FF_SYSCTL:
            handle_sysctl_msg(msg);
            break;
        case FF_IOCTL:
            handle_ioctl_msg(msg);
            break;
        case FF_ROUTE:
            handle_route_msg(msg);
            break;
        case FF_TOP:
            handle_top_msg(msg);
            break;
#ifdef FF_NETGRAPH
        case FF_NGCTL:
            handle_ngctl_msg(msg);
            break;
#endif
#ifdef FF_IPFW
        case FF_IPFW_CTL:
            handle_ipfw_msg(msg);
            break;
#endif
        case FF_TRAFFIC:
            handle_traffic_msg(msg);
            break;
        default:
            handle_default_msg(msg);
            break;
    }
    // No queueing for now.
    //rte_ring_enqueue(msg_ring[proc_id].ring[1], msg);
}


static inline int
process_msg_ring(uint16_t proc_id)
{
    /* No queueing for now
    void *msg;
    int ret = rte_ring_dequeue(msg_ring[proc_id].ring[0], &msg);

    if (unlikely(ret == 0)) {
        handle_msg((struct ff_msg *)msg, proc_id);
    }
    */
    return 0;
}

int
ff_netmap_if_send(struct ff_netmap_if_context *ctx, void *m,
    int total)
{
    /* For testing purposes, simply send the packet RIGHT NOW! */
    /* Do not queue it, pass it along.  */
    /* Do NOT go through the intermediate rte_mempool.  We'll do our own anyway*/
    int ret;
    struct pollfd pfd;
    pfd.fd = s_netmapfd;
    pfd.events = POLLOUT;
    pfd.revents = 0;
        
    if (poll(&pfd, 1, 0) == -1)
    {
        int err = errno;
        ff_mbuf_free(m);
        errno = err;
        return -1;
    }
        
    if (!(pfd.revents & POLLOUT))
    {
        // Don't free the buffer?
        errno = ENOBUFS;
        return -1;
    }
    
    struct netmap_slot *slot = &ctx->tx_ring->slot[ctx->tx_ring->head];
    ret = ff_mbuf_copydata(m, NETMAP_BUF(ctx->tx_ring, 
                                         slot->buf_idx), 
                           0, total);
    slot->len = total;
    ff_mbuf_free(m);
    if (ret == -1)
    {
        return -1;
    }
    ctx->tx_ring->head = nm_ring_next(ctx->tx_ring, ctx->tx_ring->head);
    ret = ioctl(s_netmapfd, NIOCTXSYNC, 0);
    return ret;
}

static int
main_loop(loop_func_t loop, void *arg)
{
    //struct mbuf *pkts_burst[MAX_PKT_BURST];
#define EXPIRE_NS       10    
    uint64_t /*prev_tsc, */cur_tsc, usch_tsc, div_tsc, usr_tsc, sys_tsc, 
             end_tsc, idle_sleep_tsc, expire_tsc;
    int i, idle;
    uint16_t port_id;
    const uint64_t drain_tsc = (ff_get_tsc_ns() + US_PER_S - 1) /
                                US_PER_S * BURST_TX_DRAIN_US;
    struct ff_netmap_if_context *ctx;

    //prev_tsc = 0;
    usch_tsc = 0;

    while (1) {
        cur_tsc = ff_get_tsc_ns();
        expire_tsc = cur_tsc + EXPIRE_NS;
        if (unlikely(expire_tsc < cur_tsc)) {
            //timer_manage(); Keep this as a reminder to watch a timer (DPDK only?).
        }

        idle = 1;
        sys_tsc = 0;
        usr_tsc = 0;

        /*
         * TX burst queue drain
         */
        /* For now, no bursting.  Send when we have one packet
        diff_tsc = cur_tsc - prev_tsc;
        if (unlikely(diff_tsc > drain_tsc)) {
            for (i = 0; i < ff_global_cfg.netmap.nb_ports; i++) 
            {
                if (s_context[i].tx_mbufs[port_id].len == 0)
                    continue;

                idle = 0;
                send_burst(i);
            }

            prev_tsc = cur_tsc;
        }
        */
        /*
         * Read packet from RX queues
         */
        for (i = 0; i < ff_global_cfg.netmap.nb_ports; ++i) {
            port_id = s_context[i].port_id;
            ctx = &s_context[port_id];
            int len;
            char *data = netmap_read(ctx, &len);
            if (!data)
                continue;
            idle = 0;
            /* Prefetch and handle already prefetched packets */
            //for (j = 0; j < (nb_rx - PREFETCH_OFFSET); j++) {
            //    rte_prefetch0(rte_pktmbuf_mtod(pkts_burst[
            //            j + PREFETCH_OFFSET], void *));
            process_packets(port_id, ctx, data, len);
            //}

        }

        // No rings for now.
        //process_msg_ring(qconf->proc_id);

        div_tsc = ff_get_tsc_ns();//rte_rdtsc();

        if (likely(loop != NULL && (!idle || cur_tsc - usch_tsc > drain_tsc))) {
            usch_tsc = cur_tsc;
            loop(arg);
        }

        idle_sleep_tsc = ff_get_tsc_ns();//rte_rdtsc();
        end_tsc = idle_sleep_tsc;

        end_tsc = ff_get_tsc_ns();//rte_rdtsc();

        if (usch_tsc == cur_tsc) {
            usr_tsc = idle_sleep_tsc - div_tsc;
        }

        if (!idle) {
            sys_tsc = div_tsc - cur_tsc;
            ff_top_status.sys_tsc += sys_tsc;
        }

        ff_top_status.usr_tsc += usr_tsc;
        ff_top_status.work_tsc += end_tsc - cur_tsc;
        ff_top_status.idle_tsc += end_tsc - cur_tsc - usr_tsc - sys_tsc;

        ff_top_status.loops++;
    }

    return 0;
}

int
ff_netmap_if_up(void) {
    int i;
    for (i = 0; i < ff_global_cfg.netmap.nb_ports; i++) {
        if (ff_veth_attach(&ff_global_cfg.netmap.port_cfgs[i]) == NULL)
        {
            return -1;
        }
    }

    return 0;
}


// EXPORTED AS ff_run!!!
void
ff_netmap_run(loop_func_t loop, void *arg) {
    main_loop(loop, arg);
    //struct loop_routine *lr = rte_malloc(NULL,
    //    sizeof(struct loop_routine), 0);
    //lr->loop = loop;
    //lr->arg = arg;
    //rte_eal_mp_remote_launch(main_loop, lr, CALL_MASTER);
    //rte_eal_mp_wait_lcore();
    //rte_free(lr);
}

void
ff_netmap_pktmbuf_free(void *m)
{
    // I believe we don't need to free anything (since it's mmap'd).
    //rte_pktmbuf_free((struct rte_mbuf *)m);
}

static uint32_t
toeplitz_hash(unsigned keylen, const uint8_t *key,
    unsigned datalen, const uint8_t *data)
{
    uint32_t hash = 0, v;
    u_int i, b;

    /* XXXRW: Perhaps an assertion about key length vs. data length? */

    v = (key[0]<<24) + (key[1]<<16) + (key[2] <<8) + key[3];
    for (i = 0; i < datalen; i++) {
        for (b = 0; b < 8; b++) {
            if (data[i] & (1<<(7-b)))
                hash ^= v;
            v <<= 1;
            if ((i + 4) < keylen &&
                (key[i+4] & (1<<(7-b))))
                v |= 1;
        }
    }
    return (hash);
}

int
ff_rss_check(void *softc, uint32_t saddr, uint32_t daddr,
    uint16_t sport, uint16_t dport)
{
#define ETH_RSS_RETA_NUM_ENTRIES 128 // The default
    uint16_t reta_size = ETH_RSS_RETA_NUM_ENTRIES;//rss_reta_size[ctx->port_id];
    //uint16_t queueid = qconf->tx_queue_id[ctx->port_id];

    uint8_t data[sizeof(saddr) + sizeof(daddr) + sizeof(sport) +
        sizeof(dport)];

    unsigned datalen = 0;

    bcopy(&saddr, &data[datalen], sizeof(saddr));
    datalen += sizeof(saddr);

    bcopy(&daddr, &data[datalen], sizeof(daddr));
    datalen += sizeof(daddr);

    bcopy(&sport, &data[datalen], sizeof(sport));
    datalen += sizeof(sport);

    bcopy(&dport, &data[datalen], sizeof(dport));
    datalen += sizeof(dport);

    uint32_t hash = toeplitz_hash(sizeof(default_rsskey_40bytes),
        default_rsskey_40bytes, datalen, data);

    return ((hash & (reta_size - 1)) % reta_size) == 0;//queueid;
}

uint64_t
ff_get_tsc_ns()
{
    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = 0;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (ts.tv_sec * 1000000000ull + ts.tv_nsec);
}

