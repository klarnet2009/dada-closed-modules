/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM miload

#if !defined(_MILOAD_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _MILOAD_TRACE_H

#include <linux/types.h>
#include <linux/tracepoint.h>

TRACE_EVENT(cluster_real_scale_delta,

	TP_PROTO(unsigned int id, unsigned int cid, unsigned int pid, unsigned int nr,
		 u64 prev, u64 curr),

	TP_ARGS(id, cid, pid, nr, prev, curr),

	TP_STRUCT__entry(
		__field(unsigned int, id)
		__field(unsigned int, cid)
		__field(unsigned int, pid)
		__field(unsigned int, nr)
		__field(u64, prev)
		__field(u64, curr)
	),

	TP_fast_assign(
		__entry->id = id;
		__entry->cid = cid;
		__entry->pid = pid;
		__entry->nr = nr;
		__entry->prev = prev;
		__entry->curr = curr;
	),

	TP_printk("grp->id=%u cid=%u p->pid-%d nr=%d prev=%llu curr=%llu",
		  __entry->id, __entry->cid, __entry->pid, __entry->nr,
		  __entry->prev, __entry->curr)
);

TRACE_EVENT(cluster_util_debug,

	TP_PROTO(unsigned int cid, unsigned int id, unsigned int util, unsigned int avg_freq,
		 bool cluster_use_cur_flag),

	TP_ARGS(cid, id, util, avg_freq, cluster_use_cur_flag),

	TP_STRUCT__entry(
		__field(unsigned int, cid)
		__field(unsigned int, id)
		__field(unsigned int, util)
		__field(unsigned int, avg_freq)
		__field(bool, cluster_use_cur_flag)
	),

	TP_fast_assign(
		__entry->cid = cid;
		__entry->id = id;
		__entry->util = util;
		__entry->avg_freq = avg_freq;
		__entry->cluster_use_cur_flag = cluster_use_cur_flag;
	),

	TP_printk("cid=%u id=%u util=%u avg_freq=%u cluster_use_cur_flag=%d",
		  __entry->cid, __entry->id, __entry->util, __entry->avg_freq,
		  __entry->cluster_use_cur_flag)
);

#endif /* _MILOAD_TRACE_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE miload_trace

/* This part must be outside protection */
#include <trace/define_trace.h>
