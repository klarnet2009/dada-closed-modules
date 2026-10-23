// SPDX-License-Identifier: GPL-2.0
/*
 * Reconstructed from the stock debug_ext.ko (Xiaomi 15 "dada") by reverse engineering; not original source.
 * dma-buf process statistics are derived from Qualcomm's minidump_memory.c (md_dma_buf_procs / get_dma_info).
 */
/*
 * The stock module was built against 6.6.57, whose generic __access_ok() lacks the later "size <= limit" check;
 * provide that older form so that the inlined _copy_from_user() is identical.
 */
#define __access_ok __access_ok
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/sched/task.h>
#include <linux/fdtable.h>
#include <linux/dma-buf.h>
#include <linux/jhash.h>
#include <linux/list.h>
#include <linux/list_sort.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/of.h>
#include <linux/of_reserved_mem.h>
#include <linux/reboot.h>
#include <linux/notifier.h>
#include <linux/uaccess.h>
#include <linux/qcom_dma_heap.h>

static inline int __access_ok(const void __user *ptr, unsigned long size)
{
	unsigned long limit = TASK_SIZE_MAX;
	unsigned long addr = (unsigned long)ptr;

	return addr <= (limit - size);
}

MODULE_IMPORT_NS(MINIDUMP);

/* provided by debug_symbol.ko (Qualcomm drivers/soc/qcom/debug_symbol.c) */
extern unsigned long debug_symbol_lookup_name(const char *name);

struct proc_dir_entry *entry_debug_ext;
struct proc_dir_entry *entry_dma_buf;
struct proc_dir_entry *entry_mem_statis;
struct proc_dir_entry *entry_set_panic;

/* ---- dma_buf debug ---- */

#define DMA_BUF_HASH_SIZE (1 << 13)
#define DMA_BUF_HASH_SEED 0x9747b28c
static bool dma_buf_hash[DMA_BUF_HASH_SIZE];

char *dma_buf_procs_addr;
size_t dma_buf_procs_size = SZ_256K;

struct priv_buf {
	char *buf;
	size_t size;
	size_t offset;
};

struct dma_buf_priv {
	struct priv_buf *priv_buf;
	struct task_struct *task;
	int count;
	size_t size;
};

struct dma_buf_node {
	struct dma_buf *dmabuf;
	struct list_head list;
};

struct dma_buf_proc {
	char comm[TASK_COMM_LEN];
	int pid;
	size_t size;
	struct list_head dma_list;
	struct list_head node;
};

static int get_dma_info_sort(const void *data, struct file *file, unsigned int n)
{
	struct dma_buf_proc *proc = (struct dma_buf_proc *)data;
	struct list_head *head = &proc->dma_list;
	struct dma_buf_node *node;
	struct dma_buf *dmabuf;

	if (!qcom_is_dma_buf_file(file))
		return 0;

	dmabuf = file->private_data;
	list_for_each_entry(node, head, list) {
		if (node->dmabuf == dmabuf)
			return 0;
	}

	node = kzalloc(sizeof(*node), GFP_ATOMIC);
	if (!node)
		return -ENOMEM;

	get_file(file);
	node->dmabuf = file->private_data;
	proc->size += ((struct dma_buf *)file->private_data)->size >> 10;
	list_add(&node->list, &proc->dma_list);
	return 0;
}

static int dmacmp(void *priv, const struct list_head *a, const struct list_head *b)
{
	struct dma_buf_node *na = container_of(a, struct dma_buf_node, list);
	struct dma_buf_node *nb = container_of(b, struct dma_buf_node, list);

	return nb->dmabuf->size - na->dmabuf->size;
}

static void free_proc(struct dma_buf_proc *proc)
{
	struct dma_buf_node *node, *tmp;

	list_for_each_entry_safe(node, tmp, &proc->dma_list, list) {
		if (node->dmabuf && node->dmabuf->file)
			fput(node->dmabuf->file);
		list_del(&node->list);
		kfree(node);
	}
	kfree(proc);
}

static int proccmp(void *priv, const struct list_head *a, const struct list_head *b)
{
	struct dma_buf_proc *pa = container_of(a, struct dma_buf_proc, node);
	struct dma_buf_proc *pb = container_of(b, struct dma_buf_proc, node);

	return pb->size - pa->size;
}

