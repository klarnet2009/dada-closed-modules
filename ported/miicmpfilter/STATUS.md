# miicmpfilter - EQUIVALENT

Template: XRING `xiaomi-modules/mi_net/miicmpfilter/` (dijun-v-oss), used UNCHANGED (5 files: `miicmpfilter_core.c`, `miicmpfilter_nl.c`, 2 headers). Same vendor code, no platform glue.
compare_ko: 10 functions identical, relocations, objects (incl. `imcp_unreach_genl_family` in `.data..ro_after_init`, ops/policy tables), 0 exports, 16 imports + CRCs, modinfo (`GPL v2`, author, description, `rtnl-link-miicmpfilter` alias, version 1.0): all OK -> EQUIVALENT.
Builds as-is against 6.6.144 (`.resv_start_op` path of the `#if LINUX_VERSION_CODE > 6.1` is taken).
