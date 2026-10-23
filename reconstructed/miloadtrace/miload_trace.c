// SPDX-License-Identifier: GPL-2.0
/* tracepoint definitions live in their own translation unit (stock: __traceiter_* are not inlined into callers) */
#include <linux/module.h>
#define CREATE_TRACE_POINTS
#include "miload_trace.h"