int dma_buf_procs_sort_show(struct seq_file *s, void *unused)
{
	struct task_struct *task, *thread;
	struct dma_buf_proc *proc, *tmp;
	struct dma_buf_node *node;
	LIST_HEAD(proc_list);
	int ret = 0;

	read_lock(&tasklist_lock);
	for_each_process(task) {
		proc = kzalloc(sizeof(*proc), GFP_ATOMIC);
		if (!proc) {
			read_unlock(&tasklist_lock);
			ret = -ENOMEM;
			goto out;
		}
		INIT_LIST_HEAD(&proc->dma_list);
		for_each_thread(task, thread) {
			task_lock(thread);
			if (thread->files)
				ret = iterate_fd(thread->files, 0, get_dma_info_sort, proc);
			task_unlock(thread);
		}
		if (ret || list_empty(&proc->dma_list)) {
			free_proc(proc);
			continue;
		}
		list_sort(NULL, &proc->dma_list, dmacmp);
		get_task_comm(proc->comm, task);
		proc->pid = task->tgid;
		list_add(&proc->node, &proc_list);
	}
	read_unlock(&tasklist_lock);

	list_sort(NULL, &proc_list, proccmp);
	list_for_each_entry(proc, &proc_list, node) {
		seq_printf(s, "\n%s (PID %d) size(KB): %ld\nDMA Buffers:\n", proc->comm, proc->pid, proc->size);
		seq_printf(s, "%-12s\t%-10s\t%-8s\n", "exp_name", "Size (KB)", "inode");
		list_for_each_entry(node, &proc->dma_list, list) {
			struct dma_buf *dmabuf = node->dmabuf;

			seq_printf(s, "%-12s\t%-10ld\t%-8lu\n", dmabuf->exp_name, dmabuf->size >> 10,
				   file_inode(dmabuf->file)->i_ino);
		}
	}
	ret = 0;
out:
	list_for_each_entry_safe(proc, tmp, &proc_list, node) {
		list_del(&proc->node);
		free_proc(proc);
	}
	return ret;
}

static int get_dma_info(const void *data, struct file *file, unsigned int n)
{
	struct priv_buf *buf;
	struct dma_buf_priv *dma_buf_priv;
	struct dma_buf *dmabuf;
	struct task_struct *task;
	int ret;
	u32 index;

	if (!qcom_is_dma_buf_file(file))
		return 0;

	dma_buf_priv = (struct dma_buf_priv *)data;
	buf = dma_buf_priv->priv_buf;
	task = dma_buf_priv->task;
	if (dma_buf_priv->count == 0) {
		ret = scnprintf(buf->buf + buf->offset, buf->size - buf->offset,
				"\n%s (PID %d)\nDMA Buffers:\n", task->comm, task->tgid);
		buf->offset += ret;
		if (buf->offset == buf->size - 1)
			return -EINVAL;
	}
	dmabuf = (struct dma_buf *)file->private_data;
	index = jhash(dmabuf, sizeof(struct dma_buf), DMA_BUF_HASH_SEED);
	index = index & (DMA_BUF_HASH_SIZE - 1);
	if (dma_buf_hash[index])
		return 0;
	dma_buf_hash[index] = true;
	if (dma_buf_priv->count++ == 0) {
		ret = scnprintf(buf->buf + buf->offset, buf->size - buf->offset,
				"%-8s\t%-8s\t%-8s\t%-8s\t%-12s\t%-4s\t%-8s\n",
				"size", "flags", "mode", "count", "exp_name", "fd", "ino");
		buf->offset += ret;
		if (buf->offset == buf->size - 1)
			return -EINVAL;
	}
	ret = scnprintf(buf->buf + buf->offset, buf->size - buf->offset,
			"%08zu\t%08x\t%08x\t%08ld\t%-12s\t%-4u\t%-8lu\t%s\n",
			dmabuf->size, dmabuf->file->f_flags, dmabuf->file->f_mode,
			file_count(dmabuf->file), dmabuf->exp_name, n,
			file_inode(dmabuf->file)->i_ino, dmabuf->name ?: "");
	buf->offset += ret;
	if (buf->offset == buf->size - 1)
		return -EINVAL;
	dma_buf_priv->size += dmabuf->size;
	return 0;
}

