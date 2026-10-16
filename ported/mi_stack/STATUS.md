# mi_stack - EQUIVALENT (compare_ko.py all OK: 5 functions, 25 imports)
Template: micode_bixi `drivers/xiaomi/mi_stack/mi_stack.c`, source unchanged except the lock-key hack: stock `.bss` is merged, so `mi_stack_init.__key` and `kbuf` share address 0
(relocs resolved to the key name). Fix: `portA_lockkeys.h` (stubs_portA) redefines mutex_init() with the key in `.bss.pa_lockkeys` and `kbuf` is put in the same section (key first).
