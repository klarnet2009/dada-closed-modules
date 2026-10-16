# speed_touch - EQUIVALENT

`compare_ko.py`: 3 functions, bytes, relocs, objects, 0 exports, 15 import CRCs, modinfo: all OK. `work/scripts/portC_data.py`: 13 data objects identical (params, ops, miscdevice), kCFI words identical.

Template: MiCode branch `warsaw-w-oss`, `drivers/mihw/input/speed_touch.c` (copied UNCHANGED, trailing Makefile text of my `git show` stripped). The stock was built from exactly this source (6.6 API: nothing needed porting).
Build: ThinLTO defconfig, `CONFIG_LTO=1`, `-ffunction-sections -fdata-sections`. No `#line` pins needed (pr_err only).
Provider stub: `re/stubs_portC:metis_stub` (`metis.ko`, 6 exports used by speed_touch: `in_interest_input_event`, `speed_touch_{store,restore}_freq`, `trigger_fboost_work`, `speed_touch_{producer,consumer}_in_boost`); all 6 CRCs match the stock metis exports (so `depends=metis` also matches).
