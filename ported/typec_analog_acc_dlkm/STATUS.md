# typec_analog_acc_dlkm - EQUIVALENT (OS4 stock, no hacks)

Reference: OS4 `<work-dir>` (modinfo `name=typec_analog_acc_dlkm`, depends=ucsi_qti_glink; 17.10.2026).
This directory is the HyperOS 3 reconstruction `work/re/src/typec_analog_acc/` (see its STATUS.md: acc_priv layout, notifier order, stub `re/stubs_misc:ucsi_stub` for `register_ucsi_glink_notifier`) with the file/module renamed
to the OS4 name (`out = typec_analog_acc_dlkm.ko`, ThinLTO `typec_analog_acc_dlkm_defconfig`, `CONFIG_UCSI_QTI_GLINK=1`). The OS4 binary is identical to the HyperOS 3 one; no source change was necessary.
`compare_ko.py` (real Kleaf build on 6.6.144): 4 functions byte-identical, relocations, globals, exports (1) + CRC, imports (8) + CRCs, modinfo all OK => EQUIVALENT.
Build: `bash work/re/src/g3_tools/g3_build.sh typec_analog_acc_dlkm`.
