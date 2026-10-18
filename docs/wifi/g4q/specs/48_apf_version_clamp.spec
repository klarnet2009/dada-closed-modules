# g4q 48: hdd_post_get_apf_capabilities_rsp (stock): an APF version >= 6001 is reported to user space as 4 ("APF Version ret: %u -> 4"); hdd_ctx->apf_version keeps the real value
@@@ core/hdd/src/wlan_hdd_apf.c
<<<<
	struct sk_buff *skb;
	uint32_t nl_buf_len;

	hdd_enter();

	nl_buf_len = NLMSG_HDRLEN;
====
	struct sk_buff *skb;
	uint32_t nl_buf_len;
	uint32_t apf_version;

	hdd_enter();

	nl_buf_len = NLMSG_HDRLEN;
>>>>
<<<<
		  apf_get_offload->max_bytes_for_apf_inst);

	if (nla_put_u32(skb, APF_PACKET_SIZE,
			apf_get_offload->max_bytes_for_apf_inst) ||
	    nla_put_u32(skb, APF_VERSION, apf_get_offload->apf_version)) {
====
		  apf_get_offload->max_bytes_for_apf_inst);

	apf_version = apf_get_offload->apf_version;
	if (apf_version > 6000) {
		hdd_err("APF Version ret: %u -> 4", apf_version);
		apf_version = 4;
	}

	if (nla_put_u32(skb, APF_PACKET_SIZE,
			apf_get_offload->max_bytes_for_apf_inst) ||
	    nla_put_u32(skb, APF_VERSION, apf_version)) {
>>>>