void get_dma_buf_procs(char *m, size_t dump_size)
{
	struct task_struct *task, *thread;
	struct files_struct *files;
	int ret = 0;
	struct priv_buf buf;
	struct dma_buf_priv dma_buf_priv;

	buf.buf = m;
	buf.size = dump_size;
	buf.offset = 0;
	dma_buf_priv.priv_buf = &buf;
	dma_buf_priv.task = NULL;
	dma_buf_priv.count = 0;
	dma_buf_priv.size = 0;

	rcu_read_lock();
	for_each_process(task) {
		struct files_struct *group_leader_files = NULL;

		dma_buf_priv.task = task;
		for_each_thread(task, thread) {
			task_lock(thread);
			if (unlikely(!group_leader_files))
				group_leader_files = task->group_leader->files;
			files = thread->files;
			if (files && (group_leader_files != files || thread == task->group_leader))
				ret = iterate_fd(files, 0, get_dma_info, &dma_buf_priv);
			task_unlock(thread);
			if (ret)
				goto err;
		}
		if (dma_buf_priv.count) {
			ret = scnprintf(buf.buf + buf.offset, buf.size - buf.offset,
					"Total %d objects, %zu bytes\n", dma_buf_priv.count, dma_buf_priv.size);
			buf.offset += ret;
			if (buf.offset == buf.size - 1)
				goto err;
			dma_buf_priv.count = 0;
			dma_buf_priv.size = 0;
			memset(dma_buf_hash, 0, sizeof(dma_buf_hash));
		}
	}
	rcu_read_unlock();
	return;
err:
	rcu_read_unlock();
	pr_err("dma buf procs info pre-alloc memory region exhausted!\n");
}

static ssize_t dma_buf_procs_read(struct file *file, char __user *ubuf, size_t count, loff_t *ppos)
{
	if (dma_buf_procs_addr)
		memset(dma_buf_procs_addr, 0, dma_buf_procs_size);
	get_dma_buf_procs(dma_buf_procs_addr, dma_buf_procs_size);
	return simple_read_from_buffer(ubuf, count, ppos, dma_buf_procs_addr, dma_buf_procs_size);
}

static const struct proc_ops dma_buf_procs_proc_ops = {
	.proc_read = dma_buf_procs_read,
	.proc_lseek = default_llseek,
};

int init_dma_buf_debug(struct proc_dir_entry *parent)
{
	struct proc_dir_entry *entry;

	entry_dma_buf = proc_mkdir("dma_buf", parent);
	if (!entry_dma_buf) {
		pr_err("%s: Can't create dma_buf proc entry\n", __func__);
		return -ENOMEM;
	}
	entry = proc_create("dma_buf_procs", 0100444, entry_dma_buf, &dma_buf_procs_proc_ops);
	if (!entry) {
#line 288
		pr_err("%s: %d: Can't create dma_buf_procs proc entry!\n", __func__, __LINE__);
		return -ENOMEM;
	}
	proc_set_size(entry, dma_buf_procs_size);
	dma_buf_procs_addr = kzalloc(dma_buf_procs_size, GFP_KERNEL);
	if (!dma_buf_procs_addr) {
#line 296
		pr_err("%s: %d: Can't alloc memory for dma_buf_procs_addr!\n", __func__, __LINE__);
		return -ENOMEM;
	}
	proc_create_single_data("dma_buf_procs_sort", 0, entry_dma_buf, dma_buf_procs_sort_show, NULL);
	return 0;
}

void dma_buf_debug_clean(struct proc_dir_entry *parent)
{
	remove_proc_entry("dma_buf_procs_sort", entry_dma_buf);
	remove_proc_entry("dma_buf_procs", entry_dma_buf);
	remove_proc_entry("dma_buf", parent);
	if (dma_buf_procs_addr)
		kfree(dma_buf_procs_addr);
}

/* ---- memory statistics ---- */

static unsigned int initrd_size;
module_param(initrd_size, uint, 0444);
MODULE_PARM_DESC(initrd_size, "Ramdisk size (kb)");

