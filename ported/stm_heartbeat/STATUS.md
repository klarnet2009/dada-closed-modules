# stm_heartbeat - EQUIVALENT (compare_ko.py all OK)
Template: micode_bixi `drivers/hwtracing/stm/heartbeat.c` (upstream Linux drivers/hwtracing/stm), copied UNCHANGED (+ `stm.h`) and built against 6.6.144 headers.
Needs //re/stm_core:stm_core as dep (import CRCs of stm_register_protocol/stm_source_* exports match). Build: ThinLTO defconfig, function/data sections, CONFIG_LTO=1.
