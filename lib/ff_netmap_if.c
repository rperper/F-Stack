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
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/timerfd.h>
#include <netinet/in.h>
#include <net/if.h>
#include <arpa/inet.h>

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
    int                 fd;
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

int s_netmap_if_timer = -1;

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

#ifdef FF_WANTDEBUG
debug_fn_t ff_debug = NULL;
void ff_setdebug(debug_fn_t dbg_fn)
{
    ff_debug = dbg_fn;
}
#endif

#define ASCII_IZE(c)    (((c >= ' ') && (c <= '~')) ? c : '.')
void ASCII_dump(char *comment, char *dat, int length)
{
    int i;
    int end = (length / 16) * 16;
    unsigned char *data = (unsigned char *)dat;
    struct timespec ts;
    struct tm tms;
    clock_gettime(CLOCK_REALTIME, &ts);
    localtime_r(&ts.tv_sec, &tms);
   
    printf("%02u:%02u:%02u.%06lu %s, length: %d\n", tms.tm_hour, tms.tm_min, 
           tms.tm_sec, ts.tv_nsec / 1000, comment, length);
    for (i = 0; i < end; i+=16)
    {
        printf("0x%04x:  %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x"
               " %02x%02x %02x%02x  %c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c\n",
               i, data[i], data[i+1], data[i+2], data[i+3], data[i+4], data[i+5],
               data[i+6], data[i+7], data[i+8], data[i+9], data[i+10], data[i+11],
               data[i+12], data[i+13], data[i+14], data[i+15], ASCII_IZE(data[i]),
               ASCII_IZE(data[i+1]), ASCII_IZE(data[i+2]), ASCII_IZE(data[i+3]),
               ASCII_IZE(data[i+4]), ASCII_IZE(data[i+5]), ASCII_IZE(data[i+6]),
               ASCII_IZE(data[i+7]), ASCII_IZE(data[i+8]), ASCII_IZE(data[i+9]),
               ASCII_IZE(data[i+10]), ASCII_IZE(data[i+11]), ASCII_IZE(data[i+12]),
               ASCII_IZE(data[i+13]), ASCII_IZE(data[i+14]), ASCII_IZE(data[i+15]));
    }
    if (end == length)
        return;
    for (i = end; i < end + 16; ++i)
    {
        if (i == end)
            printf("0x%04x:  ", i);
        if (i < length)
            printf("%02x", data[i]);
        else
            printf("  ");
        if (i % 2) 
            printf(" ");
    }
    printf(" ");
    for (i = end; i < length; ++i)
        printf("%c", ASCII_IZE(data[i]));
    printf("\n");
}
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
    
    ctx->fd = open("/dev/netmap", O_RDWR);
    if (ctx->fd == -1)
    {
        fprintf(stderr, "Error opening /dev/netmap: %s\n", strerror(errno));
        return NULL;
    }
    
    ctx->sc = sc;
    ctx->ifp = ifp;
    ctx->port_id = cfg->port_id;
    //ctx->hw_features = cfg->hw_features;
    strcpy(ctx->req.nr_name, cfg->name);
    ctx->req.nr_version = NETMAP_API;
    if (ioctl(ctx->fd, NIOCREGIF, (void *)&ctx->req) == -1)
    {
        fprintf(stderr, "Error initializing netmap port #%d, %s: %s\n", 
                cfg->port_id, cfg->name, strerror(errno));
        return NULL;
    }
    ctx->map = ff_mmap(0, ctx->req.nr_memsize, ff_PROT_READ | ff_PROT_WRITE, 
                       ff_MAP_SHARED, ctx->fd, 0);
    if (!ctx->map)
    {
        fprintf(stderr, "Error creating netmap memory, port #%d, %s: %s\n", 
                cfg->port_id, cfg->name, strerror(errno));
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
    if (ctx->fd > 0)
    {
        close(ctx->fd);
        ctx->fd = -1;
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
    s_netmap_if_timer = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    if (s_netmap_if_timer == -1)
    {
        fprintf(stderr, "Error creating F-Stack/Netmap timer: %s\n", 
                strerror(errno));
        return -1;
    }
    /* The original code:
    rte_timer_subsystem_init();
    uint64_t hz = rte_get_timer_hz();
    uint64_t intrs = MS_PER_S/ff_global_cfg.freebsd.hz;
    uint64_t tsc = (hz + MS_PER_S - 1) / MS_PER_S*intrs;

    rte_timer_init(&freebsd_clock);
    rte_timer_reset(&freebsd_clock, tsc, PERIODICAL,
        rte_lcore_id(), &ff_hardclock_job, NULL);
    Since I have no idea of the original values, I'm simply going to use 10ms. */
    struct itimerspec timerspec;
    timerspec.it_interval.tv_sec = 0;
    timerspec.it_interval.tv_nsec = 1000000l; //1 million
    timerspec.it_value.tv_sec = timerspec.it_interval.tv_sec;
    timerspec.it_value.tv_nsec = timerspec.it_interval.tv_nsec;
    if (timerfd_settime(s_netmap_if_timer, 0 /* relative timer */, 
                        &timerspec, NULL) == -1)
    {
        fprintf(stderr, "Error setting timer for F-Stack/Netmap timer: %s\n",
                strerror(errno));
        return -1;
    }
    
    ff_update_current_ts();

    return 0;
}

int
ff_netmap_init(int argc, char **argv)
{
    struct ff_netmap_if_context *ctx;
    ctx = ff_malloc(sizeof(struct ff_netmap_if_context) * 
                    ff_global_cfg.netmap.nb_ports);
    if (ctx == NULL)
    {
        return -1;
    }
    memset(ctx, 0, sizeof(struct ff_netmap_if_context) * 
                   ff_global_cfg.netmap.nb_ports);
    s_context = ctx;
    
    int i;
    for (i = 0; i < ff_global_cfg.netmap.nb_ports; ++i)
    {
        ctx[i].fd = -1;
        ctx[i].cfg = &ff_global_cfg.netmap.port_cfgs[i];
        ctx[i].port_id = i;
    }

    init_clock();

    return 0;
}


static void
ff_veth_input(const struct ff_netmap_if_context *ctx, char *data, int len)
{
    /* For testing purposes, receive and process only one packet at a time.  */
    if (!data)
    {
        fprintf(stderr, "ff_veth_input, NULL data\n");
        return;
    }
#ifdef FF_WANTDEBUG    
    if (ff_debug)
        ff_debug("Input packet %d bytes\n", len);
#endif    
    //ASCII_dump("Input packet", data, len);
    void *hdr = ff_mbuf_gethdr(NULL, len, data, len, 0/*rx_csum*/);

    ff_veth_process_packet(ctx->ifp, hdr);
    
    ctx->rx_ring->cur = ctx->rx_ring->head = nm_ring_next(ctx->rx_ring,
                                                          ctx->rx_ring->head);
    // consume the packet
    ioctl(ctx->fd, NIOCRXSYNC, 0);
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
        ff_veth_input(ctx, data, len);
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


int netmap_read_process(short event, void *arg)
{
    struct ff_netmap_if_context *ctx = (struct ff_netmap_if_context *)arg;
    if (!(event & POLLIN))
        return 0;
    struct netmap_slot *slot = &ctx->rx_ring->slot[ctx->rx_ring->head];
    char *data = NETMAP_BUF(ctx->rx_ring, slot->buf_idx);
    int len = slot->len;
    process_packets(ctx->port_id, ctx, data, len);
    return 1;
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

int s_packet_num = 0;
int
ff_netmap_if_send(struct ff_netmap_if_context *ctx, void *m,
    int total)
{
    /* For testing purposes, simply send the packet RIGHT NOW! */
    /* Do not queue it, pass it along.  */
    /* Do NOT go through the intermediate rte_mempool.  We'll do our own anyway*/
    int ret;
    struct pollfd pfd;
    pfd.fd = ctx->fd;
    pfd.events = POLLOUT | POLLERR;
    pfd.revents = 0;

    s_packet_num++;
    if (poll(&pfd, 1, 0) == -1)
    {
        int err = errno;
        ff_mbuf_free(m);
        errno = err;
        return errno;
    }
        
    if (!(pfd.revents & POLLOUT))
    {
        ff_mbuf_free(m);
        return EWOULDBLOCK;
    }
    //struct mbuf *mb = (struct mbuf *)m;
    
    struct netmap_slot *slot = &ctx->tx_ring->slot[ctx->tx_ring->head];
    ret = ff_mbuf_copydata(m, NETMAP_BUF(ctx->tx_ring, 
                                         slot->buf_idx), 
                           0, total);
    slot->len = total;
    ff_mbuf_free(m);
    if (ret != 0)
    {
        fprintf(stderr, "ff_mbuf_copydata FAILED\n");
        return E2BIG;
    }
#ifdef FF_WANTDEBUG    
    if (ff_debug)
        ff_debug("Output packet %d bytes\n", total);
#endif    
    //ASCII_dump("Output packet", NETMAP_BUF(ctx->tx_ring, slot->buf_idx), total);
   
    ctx->tx_ring->cur = ctx->tx_ring->head = nm_ring_next(ctx->tx_ring,
                                                          ctx->tx_ring->head);
    ret = ioctl(ctx->fd, NIOCTXSYNC, 0);
    return (ret == 0) ? 0 : errno;
}

#ifndef FF_NETMAP
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

    //printf("Enter main loop\n");
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
            struct pollfd pfd;
    
            pfd.fd = ctx->fd;
            pfd.events = POLLIN;
            pfd.revents = 0;
        
            if (poll(&pfd, 1, 0) == -1)
            {
                continue;
            }
        
            if (!(pfd.revents & POLLIN))
            {
                continue;
            }
    
            if (netmap_read_process(pfd.revents, ctx) == 1)
                idle = 0;
            /* Prefetch and handle already prefetched packets */
            //for (j = 0; j < (nb_rx - PREFETCH_OFFSET); j++) {
            //    rte_prefetch0(rte_pktmbuf_mtod(pkts_burst[
            //            j + PREFETCH_OFFSET], void *));
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
#endif


static char *
get_port_addr(int sd, int IOCTL, struct ifreq *ifr, const char *task)
{
    char *str;
    if (ioctl(sd, IOCTL, ifr) == -1)
    {
        fprintf(stderr, "Error getting %s: %s\n", task, strerror(errno));
        close(sd);
        return NULL;
    }
    str = inet_ntoa(((struct sockaddr_in *)&ifr->ifr_addr)->sin_addr);
    if (!str)
    {
        fprintf(stderr, "Error in return address of %s\n", task);
        close(sd);
        return NULL;
    }
    str = strdup(str);
    if (!str)
    {
        fprintf(stderr, "Can't even dup a string during %s\n", task);
        close(sd);
        return NULL;
    }
    return str;
}


int
ff_netmap_if_up(void) {
    int i;
    
    int sd = socket(PF_INET, SOCK_DGRAM, 0);
    if (sd < 0)
    {
        fprintf(stderr, "Error getting socket to get if info: %s\n", 
                strerror(errno));
        return -1;
    }
    for (i = 0; i < ff_global_cfg.netmap.nb_ports; ++i)
    {
        struct ifreq ifr;
        struct ff_port_cfg *cfg = &ff_global_cfg.netmap.port_cfgs[i];
            
        memset(&ifr, 0, sizeof(ifr));
        strcpy(ifr.ifr_name, cfg->name);
        if ((!(cfg->addr = get_port_addr(sd, SIOCGIFADDR, &ifr, "addr"))) ||
            (!(cfg->netmask = get_port_addr(sd, SIOCGIFNETMASK, &ifr, "netmask"))) ||
            (!(cfg->broadcast = get_port_addr(sd, SIOCGIFBRDADDR, &ifr, "broadcast")))) {
            return -1;
        }
        // Hardware address is returned not as an IP but as 6 bytes of data
        if (ioctl(sd, SIOCGIFHWADDR, &ifr) == -1)
        {
            fprintf(stderr, "Error getting hardware address: %s\n", 
                    strerror(errno));
            close(sd);
            return -1;
        }
        memcpy(cfg->mac, &ifr.ifr_hwaddr.sa_data, 6);
        printf("Port: %s, addr: %s, netmask: %s, broadcast: %s, mac: "
               "%02x:%02x:%02x:%02x:%02x:%02x\n", cfg->name,
               cfg->addr, cfg->netmask, cfg->broadcast, 
               (unsigned char)cfg->mac[0], (unsigned char)cfg->mac[1],
               (unsigned char)cfg->mac[2], (unsigned char)cfg->mac[3],
               (unsigned char)cfg->mac[4], (unsigned char)cfg->mac[5]);
        if (ff_veth_attach(cfg) == NULL)
        {
            close(sd);
            return -1;
        }
    }
    close(sd);

    return 0;
}


// EXPORTED AS ff_run!!!
/*
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
*/


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


// Netmap only functions exported
int ff_num_events(void)
{
    return ff_global_cfg.netmap.nb_ports;
}


int ff_get_event(int index, int *fd, short *mask, event_func_t *evt_fn, 
                 void **arg)
{
    if (!s_context)
        return -1;
    *fd = s_context[index].fd;
    *mask = POLLIN;// | POLLOUT | POLLERR;
    *evt_fn = netmap_read_process;
    (*arg) = (void *)&s_context[index];
    return 0;
}


static int fire_timer()
{
    uint64_t exp;
    read(s_netmap_if_timer, &exp, sizeof(exp));
#ifdef FF_WANTDEBUG    
    if (ff_debug)
        ff_debug("Fire timer, read %ld\n", exp);
#endif
    ff_hardclock();
    ff_update_current_ts();
    return 0;
}


int ff_get_timer_fire(int *fd, short *mask, timer_fire_func_t *fire_fn)
{
    if ((!s_context) || (s_netmap_if_timer == -1))
        return -1;
    *fd = s_netmap_if_timer;
    *mask = POLLIN;
    *fire_fn = fire_timer;
#ifdef FF_WANTDEBUG    
    if (ff_debug)
        ff_debug("Setting timer with fd: %d\n", *fd);
#endif    
    return 0;
}
