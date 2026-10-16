// SPDX-License-Identifier: GPL-2.0
/* Stub provider of mca_sysfs (only the export CRCs matter; the include set is chosen so that genksyms reproduces the stock CRCs). */
#include <linux/debugfs.h>
#include <mca/common/mca_sysfs.h>

int mca_sysfs_create_link_group(const char *dev_name, const char *link_name, struct device *target_dev,
	const struct attribute_group *group) { return 0; }
EXPORT_SYMBOL(mca_sysfs_create_link_group);
void mca_sysfs_remove_link_group(const char *dev_name, const char *link_name, struct device *target_dev,
	const struct attribute_group *group) { }
EXPORT_SYMBOL(mca_sysfs_remove_link_group);
void mca_sysfs_init_attrs(struct attribute **attrs, struct mca_sysfs_attr_info *attr_info, int size) { }
EXPORT_SYMBOL(mca_sysfs_init_attrs);
struct mca_sysfs_attr_info *mca_sysfs_lookup_attr(const char *name, struct mca_sysfs_attr_info *attr_info, int size)
{ return NULL; }
EXPORT_SYMBOL(mca_sysfs_lookup_attr);
int mca_debugfs_create_group(const char *dir_name, struct mca_debugfs_attr_info *attr_info, const int attr_size,
	void *dev_data) { return 0; }
EXPORT_SYMBOL(mca_debugfs_create_group);

