# typec_analog_acc - HyperOS 3 name; in OS4 the module is `typec_analog_acc_dlkm.ko`: EQUIVALENT vs OS4 is recorded in `work/re/src/typec_analog_acc_dlkm/STATUS.md` (g5 confirmed 17.10.2026)

OS3 history of this directory: typec_analog_acc - EQUIVALENT

---

# typec_analog_acc - EQUIVALENT

compare_ko: all 4 functions byte-identical, relocations identical, exports/imports CRCs identical.
Built with the ThinLTO defconfig (typec_analog_acc_defconfig). Needed: `local_defines = CONFIG_LTO=1, CONFIG_UCSI_QTI_GLINK=1`; stub `re/stubs_misc:ucsi_stub` exporting
register_ucsi_glink_notifier (prototype from the kernel header linux/usb/ucsi_glink.h gives the stock CRC).
Notes: acc_priv is a 0xd0 byte object {int acc; struct notifier_block nb; struct snd_soc_jack jack;...};
the notifier handler must test `acc == AUDIO` first, then `acc == NONE` (the switch/if order decides the block layout);
the stock code dereferences acc_priv after the kmalloc failure message (no return) and reads priv = container_of(nb) after the first log.

## OS4 (17.10.2026)
Re-verified against the OS4 stock `typec_analog_acc_dlkm.ko`: see `work/re/src/typec_analog_acc_dlkm/STATUS.md` (same source, renamed to the OS4 module name; only the ThinLTO hash names needed updating for tas25xx).