unsigned long estimate_ddr_size(unsigned long total_kb)
{
	unsigned int gb = total_kb >> 20;

	if (gb < 6)
		return 0x600000;
	if (gb < 8)
		return 0x800000;
	if (gb < 10)
		return 0xa00000;
	if (gb < 12)
		return 0xc00000;
	if (gb < 14)
		return 0xe00000;
	return 0x1000000;
}

int reserved_memory_stat_show(struct seq_file *m, void *v)
{
	struct device_node *np = of_find_all_nodes(NULL);
	struct reserved_mem *rmem;
	unsigned long total_kb = totalram_pages() << (PAGE_SHIFT - 10);
	int count = 0;
	long nomap_size = 0;
	long cma_size = 0;
	long other_size = 0;
	long total_size = 0;

	seq_printf(m, "[ReservedMemoryTotal] size %ld KB\n\n", estimate_ddr_size(total_kb) - total_kb);

	for (; np; np = of_find_all_nodes(np)) {
		const char *type = "fdt_reserved_other";
		bool reusable = false;
		struct property *nomap;
		unsigned long kb;

		rmem = of_reserved_mem_lookup(np);
		if (!rmem || !rmem->size)
			continue;
		nomap = of_find_property(np, "no-map", NULL);
		if (of_device_is_compatible(np, "shared-dma-pool")) {
			reusable = of_find_property(np, "reusable", NULL) != NULL;
			if (reusable)
				type = "fdt_reserved_cma";
		}
		if (nomap)
			type = "fdt_no-map";
		seq_printf(m, "[%s] %s: addr 0x%llx, size %lld KB\n", type, rmem->name, rmem->base, rmem->size >> 10);
		kb = rmem->size >> 10;
		if (nomap)
			nomap_size += kb;
		else if (reusable)
			cma_size += kb;
		else
			other_size += kb;
		count++;
		total_size += kb;
	}
	seq_printf(m, "[MemblockReserved] initrd: size %u KB\n", initrd_size);
	seq_printf(m, "\nSummary:\nfdt total count %d, total size %ld KB:\nfdt_no-map total size %ld KB\n"
		   "fdt_reserved_cma total size %ld KB\nfdt_reserved_other total size %ld KB\n",
		   count, total_size, nomap_size, cma_size, other_size);
	return 0;
}

void debug_for_each_module(int (*fn)(const char *name, void *data), void *data)
{
	struct list_head *modules;
	struct module *mod;

	modules = (struct list_head *)debug_symbol_lookup_name("modules");
	if (IS_ERR_OR_NULL(modules))
		return;

	preempt_disable();
	list_for_each_entry_rcu(mod, modules, list) {
		if (fn(mod->name, data))
			break;
	}
	preempt_enable();
}

struct ko_mem_info {
	const char *name;
	unsigned int total;
	unsigned int text;
	unsigned int data;
	unsigned int ro;
	unsigned int ro_after_init;
	unsigned int init_text;
	unsigned int init_data;
	unsigned int init_ro;
	struct list_head list;
};

static int sizecmp(void *priv, const struct list_head *a, const struct list_head *b)
{
	struct ko_mem_info *ka = container_of(a, struct ko_mem_info, list);
	struct ko_mem_info *kb = container_of(b, struct ko_mem_info, list);

	return kb->total - ka->total;
}

