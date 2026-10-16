#include <linux/errno.h>
#include <linux/uaccess.h>
#include <asm/io.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <asm/setup.h>
#include <linux/kobject.h>
#include <linux/slab.h>
#include <linux/bitops.h>
#include <soc/qcom/minidump.h>

#define MAX_HARDWAREINFO_LEN	4*1024
#define DATABUF_LEN		256
#define OFFSET(x)		strlen(x)

static DEFINE_MUTEX(hwinfo_mutex);
char *hardwareinfo_buff = NULL;

static ssize_t read_xiaomi_hardw_info(struct kobject* kodjs,struct kobj_attribute *attr,char *buf)
{
	return sprintf(buf, "%s\n",hardwareinfo_buff);
}

static ssize_t write_xiaomi_hardw_info(struct kobject *kobj, struct kobj_attribute *attr,const char *buf, size_t len)
{
	char *mid_buff = NULL;
	mutex_lock(&hwinfo_mutex);
	mid_buff = (char *)kzalloc(DATABUF_LEN, GFP_KERNEL);
	snprintf(mid_buff, DATABUF_LEN, "%s", buf);

	if(!OFFSET(mid_buff)){
		pr_err("mid_buff is not exist\n");
		goto fail_end;
	}
	if(OFFSET(mid_buff) >= (MAX_HARDWAREINFO_LEN - OFFSET(hardwareinfo_buff)))
	{
		pr_err("Fail get data {%s},Out of hardwareinfo_buff MAX.\n",mid_buff);
		goto fail_end;
	}else{
		strncpy(hardwareinfo_buff+OFFSET(hardwareinfo_buff),mid_buff,OFFSET(mid_buff));
	}

	kfree(mid_buff);
	mutex_unlock(&hwinfo_mutex);
	return len;
fail_end:
	kfree(mid_buff);
	mutex_unlock(&hwinfo_mutex);
	return -1;
}
static struct kobj_attribute hardw_info_attr = __ATTR(hardw_info, S_IWUSR|S_IRUSR, read_xiaomi_hardw_info, write_xiaomi_hardw_info);

static struct attribute *xiaomi_hardwareinfo_attrs[] = {
	&hardw_info_attr.attr,
   NULL,
};

static struct attribute_group attr_hardwareinfo_group = {
   .attrs = xiaomi_hardwareinfo_attrs,
};

static struct kobject *hardware_info_to_dump_kobj;

static int __init hardware_info_to_dump_init(void)
{
	int ret;
	struct md_region md_entry = {0};

	/*Allocate memory*/
	hardwareinfo_buff = kzalloc(MAX_HARDWAREINFO_LEN, GFP_KERNEL);
	if(!hardwareinfo_buff)
	{
		pr_err("Failed to kzalloc hardwareinfo_buff\n");
		ret = -ENOMEM;
		goto build_fail;
	}

	strlcpy(md_entry.name, "HARDW_INFO", sizeof(md_entry.name));
	md_entry.virt_addr = (uintptr_t)hardwareinfo_buff;
	md_entry.phys_addr = virt_to_phys(hardwareinfo_buff);
	md_entry.size = MAX_HARDWAREINFO_LEN;
	ret = msm_minidump_add_region(&md_entry);
	if (ret < 0) {
		pr_err("Failed to add hardw_info data in Minidump\n");
		goto build_fail;
	}

	hardware_info_to_dump_kobj = kobject_create_and_add("hardware_info_to_dump", kernel_kobj);
	if(!hardware_info_to_dump_kobj){
		pr_err("hardware_info_to_dump create fail \n");
		return -ENOMEM;
	}

	ret = sysfs_create_group(hardware_info_to_dump_kobj, &attr_hardwareinfo_group);
	if (ret){
		sysfs_remove_group(hardware_info_to_dump_kobj, &attr_hardwareinfo_group);
		kobject_del(hardware_info_to_dump_kobj);
		pr_err("sysfs_create_group create fail \n");
	}

build_fail:
	return ret;
}

static void __exit hardware_info_to_dump_exit(void)
{
	if(hardware_info_to_dump_kobj){
		sysfs_remove_group(hardware_info_to_dump_kobj, &attr_hardwareinfo_group);
		kobject_del(hardware_info_to_dump_kobj);
	}
}

module_init(hardware_info_to_dump_init);
module_exit(hardware_info_to_dump_exit);
MODULE_LICENSE("GPL");
