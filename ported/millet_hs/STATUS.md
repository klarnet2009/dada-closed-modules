# millet_hs - EQUIVALENT

Template: XRING `dijun-v-oss` `xring-modules/drivers/mihw/millet/millet_hs.c` (+ `millet.h`, `uapi/millet.h`; copies in `work/re/ref/portD/dijun_millet/`). Compiles unchanged on 6.6.144.
compare_ko: 5 functions byte-identical, relocations, objects, 0 exports, 8 imports + CRCs (from our `millet_core` build, `deps = //re/millet_core`), modinfo (`depends=millet_core`): all OK -> EQUIVALENT.
Only change in the shared header: see millet_sig (`frozen_task_group` has no `frozen_task()` term; `frozen_task` made `static inline` to satisfy -Werror; unused here).