int ko_memory_stat_show(struct seq_file *m, void *v)
{
	struct list_head *modules;
	struct module *mod;
	struct ko_mem_info *ko, *tmp;
	unsigned int total = 0;
	unsigned int init_data, init_ro;
	LIST_HEAD(ko_list);

	modules = (struct list_head *)debug_symbol_lookup_name("modules");
	if (!IS_ERR_OR_NULL(modules)) {
		preempt_disable();
		list_for_each_entry_rcu(mod, modules, list) {
			ko = kmalloc(sizeof(*ko), GFP_ATOMIC);
			if (!ko)
				break;
			ko->name = mod->name;
			ko->text = mod->mem[MOD_TEXT].size >> 10;
			ko->data = mod->mem[MOD_DATA].size >> 10;
			ko->ro = mod->mem[MOD_RODATA].size >> 10;
			ko->ro_after_init = mod->mem[MOD_RO_AFTER_INIT].size >> 10;
			ko->init_text = mod->mem[MOD_INIT_TEXT].size >> 10;
			init_data = mod->mem[MOD_INIT_DATA].size;
			ko->init_data = init_data >> 10;
			init_ro = mod->mem[MOD_INIT_RODATA].size;
			ko->init_ro = init_ro >> 10;
			ko->total = init_data + init_ro;
			list_add(&ko->list, &ko_list);
		}
		preempt_enable();
	}

	list_sort(NULL, &ko_list, sizecmp);
	list_for_each_entry(ko, &ko_list, list)
		total += ko->total;
	seq_printf(m, "[KernelModuleTotal] size %u KB\n\n", total);
	list_for_each_entry_safe(ko, tmp, &ko_list, list) {
		seq_printf(m, "[KernelModule] %s: text_size %u KB | data_size %u KB, ro_data_size %u KB, "
			   "ro_after_init_size %u KB, init_text_size %u KB, init_data_size %u KB, "
			   "init_ro_data_size %u KB\n",
			   ko->name, ko->text, ko->data, ko->ro, ko->ro_after_init, ko->init_text,
			   ko->init_data, ko->init_ro);
		kfree(ko);
	}
	return 0;
}

int mem_statis_init(struct proc_dir_entry *parent)
{
	entry_mem_statis = proc_mkdir("mem_statis", parent);
	if (!entry_mem_statis) {
		pr_err("%s: Can't create mem statistics entry\n", __func__);
		return -ENOMEM;
	}
	proc_create_single_data("reserved_memory_stat", 0, entry_mem_statis, reserved_memory_stat_show, NULL);
	proc_create_single_data("ko_memory_stat", 0, entry_mem_statis, ko_memory_stat_show, NULL);
	return 0;
}

void mem_statis_exit(struct proc_dir_entry *parent)
{
	if (entry_mem_statis)
		remove_proc_entry("reserved_memory_stat", entry_mem_statis);
	if (parent)
		remove_proc_entry("mem_statis", parent);
}

/* ---- panic trigger ---- */

static int reboot_flag;
static int shutdown_flag;

static ssize_t reboot_panic_procs_read(struct file *file, char __user *ubuf, size_t count, loff_t *ppos)
{
	char buf[16] = { 0 };
	int ret;

	sprintf(buf, "%d\n", reboot_flag);
	ret = simple_read_from_buffer(ubuf, count, ppos, buf, strlen(buf));
	return ret;
}

static ssize_t reboot_panic_procs_write(struct file *file, const char __user *ubuf, size_t count, loff_t *ppos)
{
	int val = 0;
	char buf[16] = { 0 };

	if (copy_from_user(buf, ubuf, count))
		return -EFAULT;
	sscanf(buf, "%d", &val);
	reboot_flag = val > 0;
	return count;
}

static ssize_t shutdown_panic_procs_read(struct file *file, char __user *ubuf, size_t count, loff_t *ppos)
{
	char buf[16] = { 0 };
	int ret;

	sprintf(buf, "%d\n", shutdown_flag);
	ret = simple_read_from_buffer(ubuf, count, ppos, buf, strlen(buf));
	return ret;
}

static ssize_t shutdown_panic_procs_write(struct file *file, const char __user *ubuf, size_t count, loff_t *ppos)
{
	int val = 0;
	char buf[16] = { 0 };

	if (copy_from_user(buf, ubuf, count))
		return -EFAULT;
	sscanf(buf, "%d", &val);
	shutdown_flag = val > 0;
	return count;
}

static ssize_t customized_panic_procs_write(struct file *file, const char __user *ubuf, size_t count, loff_t *ppos)
{
	char buf[16] = { 0 };
	size_t len = min_t(size_t, count, sizeof(buf));

	if (copy_from_user(buf, ubuf, len))
		return -EFAULT;
	buf[len - 1] = '\0';
	pr_err("panic_trigger: %s\n", buf);
	panic(buf);
}

static const struct proc_ops reboot_panic_ops = {
	.proc_read = reboot_panic_procs_read,
	.proc_write = reboot_panic_procs_write,
	.proc_lseek = default_llseek,
};

static const struct proc_ops shutdown_panic_ops = {
	.proc_read = shutdown_panic_procs_read,
	.proc_write = shutdown_panic_procs_write,
	.proc_lseek = default_llseek,
};

