# xm_power - EQUIVALENT

`compare_ko.py`: 15 functions (bytes+relocs), objects, 0 exports, 37 import CRCs, modinfo: all OK; `portC_data.py`: 15 data objects identical, kCFI identical; rodata strings identical.

Template: MiCode branch `goku-u-oss-test` `drivers/power/xm_power/xm_power.{c,h}` (pr_fmt "PowerDet:"), plus from branch `pearl-s-oss` (same dir) the `cputime_adjust()` / `task_cputime_adjusted()` pair (non-`CONFIG_VIRT_CPU_ACCOUNTING_NATIVE` variant, uses `mul_u64_u64_div_u64_bak`),
which the stock module defines as global functions (the goku header only declares `task_cputime_adjusted` as extern). Everything else of pearl (screen notifier `CONFIG_MI_DISP_NOTIFIER`, `pending suspend count`, CpuLoad line, `load < 3` check, pr_fmt with pid/func/line) is NOT in the stock.
Changes against the template: 
- the "Cpu usage" log line has no `cpu_mask` (stock string: `Cpu usage:%d%% user + %d%% kernel, comm:%s, pid:%d, pcomm:%s, ppid:%d`), `struct top_cpu_info` keeps `cpus_mask` (copied, never printed);
- API drift: `do_each_thread()` no longer exists in 6.6 headers (`while_each_thread()` does). A local compat `#define do_each_thread(g,t) for (g = t = &init_task; (g = t = next_task(g)) != &init_task; ) do` reproduces the stock exactly (task->tasks walk + `thread_group` walk at 0x6c8);
  using `for_each_process_thread()` (signal->thread_head/thread_node) gives different code (6 instructions more).
Build: ThinLTO defconfig, `CONFIG_LTO=1`, function/data sections; no stubs needed (`depends=` empty).
