# millet_oem_cgroup - EQUIVALENT vs OS4 (g5)

Stock reference: HyperOS 4.0 `millet_oem_cgroup.ko` (md5 c5dda9da..., <work-dir>), checked 17.10.2026 by with the real Kleaf build (`//re/millet_oem_cgroup`) + compare_ko.py (EQUIVALENT).

unchanged since HyperOS 3: no source change; real Kleaf build + compare_ko.py vs the OS4 stock: function set (4), bytes, relocations, global objects, exports (0), imports+CRC (7), modinfo all OK.

---

## Earlier notes (HyperOS 3 round, kept for the history)

# millet_oem_cgroup
STATUS: EQUIVALENT.
Key facts: oem_cgroup_hook is a zeroed struct cgroup_subsys (248 B); freezer_cgrp_subsys.can_attach/cancel_attach (+0x48/+0x50) are replaced by wrappers that call it; millet_can_attach allows task==current, euid 1000 or CAP_SYS_ADMIN, else logs "Permission problem" and returns 1.
Needs local_defines CONFIG_LTO=1 (stock READ_ONCE is ldar/ldapr).
