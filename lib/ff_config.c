/*
 * Copyright (C) 2017 THL A29 Limited, a Tencent company.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *   list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <getopt.h>
#include <ctype.h>
#ifndef FF_NETMAP
#include <rte_config.h>
#include <rte_string_fns.h>
#else
#include <errno.h>
#endif
#include "ff_config.h"
#include "ff_ini_parser.h"

#define DEFAULT_CONFIG_FILE   "config.ini"

#define BITS_PER_HEX 4

struct ff_config ff_global_cfg;
int dpdk_argc;
char *dpdk_argv[DPDK_CONFIG_NUM + 1];

char* const short_options = "c:t:p:";
struct option long_options[] = {
    { "conf", 1, NULL, 'c'},
#ifndef FF_NETMAP    
    { "proc-type", 1, NULL, 't'},
    { "proc-id", 1, NULL, 'p'},
#endif    
    { 0, 0, 0, 0},
};

#ifndef FF_NETMAP
static int
xdigit2val(unsigned char c)
{
    int val;

    if (isdigit(c))
        val = c - '0';
    else if (isupper(c))
        val = c - 'A' + 10;
    else
        val = c - 'a' + 10;
    return val;
}

static int
parse_lcore_mask(struct ff_config *cfg, const char *coremask)
{
    int i, j, idx = 0;
    unsigned count = 0;
    char c;
    int val;
    uint16_t *proc_lcore;
    char buf[RTE_MAX_LCORE] = {0};

    if (coremask == NULL)
        return 0;

    cfg->dpdk.proc_lcore = (uint16_t *)calloc(RTE_MAX_LCORE, sizeof(uint16_t));
    if (cfg->dpdk.proc_lcore == NULL) {
        fprintf(stderr, "parse_lcore_mask malloc failed\n");
        return 0;
    }
    proc_lcore = cfg->dpdk.proc_lcore;

    /*
     * Remove all blank characters ahead and after.
     * Remove 0x/0X if exists.
     */
    while (isblank(*coremask))
        coremask++;
    if (coremask[0] == '0' && ((coremask[1] == 'x')
        || (coremask[1] == 'X')))
        coremask += 2;

    i = strlen(coremask);
    while ((i > 0) && isblank(coremask[i - 1]))
        i--;

    if (i == 0)
        return 0;

    for (i = i - 1; i >= 0 && idx < RTE_MAX_LCORE; i--) {
        c = coremask[i];
        if (isxdigit(c) == 0) {
            return 0;
        }
        val = xdigit2val(c);
        for (j = 0; j < BITS_PER_HEX && idx < RTE_MAX_LCORE; j++, idx++) {
            if ((1 << j) & val) {
                proc_lcore[count] = idx;
                if (cfg->dpdk.proc_id == count) {
                    sprintf(buf, "%x", 1<<idx);
                    cfg->dpdk.proc_mask = strdup(buf);
                }
                count++;
            }
        }
    }

    for (; i >= 0; i--)
        if (coremask[i] != '0')
            return 0;

    if (cfg->dpdk.proc_id >= count)
        return 0;

    cfg->dpdk.nb_procs = count;

    return 1;
}
#endif

static int
is_integer(const char *s)
{
    if (*s == '-' || *s == '+')
        s++;
    if (*s < '0' || '9' < *s)
        return 0;
    s++;
    while ('0' <= *s && *s <= '9')
        s++;
    return (*s == '\0');
}

