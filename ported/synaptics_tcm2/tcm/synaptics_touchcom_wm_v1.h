/* body of syna_tcm_v1_write_message / _timeout: included twice by synaptics_touchcom_core_v1.c (the stock has two full copies, with their own __func__) */
static int WM_NAME(struct tcm_dev *tcm_dev,
		unsigned char command, unsigned char *payload,
		unsigned int length_total, unsigned int length,
		unsigned char *resp_code, unsigned int delay_ms_resp WM_TMO_PARAM)
{
	int retval = 0;
	unsigned int idx;
	unsigned int chunks;
	unsigned int chunk_space;
	unsigned int xfer_length;
	unsigned int remaining_length;
	int timeout = 0;
	int polling_ms = 0;
	struct tcm_message_data_blob *tcm_msg = NULL;
	syna_pal_mutex_t *cmd_mutex = NULL;
	syna_pal_mutex_t *rw_mutex = NULL;
	syna_pal_completion_t *cmd_completion = NULL;
	bool has_irq_ctrl = false;
	bool in_polling = false;
	bool last = false;
	unsigned char tmp;
	unsigned short crc16 = 0xFFFF;

	if (!tcm_dev) {
		LOGE("Invalid tcm device handle\n");
		return -ERR_INVAL;
	}
	WM_TMO_INIT

#ifdef TOUCH_SYNA_BOOTLOADER_RECOVERY
	/* check fw reflash status */
	if (ATOMIC_GET(tcm_dev->firmware_flashing) && command != CMD_IDENTIFY && command != CMD_GET_BOOT_INFO && command != CMD_RESET && command != CMD_ERASE_FLASH && command != CMD_WRITE_FLASH && command != CMD_RUN_BOOTLOADER_FIRMWARE) {
		LOGE("ic is updating , %x command is invalid now\n", command);
		return -ERR_INVAL;
	}
#endif

	tcm_msg = &tcm_dev->msg_data;
	cmd_mutex = &tcm_msg->cmd_mutex;
	rw_mutex = &tcm_msg->rw_mutex;
	cmd_completion = &tcm_msg->cmd_completion;

	if (resp_code)
		*resp_code = STATUS_INVALID;
	/* indicate which mode is used */
	in_polling = (delay_ms_resp != RESP_IN_ATTN);

	/* irq control is enabled only when the operations is implemented
	 * and the current status of irq is enabled.
	 * do not enable irq if it is disabled by someone.
	 */
	has_irq_ctrl = (bool)(tcm_dev->hw_if->ops_enable_irq != NULL);
	has_irq_ctrl &= tcm_dev->hw_if->bdata_attn.irq_enabled;

	/* disable irq when using polling mode */
	if (has_irq_ctrl && in_polling && tcm_dev->hw_if->ops_enable_irq)
		tcm_dev->hw_if->ops_enable_irq(tcm_dev->hw_if, false);
	/*enable irq when using int mode*/
	if (!has_irq_ctrl && !in_polling && tcm_dev->hw_if->ops_enable_irq)
		tcm_dev->hw_if->ops_enable_irq(tcm_dev->hw_if, true);

	syna_pal_mutex_lock(cmd_mutex);

	syna_pal_mutex_lock(rw_mutex);

	ATOMIC_SET(tcm_msg->command_status, CMD_STATE_BUSY);

	/* reset the command completion */
	syna_pal_completion_reset(cmd_completion);

	tcm_msg->command = command;

	remaining_length = length;

	if (length != length_total) {
		LOGD("Command: 0x%02x, payload length: %d / %d\n",
			command, length, length_total);
	} else {
		LOGD("Command: 0x%02x, payload length: %d\n",
			command, length);
	}

	/* calculate the crc if supported */
	if (tcm_msg->has_crc) {
		crc16 = syna_tcm_crc16(&command, 1, crc16);
		tmp = (unsigned char)length & 0xff;
		crc16 = syna_tcm_crc16(&tmp, 1, crc16);
		tmp = (unsigned char)(length >> 8) & 0xff;
		crc16 = syna_tcm_crc16(&tmp, 1, crc16);
		if (length > 0)
			crc16 = syna_tcm_crc16(payload, length, crc16);

		LOGD("CRC to write: 0x%04X\n", crc16);
	}

	/* available space for payload = total size - command byte */
	if (tcm_dev->max_wr_size == 0)
		chunk_space = remaining_length;
	else
		chunk_space = tcm_dev->max_wr_size - 1;

	chunks = syna_pal_ceil_div(remaining_length, chunk_space);
	chunks = chunks == 0 ? 1 : chunks;

	/* send out command packets
	 *
	 * separate into several sub-packets if the overall size is over
	 * than the maximum write size.
	 */
	for (idx = 0; idx < chunks; idx++) {
		if (remaining_length > chunk_space)
			xfer_length = chunk_space;
		else
			xfer_length = remaining_length;

		last = ((idx + 1) == chunks);

		if (idx == 0) {
			retval = syna_tcm_v1_write(tcm_dev,
					tcm_msg->command,
					&payload[0],
					xfer_length,
					(tcm_msg->has_crc) && last,
					crc16);
		} else {
			retval = syna_tcm_v1_write(tcm_dev,
					CMD_CONTINUE_WRITE,
					&payload[idx * chunk_space],
					xfer_length,
					(tcm_msg->has_crc) && last,
					crc16);
		}

		if (retval < 0) {
			LOGE("Fail to write %d bytes to device\n",
				xfer_length);
			syna_pal_mutex_unlock(rw_mutex);
			goto exit;
		}

		remaining_length -= xfer_length;

		if (chunks > 1)
			syna_pal_sleep_us(WR_DELAY_US_MIN, WR_DELAY_US_MAX);
	}

	syna_pal_mutex_unlock(rw_mutex);

	/* handle the command response
	 *
	 * assuming to select the polling mode, the while-loop below will
	 * repeatedly read in the respose data based on the given polling
	 * time; otherwise, wait until receiving a completion event from
	 * interrupt thread.
	 */
	timeout = 0;
	if (!in_polling)
		polling_ms = timeout_ms_resp;
	else
		polling_ms = delay_ms_resp;

	do {
		/* wait for the completion event triggered by read_message */
		retval = syna_pal_completion_wait_for(cmd_completion,
				polling_ms);

		/* break the loop once a resp was ready */
		if (ATOMIC_GET(tcm_msg->command_status) == CMD_STATE_IDLE)
			goto check_response;

#ifdef TOUCH_SYNA_BOOTLOADER_RECOVERY
		if (ATOMIC_GET(tcm_dev->firmware_flashing) == 1 && command != CMD_IDENTIFY && command != CMD_GET_BOOT_INFO && command != CMD_RESET && command != CMD_ERASE_FLASH && command != CMD_WRITE_FLASH && command != CMD_RUN_BOOTLOADER_FIRMWARE) {
			goto check_response;
		}
#endif
		/* otherwise, keep in polling */
		if (in_polling) {
			ATOMIC_SET(tcm_msg->command_status, CMD_STATE_BUSY);

			/* retrieve the message packet back */
			retval = syna_tcm_v1_read_message(tcm_dev, NULL);
			/* keep in polling if still not having a valid resp */
			if (retval < 0)
				syna_pal_completion_reset(cmd_completion);
		}

		timeout += polling_ms;

	} while (timeout < timeout_ms_resp);

	/* check the status of response data
	 * according to the touchcomm spec, each command message
	 * should have an associated response message.
	 */
check_response:
	if (ATOMIC_GET(tcm_msg->command_status) != CMD_STATE_IDLE) {
		if (timeout >= timeout_ms_resp) {
			LOGE("Timed out wait for response of command 0x%02x\n",
				command);
			retval = -ERR_TIMEDOUT;
			goto exit;
		} else {
			LOGE("Fail to get valid response of command 0x%02x\n",
				command);
			retval = -ERR_TCMMSG;
			goto exit;
		}
	}

	/* copy response code to the caller */
	if (resp_code)
		*resp_code = tcm_msg->status_report_code;

	retval = 0;
	if (tcm_msg->response_code != STATUS_OK) {
		LOGE("Received code 0x%02x (command 0x%02x)\n",
			tcm_msg->status_report_code, tcm_msg->command);
		retval = -ERR_TCMMSG;
	}

exit:
	tcm_msg->command = CMD_NONE;

	ATOMIC_SET(tcm_msg->command_status, CMD_STATE_IDLE);

	syna_pal_mutex_unlock(cmd_mutex);

	/* recovery the irq if using polling mode */
	if (has_irq_ctrl && in_polling && tcm_dev->hw_if->ops_enable_irq)
		tcm_dev->hw_if->ops_enable_irq(tcm_dev->hw_if, true);
	/*recovery the irq when using int mode*/
	if (!has_irq_ctrl && !in_polling && tcm_dev->hw_if->ops_enable_irq)
		tcm_dev->hw_if->ops_enable_irq(tcm_dev->hw_if, false);

	return retval;
}
