# millet_oem_cgroup
STATUS: EQUIVALENT.
Key facts: oem_cgroup_hook is a zeroed struct cgroup_subsys (248 B); freezer_cgrp_subsys.can_attach/cancel_attach (+0x48/+0x50) are replaced by wrappers that call it; millet_can_attach allows task==current, euid 1000 or CAP_SYS_ADMIN, else logs "Permission problem" and returns 1.
Needs local_defines CONFIG_LTO=1 (stock READ_ONCE is ldar/ldapr).
