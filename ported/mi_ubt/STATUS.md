# mi_ubt - EQUIVALENT (compare_ko.py all OK: 4 functions, 1 export, 28 imports)
Template: micode_bixi `drivers/xiaomi/mi_ubt/mi_ubt.c` (+ `include/mi_ubt/mi_ubt.h` in stubs_portA), used unchanged except:
- 6.6.144 `__task_state_index()` additionally reports TASK_FROZEN as uninterruptible; the stock (6.6.57) did not. Local `mi_task_state_index()` copy of the 6.6.57 logic,
  fed into the header's `task_index_to_char()` (keeps the `task_index_to_char.state_char` object).
Build: ThinLTO `mi_ubt_defconfig`, `-ffunction-sections -fdata-sections`, `CONFIG_LTO=1`.
