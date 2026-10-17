# bootinfo - EQUIVALENT vs OS4

G6 re-check against the HyperOS 4.0 stock (17.10.2026): OS3 and OS4 stock binaries compared function by function (g6/os3_vs_os4.csv): all 8 function bodies, relocations, kCFI words, data/rodata sections, exports+CRC, imports+CRC, strings and modinfo are identical (only vermagic/scmversion differ), so the existing source stays valid for OS4. Real Kleaf build of the source compared with the OS4 stock `vendor_ramdisk/bootinfo.ko`: compare_ko.py EQUIVALENT (8 functions, 0 exports, 13 imports).

Earlier headline (HyperOS 3 round): bootinfo - EQUIVALENT


Template: NOT the XRING `vendor_stability/bootinfo` (that one is the XRING power-down-reason variant; the dada stock is the Qualcomm PMIC variant).
Used: MiCode `breeze-u-oss` `drivers/misc/bootinfo.c` (module form: `module_param_string pureason/pdreason`, `kstrtou32`, `powerup_reasons/powerdown_reasons/reset_reasons`; found by scanning all
trees of `<build-dir>`: `git ls-tree -r --name-only refs/remotes/o/<branch>` is instant, blobs are fetched lazily by `git show`). Copy: `work/re/ref/portD/bootinfo/`.
The repo copy has bogus `<<<<<<< PATCH SET / >>>>>>> BASE` conflict markers around it (removed), and no header: `bootinfo.h` was rebuilt from sunstone-u-oss `asm/bootinfo.h` + the stock arrays.
compare_ko: 8 functions identical, relocations, objects, 0 exports, 13 imports + CRCs, modinfo: all OK -> EQUIVALENT (kernel's FORTIFY strlen/strnlen/fortify_panic paths reproduce 1:1).

Differences of the stock vs the breeze file (the "powerup reason is writable" patch HQ-351356 that the branch had reverted):
- new `static char current_pu_reason_str[64]`; `powerup_reason_show()` only computes the reason if it is empty, stores it with `snprintf(cur, strlen(r)+2, "%s", r)` (or `strcpy(cur, "unknown reboot")`)
  and returns `snprintf(buf, strlen(cur)+2, "%s\n", cur)`; new `powerup_reason_store()` = `sscanf(buf, "%s", cur)`, `return count` (only `powerup_reason` has a store; all attrs mode 0644).
- `u32 reset_reason` (find_first_bit size 32), the `pr_err/pr_debug` calls of `powerup_reason_show/poweroff_reason_show/powerup_reason_details_show` are gone, `poweroff_reason_show` unknown case is `snprintf(s, 16, "unknown reboot\n")`.
