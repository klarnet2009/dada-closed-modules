# g4q 45: generate_pmkid (stock): an all-zero PMKSA PMKID is not appended; message "vdev:%d PMKID is 0" when nothing remains
@@@ cmn/umac/cmn_services/crypto/src/wlan_crypto_global_api.c
<<<<
	int32_t random_pmkid;
	uint8_t *pmkid_buf = NULL, *temp_ptr = NULL;
====
	int32_t random_pmkid;
	uint8_t *pmkid_buf = NULL, *temp_ptr = NULL;
	uint8_t zero_pmkid[PMKID_LEN] = {0};
>>>>
<<<<
	if (pmksa)
		*pmkid_cnt = 1;

	*pmkid_cnt += random_pmkid;

	if (*pmkid_cnt > 1)
		crypto_debug("Appending %d PMKIDs to the RSN IE", *pmkid_cnt);
====
	if (pmksa && qdf_mem_cmp(pmksa->pmkid, zero_pmkid, PMKID_LEN))
		*pmkid_cnt = 1;

	*pmkid_cnt += random_pmkid;

	if (*pmkid_cnt == 0) {
		crypto_debug("vdev:%d PMKID is 0", wlan_vdev_get_id(vdev));
		return NULL;
	}

	if (*pmkid_cnt > 1)
		crypto_debug("Appending %d PMKIDs to the RSN IE", *pmkid_cnt);
>>>>
<<<<
	if (pmksa) {
		qdf_mem_copy(pmkid_buf, pmksa->pmkid, PMKID_LEN);
		pmkid_buf += PMKID_LEN;
	}

	if (random_pmkid)
		qdf_get_random_bytes(pmkid_buf, random_pmkid * PMKID_LEN);
====
	if (pmksa && qdf_mem_cmp(pmksa->pmkid, zero_pmkid, PMKID_LEN)) {
		qdf_mem_copy(pmkid_buf, pmksa->pmkid, PMKID_LEN);
		pmkid_buf += PMKID_LEN;
	}

	if (random_pmkid)
		qdf_get_random_bytes(pmkid_buf, random_pmkid * PMKID_LEN);
>>>>