static int
freebsd_conf_handler(struct ff_config *cfg, const char *section,
    const char *name, const char *value)
{
    struct ff_freebsd_cfg *newconf, **cur;
    newconf = (struct ff_freebsd_cfg *)malloc(sizeof(struct ff_freebsd_cfg));
    if (newconf == NULL) {
        fprintf(stderr, "freebsd conf malloc failed\n");
        return 0;
    }

    newconf->name = strdup(name);
    newconf->str = strdup(value);

    if (strcmp(section, "boot") == 0) {
        cur = &cfg->freebsd.boot;

        newconf->value = (void *)newconf->str;
        newconf->vlen = strlen(value);
    } else if (strcmp(section, "sysctl") == 0) {
        cur = &cfg->freebsd.sysctl;

        if (is_integer(value)) {
            if (strcmp(name, "kern.ipc.maxsockbuf") == 0) {
                long *p = (long *)malloc(sizeof(long));
                *p = atol(value);
                newconf->value = (void *)p;
                newconf->vlen = sizeof(*p);
            } else {
                 int *p = (int *)malloc(sizeof(int));
                 *p = atoi(value);
                 newconf->value = (void *)p;
                 newconf->vlen = sizeof(*p);
            }
        } else {
            newconf->value = (void *)newconf->str;
            newconf->vlen = strlen(value);
        }
    } else {
        fprintf(stderr, "freebsd conf section[%s] error\n", section);
        return 0;
    }

    if (*cur == NULL) {
        newconf->next = NULL;
        *cur = newconf;
    } else {
        newconf->next = (*cur)->next;
        (*cur)->next = newconf;
    }

    return 1;
}

#ifndef FF_NETMAP
// A recursive binary search function. It returns location of x in
// given array arr[l..r] is present, otherwise -1
static int
uint16_binary_search(uint16_t arr[], int l, int r, uint16_t x)
{
    if (r >= l) {
        int mid = l + (r - l)/2;

        // If the element is present at the middle itself
        if (arr[mid] == x)  return mid;

        // If element is smaller than mid, then it can only be present
        // in left subarray
        if (arr[mid] > x) return uint16_binary_search(arr, l, mid-1, x);

        // Else the element can only be present in right subarray
        return uint16_binary_search(arr, mid+1, r, x);
    }

    // We reach here when element is not present in array
    return -1;
}
#endif


static int
uint16_cmp (const void * a, const void * b)
{
    return ( *(uint16_t*)a - *(uint16_t*)b );
}

static inline void
sort_uint16_array(uint16_t arr[], int n)
{
    qsort(arr, n, sizeof(uint16_t), uint16_cmp);
}

static inline char *
__strstrip(char *s)
{
    char *end = s + strlen(s) - 1;
    while(*s == ' ') s++;
    for (; end >= s; --end) {
        if (*end != ' ') break;
    }
    *(++end) = '\0';
    return s;
}


#ifdef FF_NETMAP
/* split string into tokens */
static int
rte_strsplit(char *string, int stringlen,
             char **tokens, int maxtokens, char delim)
{
    int i, tok = 0;
    int tokstart = 1; /* first token is right at start of string */
   
    if (string == NULL || tokens == NULL)
        goto einval_error;

    for (i = 0; i < stringlen; i++) {
        if (string[i] == '\0' || tok >= maxtokens)
            break;
        if (tokstart) {
            tokstart = 0;
            tokens[tok++] = &string[i];
        }
        if (string[i] == delim) {
            string[i] = '\0';
            tokstart = 1;
        }
    }
    return tok;

einval_error:
    errno = EINVAL;
    return -1;
}
#endif


