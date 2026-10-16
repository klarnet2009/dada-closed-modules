# mca_soc_limit - EQUIVALENT (reference = HyperOS 4.0 / OS4 stock `mca_soc_limit.ko`)

SAFETY: charging module (`mca_*`): source for the community only, never flash it / never put it into a ROM build.

Template: XRING `mca/mca_strategy/strategy_fg/mca_soc_limit.c` + `inc/mca_soc_limit.h` (copied as is).
Changes: (1) `#include <mca/strategy/strategy_soc_limit.h>` (not in the XRING tree) replaced by a one-line prototype `void soc_limit_process(bool enable, int soc_limit_thre);`
(kept on line 34 so that the `__LINE__` of the two log calls, 55 and 60, match the stock); (2) `MCA_EVENT_CHARGE_STATUS` -> `MCA_EVENT_TYPE_CHARGE_STATUS` (name in our reconstructed `mca_event.h`);
(3) header flattened (no `inc/` dir, listed in `srcs`). Same ThinLTO defconfig + `-ffunction-sections` as the other mca ports.
Providers are the generated OS4 stubs `//re/stubs_a17B/os4:os4_*` (exact OS4 export CRCs, see `work/a17/portB/gen_stubs.py`), not the HyperOS 3 stubs.

Result (compare_ko vs the OS4 stock): 5 functions byte-identical, relocations, objects, 1 export + CRC, 8 imports + CRCs, modinfo -> EQUIVALENT (the OS3 binary is identical to the OS4 one for this module).
