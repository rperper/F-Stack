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

#include <stdlib.h>
#include <stdio.h>

#include "ff_api.h"
#include "ff_config.h"
#ifdef FF_NETMAP
#include "ff_netmap_if.h"
#else
#include "ff_dpdk_if.h"
#endif

extern int ff_freebsd_init();

int
ff_init(int argc, char * const argv[])
{
    int ret;
    ret = ff_load_config(argc, argv);
    if (ret < 0)
        exit(1);

#ifdef FF_NETMAP    
    ret = ff_netmap_init(dpdk_argc, (char **)&dpdk_argv);
#else    
    ret = ff_dpdk_init(dpdk_argc, (char **)&dpdk_argv);
#endif
    printf("ff_user_init returned %d\n", ret);    
    if (ret < 0)
        exit(1);

    printf("Call ff_freebsd_init\n");
    ret = ff_freebsd_init();
    printf("Return from ff_freebsd_init, ret: %d\n", ret);
    if (ret < 0)
        exit(1);

#ifdef FF_NETMAP    
    ret = ff_netmap_if_up();
#else    
    ret = ff_dpdk_if_up();
#endif    
    if (ret < 0)
        exit(1);

    return 0;
}

void
ff_run(loop_func_t loop, void *arg)
{
#ifdef FF_NETMAP    
    ff_netmap_run(loop, arg);
#else    
    ff_dpdk_run(loop, arg);
#endif    
}