static int
__parse_config_list(uint16_t *arr, int *sz, const char *value) {
    int i, j;
    char input[4096];
    char *tokens[128];
    int nTokens = 0;
    char *endptr;
    int nr_ele = 0;
    int max_ele = *sz;

    strncpy(input, value, 4096);
    nTokens = rte_strsplit(input, sizeof(input), tokens, 128, ',');
    for (i = 0; i < nTokens; i++) {
        char *tok = tokens[i];
        char *middle = strchr(tok, '-');
        if (middle == NULL) {
            tok = __strstrip(tok);
            long v = strtol(tok, &endptr, 10);
            if (*endptr != '\0') {
                fprintf(stderr, "%s is not a integer.", tok);
                return 0;
            }
            if (nr_ele > max_ele) {
                fprintf(stderr, "too many elements in list %s\n", value);
                return 0;
            }
            arr[nr_ele++] = (uint16_t)v;
        } else {
            *middle = '\0';
            char *lbound = __strstrip(tok);
            char *rbound = __strstrip(middle+1);
            long lv = strtol(lbound, &endptr, 10);
            if (*endptr != '\0') {
                fprintf(stderr, "%s is not a integer.", lbound);
                return 0;
            }
            long rv = strtol(rbound, &endptr, 10);
            if (*endptr != '\0') {
                fprintf(stderr, "%s is not a integer.", rbound);
                return 0;
            }
            for (j = lv; j <= rv; ++j) {
                if (nr_ele > max_ele) {
                    fprintf(stderr, "too many elements in list %s.\n", value);
                    return 0;
                }
                arr[nr_ele++] = (uint16_t)j;
            }
        }
    }
    if (nr_ele <= 0) {
        fprintf(stderr, "list %s is empty\n", value);
        return 1;
    }
    sort_uint16_array(arr, nr_ele);
    *sz = nr_ele;
    return 1;
}


#ifndef FF_NETMAP
static int
parse_port_lcore_list(struct ff_port_cfg *cfg, const char *v_str)
{
    cfg->nb_lcores = DPDK_MAX_LCORE;
    uint16_t *cores = cfg->lcore_list;
    return __parse_config_list(cores, &cfg->nb_lcores, v_str);
}
#endif


static int
parse_port_list(struct ff_config *cfg, const char *v_str)
{
    int res;
    uint16_t ports[RTE_MAX_ETHPORTS];
    int sz = RTE_MAX_ETHPORTS;

    res = __parse_config_list(ports, &sz, v_str);
    if (! res) return res;

#ifndef FF_NETMAP
    uint16_t *portid_list = malloc(sizeof(uint16_t)*sz);

    if (portid_list == NULL) {
        fprintf(stderr, "parse_port_list malloc failed\n");
        return 0;
    }
    memcpy(portid_list, ports, sz*sizeof(uint16_t));
#endif
#ifdef FF_NETMAP
    cfg->netmap.nb_ports = sz;
#else
    cfg->dpdk.portid_list = portid_list;
    cfg->dpdk.nb_ports = sz;
    cfg->dpdk.max_portid = portid_list[sz-1];
#endif
    return res;
}


