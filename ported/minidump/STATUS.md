# minidump - function-level EQUIVALENT (61 functions byte-identical, relocations, objects, 6 exports + CRCs, modinfo all OK); 1 import CRC differs (stub artifact)
compare_ko.py: only `imported symbols + CRCs` differs: `qcom_is_dma_buf_file` stock 0x1c02c348 vs ours 0xa7b7c305. The provider is our stub (//re/stubs_portA:minidump_deps_stub); genksyms expands the
types visible in the provider's TU (stock qcom_dma_heaps.ko compiled with many more headers), so the stub's CRC cannot be made equal without the real provider source. Code is unaffected.
Template: micode_bixi `drivers/soc/qcom/{msm_minidump.c,minidump_log.c,minidump_memory.c,minidump_memory.h,minidump_private.h,elf.h,debug_symbol.h}` (Qualcomm minidump).
Changes against the template:
- msm_minidump.c md_get_region(): `if (hdr_name && !strcmp(hdr_name, name))` (stock checks the string for NULL; the template does not).
- Kernel-private headers (`kernel/sched/sched.h`, `kernel/time/tick-internal.h`, `mm/slab.h`, `mm/internal.h` + their local includes) copied from <build-dir> into the package with the same relative layout
  (sources in drivers/soc/qcom/, so `"../../../kernel/sched/sched.h"` resolves); `../mm/internal.h` -> `../../../mm/internal.h`.
- minidump_log.c: `<linux/sched/walt.h>` replaced by local copy `walt_local.h` (6.6.144 header has plain `inline` walt_partial_*_cpus stubs when !SCHED_WALT -> emitted as functions; with SCHED_WALT=1 not needed but kept).
Configs (all as local_defines, found by matching function sets/bytes): CONFIG_QCOM_MINIDUMP, _PANIC_DUMP, _PANIC_MEMORY_INFO (minidump_memory.c), _PANIC_CPU_CONTEXT, _PANIC_KTASK_STACK (dump_trace), _PANIC_CPUFREQ_INFO (log_cpu_freq),
_FTRACE, _PSTORE (ramoops regions in msm_minidump_log_init), NOT _DYN_MINIDUMP_STACK; CONFIG_SCHED_WALT=1 (WALT fields "Last_enqueued_ts"...), CONFIG_MINIDUMP_MAX_ENTRIES=200, CONFIG_GH_RM_DRV=1, CONFIG_QCOM_DEBUG_SYMBOL=1, CONFIG_QCOM_SMEM=1.
Stub providers: gh_rm_minidump_*, debug_symbol_*, qcom_smem_get, qcom_is_dma_buf_file.
