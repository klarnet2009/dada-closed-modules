# mi_trace - EQUIVALENT (compare_ko.py all OK: 14 functions, 35 imports incl. msm_minidump_{add,remove}_region CRCs, depends=minidump)
Template: micode_bixi `drivers/xiaomi/mi_trace/{mi_trace.c,mi_trace.h}`, source unchanged. Needs `CONFIG_QCOM_MINIDUMP=1` (otherwise <soc/qcom/minidump.h> turns msm_minidump_* into inline stubs and the calls vanish)
and a provider of the msm_minidump_* exports: `//re/stubs_portA:minidump_stub` (stub; real provider is the stock minidump.ko, see ../minidump).
