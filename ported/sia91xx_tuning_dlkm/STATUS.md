# sia91xx_tuning_dlkm - EQUIVALENT (OS4 stock, no hacks)

Reference: OS4 `<work-dir>` (modinfo `name=sia91xx_tuning_dlkm`; 17.10.2026).
This directory is the HyperOS 3 reconstruction `work/re/src/sia91xx_tuning/` (see its STATUS.md: SI-IN `sipa_tuning_if.c` + `sipa_tuning_misc.c` merged; lock-key section trick, function sections) with the file/module renamed
to the OS4 name (`out = sia91xx_tuning_dlkm.ko`). The OS4 binary is identical to the HyperOS 3 one; no source change was necessary.
`compare_ko.py` (real Kleaf build on 6.6.144): function set (10), bytes, relocations, globals, imports + CRCs (23), modinfo (incl. `name=`) all OK => EQUIVALENT.
Build: `bash work/re/src/g3_tools/g3_build.sh sia91xx_tuning_dlkm`.
