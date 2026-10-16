# scene_swappiness - EQUIVALENT

`compare_ko.py`: function set (6), function bytes, relocations, global objects, imports + CRCs, modinfo all identical; no exports.

What it is: Xiaomi proc interface `/proc/mi_mem_swappiness/swappiness_para` (read/write `s_anon_swappiness`, `h_anon_swappiness`,
`free_swap_threshold`) plus an `android_vh_tune_swappiness` probe (swappiness 0 when free swap is below the threshold, otherwise
`h_anon_swappiness` for kswapd / `s_anon_swappiness` for everybody else). Module params `kswapd_vm_swappiness` / `vm_swappiness`
alias `h_anon_swappiness` / `s_anon_swappiness`.

Build: `local_defines = CONFIG_LTO=1`, `copts = -ffunction-sections -fdata-sections` (see BUILD.bazel).

Recovered details that mattered for byte identity
- Source of the numbers: initial values 160 / 60 / 25600 (`.data`), `free_swap_threshold` is `unsigned int` (zero-extended compare with
  `atomic_long_read(&nr_swap_pages)`), `val * 5 * 512` is stored for `free_swap_threshold=<MB-ish 0..200>`.
- `kstrtoul()` (inline wrapper around the imported `kstrtoull`), `unsigned long val`.
- `read`: `&buffer[len]` / `128 - len` with `int len` (frontend array-bounds trap `brk #0x5512` + 32-bit arithmetic); tail written with
  `int n = len - pos; if (pos >= len) n = 0; if (count > n) count = n;` (no `simple_read_from_buffer`, the stock has none).
- `write`: the three limit checks are `if (val > 200) { pr_err } else { assign; return count; }` (the `else` shape is what makes clang keep
  `cmp #201; b.hs`); the final `return -EINVAL`.
- Only non-obvious part: stock has NO local-bounds trap check (`cmp x19,#0x7f; b.hi brk#1`) for `buffer[count] = 0`. Plain `buffer[count] = 0`
  always produced that check with this toolchain (BoundsChecking pass runs late, the dominating `count >= 128` early exit is not in a
  single-predecessor chain because of the inlined `copy_from_user`). Reproduced by `WRITE_ONCE(buffer[count], 0)` (volatile stores are not
  instrumented); `__attribute__((no_sanitize("bounds")))` on the function gives the same bytes. The real original is surely a plain store;
  this is a codegen workaround, noted in the source.
- Success message `Register swappiness interface success.\n` has no `pr_fmt` prefix and no log level (`printk("...")`); the others use
  `pr_fmt("scene_swappiness: ")`. `create_swappiness_proc` is `__init` and is not inlined.
