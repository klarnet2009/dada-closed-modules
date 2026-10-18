# g4s r1: stock __dlhc_init allocates the slot arrays with kzalloc (__kmalloc flags 0xdc0 = GFP_KERNEL|__GFP_ZERO), not kmalloc
@@@ xiaomi_ext/dlhc.c
<<<<
		comp->rstate = kmalloc(rslots * sizeof(struct dl_cstate), GFP_KERNEL);
====
		comp->rstate = kzalloc(rslots * sizeof(struct dl_cstate), GFP_KERNEL);
>>>>
<<<<
		comp->tstate = kmalloc(tslots * sizeof(struct dl_cstate), GFP_KERNEL);
====
		comp->tstate = kzalloc(tslots * sizeof(struct dl_cstate), GFP_KERNEL);
>>>>