static int
port_cfg_handler(struct ff_config *cfg, const char *section,
    const char *name, const char *value) {

#ifdef FF_NETMAP
    if (cfg->netmap.nb_ports == 0) 
#else        
    if (cfg->dpdk.nb_ports == 0) 
#endif
    {        
        fprintf(stderr, "port_cfg_handler: must config dpdk.port_list first\n");
        return 0;
    }

#ifdef FF_NETMAP
    if (cfg->netmap.port_cfgs == NULL)
#else        
    if (cfg->dpdk.port_cfgs == NULL) 
#endif        
    {
        struct ff_port_cfg *pc = calloc(RTE_MAX_ETHPORTS, 
                                        sizeof(struct ff_port_cfg));
        if (pc == NULL) {
            fprintf(stderr, "port_cfg_handler malloc failed\n");
            return 0;
        }
        // initialize lcore list and nb_lcores
        int i;
#ifdef FF_NETMAP
        for (i = 0; i < cfg->netmap.nb_ports; ++i)
#else        
        for (i = 0; i < cfg->dpdk.nb_ports; ++i) 
#endif            
        {
            uint16_t portid = i;
#ifndef FF_NETMAP
            portid = cfg->dpdk.portid_list[i];
#endif            
            struct ff_port_cfg *pconf = &pc[portid];
            pconf->port_id = portid;
#ifndef FF_NETMAP            
            pconf->nb_lcores = ff_global_cfg.dpdk.nb_procs;
            memcpy(pconf->lcore_list, ff_global_cfg.dpdk.proc_lcore,
                   pconf->nb_lcores*sizeof(uint16_t));
#endif            
        }
#ifdef FF_NETMAP
        cfg->netmap.port_cfgs = pc;
#else        
        cfg->dpdk.port_cfgs = pc;
#endif
    }

    int portid;
    int ret = sscanf(section, "port%d", &portid);
    if (ret != 1) {
        fprintf(stderr, "port_cfg_handler section[%s] error\n", section);
        return 0;
    }

#ifndef FF_NETMAP
    /* just return true if portid >= nb_ports because it has no effect */
    if (portid > cfg->dpdk.max_portid) {
        fprintf(stderr, "port_cfg_handler section[%s] bigger than max port id\n", section);
        return 1;
    }
#endif

    struct ff_port_cfg *cur;
#ifdef FF_NETMAP
    cur = &cfg->netmap.port_cfgs[portid];
#else    
    cur = &cfg->dpdk.port_cfgs[portid];
#endif    
    if (cur->name == NULL) {
#ifdef FF_NETMAP
        char name[256];
        snprintf(name, sizeof(name), "eth%d", portid);
        cur->name = strdup(name);
#else
        cur->name = strdup(section);
#endif
        cur->port_id = portid;
    }
#ifndef FF_NETMAP
    if (strcmp(name, "addr") == 0) {
        cur->addr = strdup(value);
    } else if (strcmp(name, "netmask") == 0) {
        cur->netmask = strdup(value);
    } else if (strcmp(name, "broadcast") == 0) {
        cur->broadcast = strdup(value);
    } else if (strcmp(name, "gateway") == 0) {
        cur->gateway = strdup(value);
    } else if (strcmp(name, "pcap") == 0) {
        cur->pcap = strdup(value);
    } else if (strcmp(name, "lcore_list") == 0) {
        return parse_port_lcore_list(cur, value);
    }
#endif
    return 1;
}

static int
ini_parse_handler(void* user, const char* section, const char* name,
    const char* value)
{
    struct ff_config *pconfig = (struct ff_config*)user;

    printf("[%s]: %s=%s\n", section, name, value);

    #define MATCH(s, n) strcmp(section, s) == 0 && strcmp(name, n) == 0
#ifdef FF_NETMAP
    if (MATCH("dpdk", "port_list")) {
        return parse_port_list(pconfig, value);
    } else
#else
    if MATCH("dpdk", "channel")) {
        pconfig->dpdk.nb_channel = atoi(value);
    } else if (MATCH("dpdk", "memory")) {
        pconfig->dpdk.memory = atoi(value);
    } else if (MATCH("dpdk", "no_huge")) {
        pconfig->dpdk.no_huge = atoi(value);
    } else if (MATCH("dpdk", "lcore_mask")) {
        pconfig->dpdk.lcore_mask = strdup(value);
        return parse_lcore_mask(pconfig, pconfig->dpdk.lcore_mask);
    } else if (MATCH("dpdk", "base_virtaddr")) {
        pconfig->dpdk.base_virtaddr= strdup(value);
    } else if (MATCH("dpdk", "port_list")) {
        return parse_port_list(pconfig, value);
    } else if (MATCH("dpdk", "promiscuous")) {
        pconfig->dpdk.promiscuous = atoi(value);
    } else if (MATCH("dpdk", "numa_on")) {
        pconfig->dpdk.numa_on = atoi(value);
    } else if (MATCH("dpdk", "tso")) {
        pconfig->dpdk.tso = atoi(value);
    } else if (MATCH("dpdk", "vlan_strip")) {
        pconfig->dpdk.vlan_strip = atoi(value);
    } else if (MATCH("dpdk", "idle_sleep")) {
        pconfig->dpdk.idle_sleep = atoi(value);
    } else 
