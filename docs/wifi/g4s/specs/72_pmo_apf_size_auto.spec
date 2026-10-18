# g4s r1: stock pmo_get_apf_instruction_size returns 0x800 (not 0) when APF is enabled
@@@ components/pmo/core/src/wlan_pmo_apf.c
<<<<
#define PMO_APF_SIZE_AUTO	0
====
#define PMO_APF_SIZE_AUTO	0x800
>>>>
