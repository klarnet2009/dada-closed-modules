# g4q 53: the stock dp_tx_comp_handler / dp_tx_comp_handler_be have no DP_TX_DESC_FLAG_REAPED duplicate-entry check (newer CAF code, removed)
@@@ cmn/dp/wifi3.0/dp_tx.c
<<<<
			if (qdf_unlikely(tx_desc->flags &
			    DP_TX_DESC_FLAG_REAPED)) {
				dp_tx_comp_alert("Txdesc duplicate entry, flags = %x,id = %d",
						 tx_desc->flags, tx_desc->id);
				qdf_assert_always(0);
			}

			tx_desc->flags |= DP_TX_DESC_FLAG_REAPED;

			/* Collect hw completion contents */
			hal_tx_comp_desc_sync_wrapper(tx_comp_hal_desc,
						      tx_desc_pool, tx_desc,
====
			/* Collect hw completion contents */
			hal_tx_comp_desc_sync_wrapper(tx_comp_hal_desc,
						      tx_desc_pool, tx_desc,
>>>>
@@@ cmn/dp/wifi3.0/be/dp_be_tx.c
<<<<
			if (qdf_unlikely(tx_desc->flags &
			    DP_TX_DESC_FLAG_REAPED)) {
				dp_tx_comp_alert("Txdesc duplicate entry, flags = %x,id = %d",
						 tx_desc->flags, tx_desc->id);
				qdf_assert_always(0);
			}

			tx_desc->flags |= DP_TX_DESC_FLAG_REAPED;

			/* Collect hw completion contents */
====
			/* Collect hw completion contents */
>>>>
