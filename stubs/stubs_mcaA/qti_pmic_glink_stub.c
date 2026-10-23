// SPDX-License-Identifier: GPL-2.0
/* Stub provider of the qti_pmic_glink.ko exports (server GKI header linux/soc/qcom/qti_pmic_glink.h). */
/* include list of the real provider (drivers/soc/qcom/qti_pmic_glink.c): which struct bodies are visible when
 * the export is processed changes the expansion of struct device inside the symbol CRC */
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/idr.h>
#include <linux/ipc_logging.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/rpmsg.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <linux/remoteproc/qcom_rproc.h>
#include <linux/soc/qcom/pdr.h>
#include <linux/soc/qcom/qti_pmic_glink.h>

/* private structures of the real provider (drivers/soc/qcom/qti_pmic_glink.c): they are part of the symbol CRCs */
struct pmic_glink_dev {
	struct rpmsg_device	*rpdev;
	struct device		*dev;
	struct dentry		*debugfs_dir;
	const char		*channel_name;
	void			*ipc_log;
	struct idr		client_idr;
	struct mutex		client_lock;
	struct rw_semaphore	rpdev_sem;
	spinlock_t		rx_lock;
	struct work_struct	rx_work;
	struct work_struct	init_work;
	struct workqueue_struct	*rx_wq;
	struct list_head	rx_list;
	struct list_head	dev_list;
	atomic_t		state;
	atomic_t		prev_state;
	bool			child_probed;
	u32			log_filter;
	bool			log_enable;
	struct list_head	client_dev_list;
	struct notifier_block	ssr_nb;
	const char		*subsys_name;
	void			*subsys_handle;
	void			*pdr_handle;
	const char		*pdr_service_name;
	const char		*pdr_path_name;
	atomic_t		pdr_state;
};

struct pmic_glink_client {
	struct pmic_glink_dev	*pgdev;
	const char		*name;
	u32			id;
	struct mutex		lock;
	void			*priv;
	int			(*msg_cb)(void *priv, void *data, size_t len);
	struct list_head	node;
	void			(*state_cb)(void *priv, enum pmic_glink_state state);
};

struct pmic_glink_client *pmic_glink_register_client(struct device *dev,
		const struct pmic_glink_client_data *client_data) { return NULL; }
EXPORT_SYMBOL(pmic_glink_register_client);
int pmic_glink_unregister_client(struct pmic_glink_client *client) { return 0; }
EXPORT_SYMBOL(pmic_glink_unregister_client);
int pmic_glink_write(struct pmic_glink_client *client, void *data, size_t len) { return 0; }
EXPORT_SYMBOL(pmic_glink_write);
MODULE_LICENSE("GPL");
