/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2023 XiaoMi, Inc.
 * All Rights Reserved.
 */

#ifndef MITEE_TUI_H
#define MITEE_TUI_H

#include <linux/types.h>
#include "mitee_smc_notify.h"
#include "optee_msg.h"
#include "tui_common.h"

#define TUI_TAG "mitee-tui "

extern struct device tui_dev;
extern int mitee_tui_init(void);
extern int mitee_tui_exit(void);
extern struct mitee_tui_response_t g_user_rsp;
extern void reset_global_command_id(void);
extern uint32_t send_cmd_to_user(uint32_t command_id);
extern int tui_get_panel_vendor(void);
struct optee_msg_param_value;
extern uint32_t tui_process_tuisvc_callback(struct optee_msg_param_value *value, void *buf,
					    uint32_t size_in, uint32_t *size_out);
struct tui_ioctl_buf_data;
struct mitee_tui_command_t;
extern int tui_receive_request(struct mitee_tui_command_t *cmd_id);
extern int tui_send_response(struct tui_ioctl_buf_data *data);

#define tui_dev_err(__ret__, fmt, ...) \
	dev_err(&tui_dev, "ERROR %d %s: " fmt "\n", \
		__ret__, __func__, ##__VA_ARGS__)

#define tui_dev_info(fmt, ...) \
	dev_info(&tui_dev, "%s: " fmt "\n", __func__, ##__VA_ARGS__)

#ifdef DEBUG
#define tui_dev_devel(fmt, ...) \
	dev_info(&tui_dev, "DEBUG %s: " fmt "\n", __func__, ##__VA_ARGS__)
#else /* DEBUG */
#define tui_dev_devel(...)		do {} while (0)
#endif /* !DEBUG */

/*
 * defines for the ioctl TUI driver module function call from user space.
 */
#define TUI_DEV_NAME	"mitee-tui"
#define TUI_SUPP_DEV_NAME	"mitee-tui-supp"

#define TUI_IO_MAGIC	't'

#define TUI_IO_NOTIFY	_IO(TUI_IO_MAGIC, 1)
#define TUI_IO_WAITCMD	_IO(TUI_IO_MAGIC, 2)
#define TUI_IO_ACK	_IO(TUI_IO_MAGIC, 3)
#define TUI_CLOSE_SESSION _IO(TUI_IO_MAGIC, 4)
#define TUI_IO_NOTIFY_CANCEL _IO(TUI_IO_MAGIC, 5)
#define TUI_IO_REGISTER_CALLBACK _IO(TUI_IO_MAGIC, 6)
#define TUI_IO_ALLOC_SHM _IO(TUI_IO_MAGIC, 7)
#define TUI_IO_FREE_SHM _IO(TUI_IO_MAGIC, 8)

//notify cancel macro
#define TUI_MAGIC_NUM 0x0a0b0c0d
#define TUI_SVC_ID 0x0
/**
 * Return codes of driver commands.
 */
#define TUI_OK                      0
#define TUI_ERR_UNKNOWN_CMD         1
#define TUI_ERR_NOT_SUPPORTED       2
#define TUI_ERR_INTERNAL_ERROR      3
#define TUI_ERR_NO_RESPONSE         4
#define TUI_ERR_BAD_PARAMETERS      5
#define TUI_ERR_NO_EVENT            6
#define TUI_ERR_OUT_OF_MEMORY       7
#define TUI_ERR_TIMEOUT             8
/* ... add more error codes when needed */

#define CMD_TUI_SVC_NONE             0
#define CMD_TUI_SVC_GET_SCREEN_INFO  1
#define CMD_TUI_SVC_OPEN_SESSION     2
#define CMD_TUI_SVC_CLOSE_SESSION    3
#define CMD_TUI_SVC_GET_FONT_BUFFER  4
#define CMD_TUI_SVC_INIT_TOUCH       5
#define CMD_TUI_SVC_INIT_DISPLAY     6
#define CMD_TUI_SVC_DEINIT_TOUCH     7
#define CMD_TUI_SVC_DEINIT_DISPLAY   8

/* Command header */
struct mitee_tui_command_t {
	uint32_t id;
};

/* Resolution */
struct mitee_tui_resolution_t {
	uint32_t width;
	uint32_t height;
	uint32_t dpi;
};

/* tui_shm construct */
struct mitee_tui_shm_t {
	uint8_t shm_name[16];
	int shm_fd;
	int32_t shm_id;
	uint64_t shm_size;
};

/* Response header */
struct mitee_tui_response_t {
	uint32_t id;
	uint32_t return_code;
	union {
		struct mitee_tui_resolution_t resolution;
		struct mitee_tui_shm_t fontShm;
	} data;
};

struct screen_info_t {
	uint64_t width;
	uint64_t height;
	enum tpd_vendor_id_t panel_vendor;
	uint64_t pa; // pa or sfd
};

/* font info*/
struct font_info {
	uint64_t font_size;
	uint8_t *font_buffer;
	struct tui_shm *font_shm;
};

/* tui_ioctl_data */
struct tui_ioctl_buf_data {
	uint64_t buf_ptr;
	uint64_t buf_len;
};

struct tui_alloc_buffer_t {
	uint64_t pa; // pa or sfd
	uint64_t ffa_handle; // unused
	struct dma_buf *dma_buf;
};

/* Command IDs */
#define MITEE_TUI_CMD_NONE                0
#define MITEE_TUI_CMD_GET_RESOLUTION      1
#define MITEE_TUI_CMD_OPEN_SESSION        2
#define MITEE_TUI_CMD_CLOSE_SESSION       3
#define MITEE_TUI_CMD_GET_FONT_BUFFER     4

/* Return codes */
#define MITEE_TUI_OK                  0
#define MITEE_TUI_ERROR               1
#define MITEE_TUI_ERR_UNKNOWN_CMD     2

void hal_get_fb_buffer(struct tui_alloc_buffer_t **buffer);

extern int tpd_enter_tui(void);
extern int tpd_exit_tui(void);
extern enum tpd_vendor_id_t tpd_tui_get_vendor_id(void);
extern void mitee_rpc_register_callback(uint32_t module_id, uint32_t cmd,
	uint32_t (*callback)(struct optee_msg_param_value *value, void *buf, uint32_t size_in, uint32_t *size_out));

#endif /* MITEE_TUI_H */