#endif        
    if (MATCH("kni", "enable")) {
        pconfig->kni.enable= atoi(value);
    } else if (MATCH("kni", "method")) {
        pconfig->kni.method= strdup(value);
    } else if (MATCH("kni", "tcp_port")) {
        pconfig->kni.tcp_port = strdup(value);
    } else if (MATCH("kni", "udp_port")) {
        pconfig->kni.udp_port= strdup(value);
    } else if (strcmp(section, "freebsd.boot") == 0) {
        if (strcmp(name, "hz") == 0) {
            pconfig->freebsd.hz = atoi(value);
        } else if (strcmp(name, "physmem") == 0) {
            pconfig->freebsd.physmem = atol(value);
        } else if (strcmp(name, "fd_reserve") == 0) {
            pconfig->freebsd.fd_reserve = atoi(value);
        } else {
            return freebsd_conf_handler(pconfig, "boot", name, value);
        }
    } else if (strcmp(section, "freebsd.sysctl") == 0) {
        return freebsd_conf_handler(pconfig, "sysctl", name, value);
    } else if (strncmp(section, "port", 4) == 0) {
        return port_cfg_handler(pconfig, section, name, value);
    }

    return 1;
}

static int
dpdk_args_setup(struct ff_config *cfg)
{
    int n = 0;
    dpdk_argv[n++] = strdup("f-stack");
#ifndef FF_NETMAP    
    int i;
    char temp[DPDK_CONFIG_MAXLEN] = {0};

    if (cfg->dpdk.no_huge) {
        dpdk_argv[n++] = strdup("--no-huge");
    }
    if (cfg->dpdk.proc_mask) {
        sprintf(temp, "-c%s", cfg->dpdk.proc_mask);
        dpdk_argv[n++] = strdup(temp);
    }
    if (cfg->dpdk.nb_channel) {
        sprintf(temp, "-n%d", cfg->dpdk.nb_channel);
        dpdk_argv[n++] = strdup(temp);
    }
    if (cfg->dpdk.memory) {
        sprintf(temp, "-m%d", cfg->dpdk.memory);
        dpdk_argv[n++] = strdup(temp);
    }
    if (cfg->dpdk.proc_type) {
        sprintf(temp, "--proc-type=%s", cfg->dpdk.proc_type);
        dpdk_argv[n++] = strdup(temp);
    }
    if (cfg->dpdk.base_virtaddr) {
        sprintf(temp, "--base-virtaddr=%s", cfg->dpdk.base_virtaddr);
        dpdk_argv[n++] = strdup(temp);
    }
#endif
    dpdk_argc = n;

    return n;
}

static int
ff_parse_args(struct ff_config *cfg, int argc, char *const argv[])
{
    int c;
    int index = 0;
    optind = 1;
    while((c = getopt_long(argc, argv, short_options, long_options, &index)) != -1) {
        switch (c) {
            case 'c':
                cfg->filename = strdup(optarg);
                break;
#ifndef FF_NETMAP                
            case 'p':
                cfg->dpdk.proc_id = atoi(optarg);
                break;
            case 't':
                cfg->dpdk.proc_type = strdup(optarg);
                break;
#endif
            default:
                return -1;
        }
    }

#ifndef FF_NETMAP
    if (cfg->dpdk.proc_type == NULL) {
        cfg->dpdk.proc_type = strdup("auto");
    }

    if (strcmp(cfg->dpdk.proc_type, "primary") &&
        strcmp(cfg->dpdk.proc_type, "secondary") &&
        strcmp(cfg->dpdk.proc_type, "auto")) {
        printf("invalid proc-type:%s\n", cfg->dpdk.proc_type);
        return -1;
    }

    if ((uint16_t)cfg->dpdk.proc_id > RTE_MAX_LCORE) {
        printf("invalid proc_id:%d, use default 0\n", cfg->dpdk.proc_id);
        cfg->dpdk.proc_id = 0;
    }
#endif
    return 0;
}

