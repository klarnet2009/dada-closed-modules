# typec_analog_acc - EQUIVALENT

compare_ko: all 4 functions byte-identical, relocations identical, exports/imports CRCs identical.
Built with the ThinLTO defconfig (typec_analog_acc_defconfig). Needed: `local_defines = CONFIG_LTO=1, CONFIG_UCSI_QTI_GLINK=1`; stub `re/stubs_misc:ucsi_stub` exporting
register_ucsi_glink_notifier (prototype from the kernel header linux/usb/ucsi_glink.h gives the stock CRC).
Notes: acc_priv is a 0xd0 byte object {int acc; struct notifier_block nb; struct snd_soc_jack jack;...};
the notifier handler must test `acc == AUDIO` first, then `acc == NONE` (the switch/if order decides the block layout);
the stock code dereferences acc_priv after the kmalloc failure message (no return) and reads priv = container_of(nb) after the first log.
