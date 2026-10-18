# g4s r1: stock dp_prealloc_init: for QDF_DP_TX_DESC_TYPE the element size (qdf_size_t at mp+8) is rewritten to 0x80 together with the element count
@@@ components/dp/core/src/wlan_dp_prealloc.c
<<<<
dp_update_num_elements_by_desc_type(struct wlan_dp_prealloc_cfg *cfg,
				    enum qdf_dp_desc_type desc_type,
				    uint16_t *num_elements)
{
	switch (desc_type) {
	case QDF_DP_TX_DESC_TYPE:
		*num_elements = cfg->num_tx_desc;
		return;
====
dp_update_num_elements_by_desc_type(struct wlan_dp_prealloc_cfg *cfg,
				    enum qdf_dp_desc_type desc_type,
				    uint16_t *num_elements,
				    qdf_size_t *element_size)
{
	switch (desc_type) {
	case QDF_DP_TX_DESC_TYPE:
		*num_elements = cfg->num_tx_desc;
		*element_size = TX_DESC_SIZE;
		return;
>>>>
<<<<
		dp_update_num_elements_by_desc_type(&cfg, mp->desc_type,
						    &mp->element_num);
====
		dp_update_num_elements_by_desc_type(&cfg, mp->desc_type,
						    &mp->element_num,
						    &mp->element_size);
>>>>
