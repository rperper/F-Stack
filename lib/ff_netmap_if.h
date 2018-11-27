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

#ifndef _FSTACK_NETMAP_IF_H
#define _FSTACK_NETMAP_IF_H

#include "ff_api.h"

#define ff_IF_NAME "f-stack-%d"

struct loop_routine {
    loop_func_t loop;
    void *arg;
};

int ff_netmap_init(int argc, char **argv);
int ff_netmap_if_up(void);
void ff_netmap_run(loop_func_t loop, void *arg);

struct ff_netmap_if_context;
struct ff_port_cfg;

struct ff_netmap_if_context *ff_netmap_register_if(void *sc, void *ifp,
    struct ff_port_cfg *cfg);
void ff_netmap_deregister_if(struct ff_netmap_if_context *ctx);

void ff_netmap_set_if(struct ff_netmap_if_context *ctx, void *sc, void *ifp);

int ff_netmap_if_send(struct ff_netmap_if_context* ctx, void *buf, int total);

void ff_netmap_pktmbuf_free(void *m);


#endif /* ifndef _FSTACK_DPDK_IF_H */