static int
ff_check_config(struct ff_config *cfg)
{
    if(cfg->kni.enable && !cfg->kni.method) {
        fprintf(stderr, "conf dpdk.method is necessary\n");
        return -1;
    }

    if(cfg->kni.method) {
        if(strcasecmp(cfg->kni.method,"accept") &&
            strcasecmp(cfg->kni.method,"reject")) {
            fprintf(stderr, "conf kni.method[accept|reject] is error(%s)\n",
                cfg->kni.method);
            return -1;
        }
    }

    #define CHECK_VALID(n) \
        do { \
            if (!pc->n) { \
                fprintf(stderr, "port%d if config error: no %s\n", \
                    pc->port_id, #n); \
                return -1; \
            } \
        } while (0)

    int i;
#ifdef FF_NETMAP
    for (i = 0; i< cfg->netmap.nb_ports; i++)
#else    
    for (i = 0; i < cfg->dpdk.nb_ports; i++) 
#endif        
    {
        uint16_t portid = i;
#ifndef FF_NETMAP
        portid = cfg->netmap.portid_list[i];
        struct ff_port_cfg *pc = &cfg->dpdk.port_cfgs[portid];
        CHECK_VALID(addr);
        CHECK_VALID(netmask);
        CHECK_VALID(broadcast);
        CHECK_VALID(gateway);
        // check if the lcores in lcore_list are enabled.
        int k;
        for (k = 0; k < pc->nb_lcores; k++) {
            uint16_t lcore_id = pc->lcore_list[k];
            if (uint16_binary_search(cfg->dpdk.proc_lcore, 0,
                                     cfg->dpdk.nb_procs-1, lcore_id) < 0) {
                fprintf(stderr, "lcore %d is not enabled.\n", lcore_id);
                return -1;
            }
        }
        /*
         * only primary process process KNI, so if KNI enabled,
         * primary lcore must stay in every enabled ports' lcore_list
         */
        if (cfg->kni.enable &&
            strcmp(cfg->dpdk.proc_type, "primary") == 0) {
            int found = 0;
            int j;
            uint16_t lcore_id = cfg->dpdk.proc_lcore[cfg->dpdk.proc_id];
            for (j = 0; j < pc->nb_lcores; j++) {
                if (pc->lcore_list[j] == lcore_id) {
                    found = 1;
                }
            }
            if (! found) {
                fprintf(stderr,
                         "primary lcore %d should stay in port %d's lcore_list.\n",
                         lcore_id, pc->port_id);
                return -1;
            }
        }
#else
        struct ff_port_cfg *pc = &cfg->netmap.port_cfgs[portid];
        if ((!pc->name) || (pc->name[0] == 0))
            return -1;
#endif        
    }

    return 0;
}

static void
ff_default_config(struct ff_config *cfg)
{
    memset(cfg, 0, sizeof(struct ff_config));

    cfg->filename = DEFAULT_CONFIG_FILE;

#ifdef FF_NETMAP
    cfg->netmap.nb_ports = 0;
#else
    cfg->dpdk.proc_id = -1;
    cfg->dpdk.numa_on = 1;
    cfg->dpdk.promiscuous = 1;
#endif

    cfg->freebsd.hz = 100;
    cfg->freebsd.physmem = 1048576*256;
    cfg->freebsd.fd_reserve = 0;
}

int
ff_load_config(int argc, char *const argv[])
{
    ff_default_config(&ff_global_cfg);

    int ret = ff_parse_args(&ff_global_cfg, argc, argv);
    if (ret < 0) {
        return ret;
    }

    ret = ini_parse(ff_global_cfg.filename, ini_parse_handler,
                    &ff_global_cfg);
    if (ret != 0) {
        printf("parse %s failed on line %d\n", ff_global_cfg.filename, ret);
        return -1;
    }

    if (ff_check_config(&ff_global_cfg)) {
        return -1;
    }

    if (dpdk_args_setup(&ff_global_cfg) <= 0) {
        return -1;
    }

    return 0;
}
