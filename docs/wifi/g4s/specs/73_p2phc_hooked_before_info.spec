# g4s r1: stock p2phc_p2px_event sets g_p2phc.hooked before the info message after nf_register_net_hooks
@@@ xiaomi_ext/p2phc_core.c
<<<<
		p2phc_info("%s nf_register_net_hooks", __func__);
		g_p2phc.hooked = true;
====
		g_p2phc.hooked = true;
		p2phc_info("%s nf_register_net_hooks", __func__);
>>>>
