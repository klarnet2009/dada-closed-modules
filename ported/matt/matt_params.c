// SPDX-License-Identifier: GPL-2.0
/* matt tunables (sysctl / bbr options): initial values from the OS4 stock .data/.bss. DO NOT LOAD before review. */
#include "matt.h"

int sysctl_matt_debug = 0x1e;
int sysctl_matt_enable;
int sysctl_inet_enable;
int sysctl_mem_limited;
int sysctl_limit_output = 0xffff0;
int sysctl_defer_max = 0x40;
int bbr_enable_opt = 1;
int bbr_enable_debug;
int bbr_srtt_factor = 5;
int bbr_mrtt_factor = 10;
int bbr_pace_factor;
int bbr_min_rtt_win_factor = 10;
int bbr_probe_rtt_cwnd_factor = 10;
int bbr_no_pacing = 1;
int bbr_init_cwnd_opt = 1;
int bbr_avoid_burst = 0x7d;
int bbr_cwnd_limit_factor = 0xf;
int bbr_cap_rtt_factor = 0xf;
