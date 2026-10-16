# millet_core - EQUIVALENT

Template: XRING `dijun-v-oss` branch `xring-modules/drivers/mihw/millet/millet_core.c` + `millet.h` + `uapi/millet.h` (fetched from `<build-dir>`, copy in `work/re/ref/portD/dijun_millet/`).
Builds against 6.6.144 unchanged (ThinLTO `millet_core_defconfig`, `CONFIG_LTO=1`, function/data sections).

compare_ko: 14 functions byte-identical, relocations identical, global objects, 13 exports + CRCs, 30 imports + CRCs, modinfo: all OK -> EQUIVALENT.

Changes relative to the template (the stock is a newer revision than the XRING one):
- `NETLINK_MILLET` is 31 (template 30).
- new exported globals: `atomic_t core_uid_rec[74]`, `atomic_t core_black_pid_rec[10]`, `atomic_t core_black_uid_rec[10]`, `int freezebinder_enable` (used by millet_pkg/millet_binder).
- `dump_send_msg()` also prints `"up sig: 0x%lu\n", msg->pri[0]` when `owner == SIG_TYPE`.
- `millet_sock_show()` appends the three `core_*_rec` tables (`"core_uid_rec:\n"`, `"%d,"`, ...; the value is read once into a local).
- `millet_sendto_user()`: the `has_init` / `loop_init` failure paths return without `kfree_skb()` (the stock leaks the skb there; reproduced).
- `judge_millet_freeze_switch`, `millet_v`, `millet_rootdir` are global but not exported (as in stock).
