/* SPDX-License-Identifier: GPL-2.0 */
/* mca_sysfs.h - reconstructed (struct layouts from the stock mca_sysfs.ko BTF, helper macros from the call sites) */
#ifndef __MCA_SYSFS_H__
#define __MCA_SYSFS_H__

#include <linux/device.h>
#include <linux/sysfs.h>

#define SYSFS_DEV_1	"charger"
#define SYSFS_DEV_2	"fuelgauge"

struct mca_sysfs_attr_info {
	struct device_attribute attr;
	int sysfs_attr_name;
};

#define mca_sysfs_attr_ro(_func, _mode, _id, _name)			\
	{								\
		.attr = __ATTR(_name, _mode, _func##_show, NULL),	\
		.sysfs_attr_name = _id,					\
	}
#define mca_sysfs_attr_rw(_func, _mode, _id, _name)			\
	{								\
		.attr = __ATTR(_name, _mode, _func##_show, _func##_store),\
		.sysfs_attr_name = _id,					\
	}
#define mca_sysfs_attr_wo(_func, _mode, _id, _name)			\
	{								\
		.attr = __ATTR(_name, _mode, NULL, _func##_store),	\
		.sysfs_attr_name = _id,					\
	}

typedef ssize_t (*mca_debugfs_show)(void *priv_data, char *buf);
typedef ssize_t (*mca_debugfs_store)(void *priv_data, const char *buf, size_t count);

struct mca_debugfs_attr_info {
	const char *file;
	unsigned short mode;
	int debugfs_attr_name;
	mca_debugfs_show show;
	mca_debugfs_store store;
};

struct mca_debugfs_attr_data {
	struct mca_debugfs_attr_info *attr_info;
	void *private;
};

#define mca_debugfs_attr(_func, _mode, _id, _name)			\
	{								\
		.file = #_name,						\
		.mode = _mode,						\
		.debugfs_attr_name = _id,				\
		.show = _func##_show,					\
		.store = _func##_store,					\
	}

struct device *mca_sysfs_create_group(const char *cls_name, const char *dev_name,
	const struct attribute_group *group);
void mca_sysfs_remove_group(const char *cls_name, struct device *dev,
	const struct attribute_group *group);
int mca_sysfs_create_link_group(const char *dev_name, const char *link_name,
	struct device *target_dev, const struct attribute_group *group);
void mca_sysfs_remove_link_group(const char *dev_name, const char *link_name,
	struct device *target_dev, const struct attribute_group *group);
int mca_sysfs_create_files(const char *dev_name, struct mca_sysfs_attr_info *attr, const int attr_size);
void mca_sysfs_init_attrs(struct attribute **attrs, struct mca_sysfs_attr_info *attr_info, int size);
struct mca_sysfs_attr_info *mca_sysfs_lookup_attr(const char *name,
	struct mca_sysfs_attr_info *attr_info, int size);
int mca_debugfs_create_group(const char *dir_name, struct mca_debugfs_attr_info *attr_info,
	const int attr_size, void *dev_data);

#endif /* __MCA_SYSFS_H__ */