static const struct proc_ops customized_panic_ops = {
	.proc_write = customized_panic_procs_write,
};

static int panic_trigger_notify(struct notifier_block *nb, unsigned long action, void *data)
{
	if (action == SYS_RESTART && reboot_flag)
		panic("Panic trigger come from SYS_RESTART\n");
	if (action == SYS_POWER_OFF && shutdown_flag)
		panic("Panic trigger come from SYS_POWER_OFF\n");
	return NOTIFY_DONE;
}

static struct notifier_block panic_trigger_nb = {
	.notifier_call = panic_trigger_notify,
};

int panic_trigger_init(struct proc_dir_entry *parent)
{
	entry_set_panic = proc_mkdir("panic_trigger", parent);
	if (!entry_set_panic) {
		pr_err("panic_trigger: %s: Can't create panic_trigger proc entry\n", __func__);
		return -ENOMEM;
	}
	if (!proc_create("panic_on_reboot", 0x666, entry_set_panic, &reboot_panic_ops)) {
#line 132
		pr_err("panic_trigger: %s: %d: Can't create panic_on_reboot proc entry!\n", __func__, __LINE__);
		goto remove_dir;
	}
	if (!proc_create("panic_on_shutdown", 0x666, entry_set_panic, &shutdown_panic_ops)) {
#line 140
		pr_err("panic_trigger: %s: %d: Can't create panic_on_shutdown proc entry!\n", __func__, __LINE__);
		goto remove_reboot;
	}
	if (!proc_create("panic", 0222, entry_set_panic, &customized_panic_ops)) {
#line 148
		pr_err("panic_trigger: %s: %d: Can't create panic proc entry!\n", __func__, __LINE__);
		goto remove_shutdown;
	}
	if (register_reboot_notifier(&panic_trigger_nb)) {
		pr_err("panic_trigger: Error: Can't register reboot notifier\n");
		remove_proc_entry("panic", entry_set_panic);
		goto remove_shutdown;
	}
	return 0;
remove_shutdown:
	remove_proc_entry("panic_on_shutdown", entry_set_panic);
remove_reboot:
	remove_proc_entry("panic_on_reboot", entry_set_panic);
remove_dir:
	remove_proc_entry("panic_trigger", parent);
	return -ENOMEM;
}

void panic_trigger_exit(struct proc_dir_entry *parent)
{
	remove_proc_entry("panic", entry_set_panic);
	remove_proc_entry("panic_on_shutdown", entry_set_panic);
	remove_proc_entry("panic_on_reboot", entry_set_panic);
	remove_proc_entry("panic_trigger", parent);
	unregister_reboot_notifier(&panic_trigger_nb);
}

/* ---- module ---- */

static int __init debug_ext_init(void)
{
	int ret;

	entry_debug_ext = proc_mkdir("debug_ext", NULL);
	if (!entry_debug_ext) {
		pr_err("%s: Can't create debug_ext proc entry\n", __func__);
		return -ENOMEM;
	}
	ret = init_dma_buf_debug(entry_debug_ext);
	if (ret) {
#line 32
		pr_err("%s: %d: init dma buf debug node failed!\n", __func__, __LINE__);
		return ret;
	}
	ret = mem_statis_init(entry_debug_ext);
	if (ret) {
#line 46
		pr_err("%s: %d: mem statistics init failed!\n", __func__, __LINE__);
		return ret;
	}
	ret = panic_trigger_init(entry_debug_ext);
	if (ret) {
#line 53
		pr_err("%s: %d: set_panic_init_notifier failed!\n", __func__, __LINE__);
		return ret;
	}
	pr_info("%s: init done!", __func__);
	return ret;
}

static void __exit debug_ext_exit(void)
{
	dma_buf_debug_clean(entry_debug_ext);
	mem_statis_exit(entry_debug_ext);
	panic_trigger_exit(entry_debug_ext);
	remove_proc_entry("debug_ext", NULL);
	entry_debug_ext = NULL;
}

module_init(debug_ext_init);
module_exit(debug_ext_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Xiaomi Kernel Debug Extensions");
MODULE_AUTHOR("chenxinyuanchen@xiaomi.com");
