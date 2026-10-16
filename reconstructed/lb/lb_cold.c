// SPDX-License-Identifier: GPL-2.0
/* Reconstructed from the stock lb.ko by reverse engineering; not original source. (cold start record/preread) */
#include "lb.h"

struct lb_manager *g_manager;
atomic_t collecting_;

static struct {
	struct package_info *package;
	unsigned int nr_pids;
	pid_t pids[5];
} g_active_package;

static inline bool lb_pid_collecting(pid_t pid)
{
	unsigned int i, nr = g_active_package.nr_pids;

	for (i = 0; i < nr; i++)
		if (g_active_package.pids[i] == pid)
			break;
	return i < nr;
}

static noinline void mi_lb_pagecache_collect(struct file *file, unsigned long index, unsigned int nr_pages, bool is_fault)
{
	struct lb_manager *mgr;
	unsigned int nr;
	unsigned int n;
	struct file *cached;
	unsigned long magic;

	if (g_atrace_enable && debug_owner.uid && current_uid().val == debug_owner.uid) {
		const char *path = file_path(file, g_manager->path_buf, 4096);

		LB_D(74, "collect file %s", path);
	}

	if (!g_active_package.package)
		return;

	if (!lb_pid_collecting(current->pid))
		return;

	mgr = g_manager;
	if (time_after(jiffies, mgr->last_jiffies + 500))
		return;

	if (!file->f_inode->i_sb)
		return;
	magic = file->f_inode->i_sb->s_magic;
	if (magic != 0xef53 && magic != 0xf2f52010 && magic != 0xe0f5e1e2)
		return;

	spin_lock(&g_manager->lock);
	nr = g_manager->nr_files;
	cached = NULL;
	for (n = 0; n < nr; n++) {
		if (file->f_inode->i_sb->s_dev == g_manager->files[n].dev && file->f_inode->i_ino == g_manager->files[n].ino) {
			cached = g_manager->files[n].file;
			break;
		}
	}
	if (n < 2048 && n == nr) {
		cached = file;
		get_file(file);
		g_manager->files[n].file = file;
		g_manager->files[n].dev = file->f_inode->i_sb->s_dev;
		g_manager->files[n].ino = file->f_inode->i_ino;
		g_manager->nr_files++;
	}
	if (n == 2048)
		goto out;
	if (g_manager->nr_records < 0x20000) {
		struct lb_record *rec = &g_manager->records[g_manager->nr_records];

		rec->file = cached;
		rec->index = index;
		rec->nr = nr_pages;
		g_manager->nr_records++;
	}
out:
	spin_unlock(&g_manager->lock);
}

void mi_lb_page_collect_on_readfile(void *data, struct file *file, loff_t pos, size_t size)
{
	if (size)
		mi_lb_pagecache_collect(file, pos >> PAGE_SHIFT, (size + 0xfff) >> 12, false);
}

void mi_lb_page_collect_on_pagefault(void *data, struct file *file, pgoff_t orig_start_pgoff, pgoff_t last_pgoff, vm_fault_t ret)
{
	if (last_pgoff != orig_start_pgoff)
		mi_lb_pagecache_collect(file, orig_start_pgoff, last_pgoff - orig_start_pgoff, true);
}

/* put every file reference taken by the collect hooks and reset the buffers */
static inline void lb_files_clear(void)
{
	unsigned int i;

	for (i = 0; i < g_manager->nr_files; i++) {
		if (g_manager->files[i].file) {
			fput(g_manager->files[i].file);
			g_manager->files[i].file = NULL;
		}
	}
	g_manager->nr_files = 0;
	g_manager->nr_records = 0;
}

static inline int mb_lb_dealwith_collect_start(struct lb_owner *owner)
{
	struct package_info *package;

	if (!atomic_add_unless(&collecting_, 1, 1)) {
		if (g_active_package.package &&
		    !strcmp(g_active_package.package->owner.package_name, owner->package_name) &&
		    g_active_package.nr_pids < 5) {
			int *slot = &g_active_package.pids[g_active_package.nr_pids];

			*slot = owner->pid;
			g_active_package.nr_pids++;
		}
		return 0;
	}

	lb_files_clear();
	package = mi_lb_get_package_info(owner, true);
	if (!package) {
		if (g_manager->app_nums >= 100) {
			struct package_info *oldest = mi_lb_get_oldest_package_info();

			if (oldest)
				package_info_put(oldest);
			else
				LB_E(328, "no old package to free");
		}
		package = kzalloc(sizeof(*package), GFP_KERNEL);
		if (!package) {
			LB_E(335, "alloc package info fail");
			return 0;
		}
		INIT_LIST_HEAD(&package->file_infos);
		mutex_init(&package->package_lock);
		package->file_num = 0;
		package->mm_size = sizeof(*package);
		package->is_cleared = false;
		atomic_inc(&package->refcount);
	}
	memcpy(&package->owner, owner, sizeof(*owner));
	package->total_startup_bytes = 0;
	package->app_startup_bytes = 0;
	g_active_package.package = package;
	unsigned long now = jiffies;
	struct delayed_work *dwork;

	g_manager->last_jiffies = now;
	dwork = &g_manager->compiler_work;
	queue_delayed_work(system_wq, dwork, 500);
	if (g_active_package.package && g_active_package.nr_pids < 5) {
		int *slot = &g_active_package.pids[g_active_package.nr_pids];

		*slot = owner->pid;
		g_active_package.nr_pids++;
	}
	return 0;
}

static inline int mb_lb_dealwith_collect_finish(struct lb_owner *owner)
{
	struct package_info *package;

	package = g_active_package.package;
	if (!package) {
		LB_E(212, "active package is NULL");
		return 0;
	}
	if (package->owner.uid == owner->uid && !strcmp(package->owner.package_name, owner->package_name)) {
		cancel_delayed_work_sync(&g_manager->compiler_work);
		wake_up_all(&g_manager->compiler_wait);
	} else {
		LB_E(222, "finish collecting app[%s] != active app[%s]", package->owner.package_name, owner->package_name);
	}
	return 0;
}

static inline int mb_lb_dealwith_collect_abort(struct lb_owner *owner)
{
	struct package_info *package;

	package = g_active_package.package;
	if (!package) {
		LB_E(233, "active package is NULL");
		return 0;
	}
	memset(&g_active_package, 0, sizeof(g_active_package));
	cancel_delayed_work_sync(&g_manager->compiler_work);
	if (package->owner.uid != owner->uid)
		return 0;
	if (strncmp(package->owner.package_name, owner->package_name, strlen(owner->package_name)))
		return 0;
	lb_files_clear();
	if (mi_lb_is_in_package_list(package)) {
		package_info_put(package);
	} else {
		LB_D(245, "active package %s not in package list", package->owner.package_name);
		g_manager->total_memory -= package->mm_size;
		kfree(package);
	}
	atomic_set(&collecting_, 0);
	return 0;
}

noinline int mi_lb_dealwith_collect_cmd(enum APP_LAUNCH_CMD cmd, struct lb_owner *owner)
{
	switch (cmd) {
	case APP_COLLECT_START:
		mb_lb_dealwith_collect_start(owner);
		break;
	case APP_LAUNCH_FINISH:
		if (!atomic_read(&collecting_)) {
			LB_D(378, "finish: the app[%s] is not collecting", owner->package_name);
			break;
		}
		mb_lb_dealwith_collect_finish(owner);
		break;
	case APP_LAUNCH_ABORT:
		if (!atomic_read(&collecting_)) {
			LB_D(385, "abort: the app[%s] is not collecting", owner->package_name);
			break;
		}
		mb_lb_dealwith_collect_abort(owner);
		break;
	default:
		LB_E(389, "unknown cold start cmd is %u", cmd);
		break;
	}
	return 0;
}

static void mi_lb_do_compile(struct work_struct *work)
{
	wake_up_all(&g_manager->compiler_wait);
}

static int mi_lb_compiler_thread(void *data);

noinline int mi_lb_init_manager(void)
{
	struct task_struct *t;
	struct lb_manager *m;
	struct lb_record *records;
	int ret;

	g_manager = kzalloc(sizeof(*g_manager), GFP_KERNEL);
	if (!g_manager)
		return -ENOMEM;
	records = kvmalloc(0x300000, GFP_KERNEL);
	m = g_manager;
	m->records = records;
	if (!records) {
		ret = -ENOMEM;
		goto free_mgr;
	}
	spin_lock_init(&m->lock);
	m->total_memory = sizeof(*g_manager) + 0x300000;
	mutex_init(&g_manager->compiler_lock);
	mutex_init(&g_manager->package_list_lock);
	INIT_LIST_HEAD(&g_manager->package_list);
	INIT_DELAYED_WORK(&g_manager->compiler_work, mi_lb_do_compile);
	g_manager->nr_records = 0;
	g_manager->nr_files = 0;
	init_waitqueue_head(&g_manager->compiler_wait);
	t = kthread_create(mi_lb_compiler_thread, g_manager, "lb:g_manager");
	if (IS_ERR(t)) {
		ret = PTR_ERR(t);
		g_manager->compiler_thread = t;
		LB_E(652, "create compiler_thread fail, ret: %d", ret);
		kvfree(g_manager->records);
		goto free_mgr;
	}
	wake_up_process(t);
	g_manager->compiler_thread = t;
	memset(&g_active_package, 0, sizeof(g_active_package));
	return 0;
free_mgr:
	if (g_manager)
		kfree(g_manager);
	return ret;
}

static const char * const file_type_list[] = { "so", "dex", "vdex", "odex", "art", "apk", "jar", NULL };

static inline int launch_reset_fileinfo(struct file_info *info, struct file *file)
{
	loff_t size;

	if (info->bitmaps)
		kfree(info->bitmaps);
	info->collect_num = 0;
	info->dev = file->f_inode->i_sb->s_dev;
	info->ino = file->f_inode->i_ino;
	info->mtime = file->f_inode->i_mtime;
	info->ref = 3;
	size = min_t(loff_t, i_size_read(file->f_inode), 1UL << 31);
	info->nr_pages = (size + 0xfff) >> 12;
	info->bitmap_longs = BITS_TO_LONGS((int)((size + 0xfff) >> 12));
	info->bitmaps = bitmap_zalloc(info->nr_pages, GFP_KERNEL);
	if (!info->bitmaps) {
		LB_E(442, "alloc file_info bitmaps err");
		if (info->path)
			kfree(info->path);
		list_del(&info->list);
		if (info)
			kfree(info);
		return -ENOMEM;
	}
	return 0;
}

static inline int mi_lb_get_fileinfo(struct package_info *package, struct file *file, struct file_info **pinfo)
{
	struct file_info *info;
	struct list_head *head = &package->file_infos;
	struct inode *inode;
	char *path;
	size_t len;
	int plen;
	loff_t size;
	int i;

	path = file_path(file, g_manager->path_buf, 4096);
	if (IS_ERR(path)) {
		LB_E(466, "find filepath error\n");
		return -1;
	}
	if (!list_empty(head)) {
		list_for_each_entry(info, head, list) {
			if (info->dev == file->f_inode->i_sb->s_dev && !strcmp(info->path, path)) {
				if (!info->is_moved) {
					list_move(&info->list, head);
					info->is_moved = true;
				}
				info->is_collected = true;
				inode = file->f_inode;
				if (info->ino != inode->i_ino || inode->i_mtime.tv_sec != info->mtime.tv_sec) {
					if (launch_reset_fileinfo(info, file))
						return -1;
				}
				*pinfo = info;
				return 0;
			}
		}
	}

	if (!g_collect_pinfile) {
		if (!strncmp(path, "/system", 7))
			return -1;
		if (!strncmp(path, "/system_ext", 11))
			return -1;
	}
	if (g_collect_specify_filetype) {
		const char *name;
		bool match = false;
		const char * const *ext;

		if (!file->f_path.dentry)
			return -1;
		name = file->f_path.dentry->d_name.name;
		if (!name)
			return -1;
		len = strlen(name);
		for (ext = file_type_list; *ext; ext++) {
			size_t elen = strlen(*ext);

			if (len < elen + 2)
				continue;
			for (i = 1; i < len - elen; i++) {
				if (name[i] == '.' && !strncasecmp(name + i + 1, *ext, elen)) {
					match = true;
					break;
				}
			}
			if (match)
				break;
		}
		if (!match)
			return -1;
	}

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info) {
		LB_E(489, "alloc file_info err");
		return -1;
	}
	info->collect_num = 0;
	info->dev = file->f_inode->i_sb->s_dev;
	info->ino = file->f_inode->i_ino;
	info->mtime = file->f_inode->i_mtime;
	info->is_moved = true;
	info->is_collected = true;
	info->ref = 3;
	size = min_t(loff_t, i_size_read(file->f_inode), 1UL << 31);
	info->nr_pages = (size + 0xfff) >> 12;
	info->bitmap_longs = BITS_TO_LONGS((int)((size + 0xfff) >> 12));
	info->bitmaps = bitmap_zalloc(info->nr_pages, GFP_KERNEL);
	if (!info->bitmaps) {
		LB_E(505, "alloc file_info bitmaps err");
		goto free_info;
	}
	plen = strlen(path);
	info->path = kzalloc(plen + 1, GFP_KERNEL);
	if (!info->path) {
		LB_E(512, "info->path alloc fail\n");
		bitmap_free(info->bitmaps);
		goto free_info;
	}
	strncpy(info->path, path, plen + 1);
	list_add(&info->list, head);
	package->file_num++;
	{
		unsigned long sz = plen + 1 + info->bitmap_longs * sizeof(unsigned long) + sizeof(*info);

		info->mm_size = sz;
		package->mm_size += sz;
		g_manager->total_memory += info->mm_size;
	}
	*pinfo = info;
	return 0;
free_info:
	kfree(info);
	return -1;
}

static inline void mi_lb_record_buf_clear(void)
{
	unsigned int i;

	for (i = 0; i < g_manager->nr_files; i++) {
		if (g_atrace_enable) {
			if (g_manager->files[i].file) {
				const char *path = file_path(g_manager->files[i].file, g_manager->path_buf, 4096);

				LB_D(194, "collected file: %s", path);
			}
		}
		if (g_manager->files[i].file) {
			fput(g_manager->files[i].file);
			g_manager->files[i].file = NULL;
		}
	}
	g_manager->nr_files = 0;
	g_manager->nr_records = 0;
}

void free_one_fileinfo(struct package_info *package, struct file_info *file_entry);

static inline bool mi_lb_file_is_collected(struct file_info *info)
{
	unsigned int k;

	for (k = 0; k < g_manager->nr_files; k++) {
		if (g_manager->files[k].dev == info->dev && g_manager->files[k].ino == info->ino)
			break;
	}
	return k < g_manager->nr_files;
}

static inline void mi_lb_compiler_do_work(void)
{
	struct package_info *package;
	struct file_info *info, *next;
	struct lb_record *rec;
	int i;

	package = g_active_package.package;
	if (!package) {
		LB_E(572, "package is null");
		return;
	}
	mutex_lock(&g_manager->compiler_lock);
	memset(&g_active_package, 0, sizeof(g_active_package));
	list_for_each_entry_safe(info, next, &package->file_infos, list)
		info->is_collected = false;

	for (i = 0; i < g_manager->nr_records; i++) {
		struct file *file = g_manager->records[i].file;
		unsigned long index;
		unsigned int nr;

		rec = &g_manager->records[i];
		if (mi_lb_get_fileinfo(package, file, &info))
			continue;
		index = rec->index;
		nr = rec->nr;
		if (index + nr > info->nr_pages)
			continue;
		if (g_debug_verbose) {
			unsigned int j = index, end = j + nr;

			for (; j < end; j++)
				if (!test_bit(j, info->bitmaps))
					package->app_startup_bytes += PAGE_SIZE;
		}
		bitmap_set(info->bitmaps, index, nr);
	}

	mi_lb_add_package_info(package);

	list_for_each_entry_safe(info, next, &package->file_infos, list) {
		if (g_debug_verbose)
			package->total_startup_bytes += bitmap_weight(info->bitmaps, info->nr_pages) << 12;
		info->is_moved = false;
		if (mi_lb_file_is_collected(info)) {
			info->ref = 3;
		} else if (--info->ref == 0) {
			free_one_fileinfo(package, info);
		}
	}

	mi_lb_record_buf_clear();
	package_info_put(package);
	atomic_set(&collecting_, 0);
	mutex_unlock(&g_manager->compiler_lock);

	if (g_debug_verbose) {
		if (g_lb_msg_level <= 2) {
			package->hit_rate = 100 - package->app_startup_bytes * 100ULL / package->total_startup_bytes;
		} else {
			LB_D(589, "compiler finish: total_memory %lu apps %u", g_manager->total_memory, g_manager->app_nums);
			package->hit_rate = 100 - package->app_startup_bytes * 100ULL / package->total_startup_bytes;
			LB_D(594, "current app %s, This app load io: %u bytes, total io: %u bytes, hit_rate: %d%%",
			     package->owner.package_name, package->app_startup_bytes, package->total_startup_bytes, package->hit_rate);
		}
	}
}

static int mi_lb_compiler_thread(void *data)
{
	struct lb_manager *mgr = data;

	if (!data) {
		LB_E(603, "data is null");
		return -EINVAL;
	}
	LB_D(607, "collecting_: %d", atomic_read(&collecting_));
	while (!kthread_should_stop()) {
		DEFINE_WAIT(wait);

		prepare_to_wait(&mgr->compiler_wait, &wait, TASK_UNINTERRUPTIBLE);
		schedule();
		finish_wait(&mgr->compiler_wait, &wait);
		if (!atomic_read(&collecting_))
			continue;
		mi_lb_compiler_do_work();
	}
	return 0;
}

noinline int mi_lb_exit_manager(void)
{
	lb_files_clear();
	if (g_manager) {
		kthread_stop(g_manager->compiler_thread);
		wake_up(&g_manager->compiler_wait);
		if (g_manager->records)
			kvfree(g_manager->records);
		if (g_manager)
			kfree(g_manager);
	}
	return 0;
}

noinline int mi_lb_dealwith_preread_size_cmd(struct lb_app_preread_info_size *info_size)
{
	struct package_info *package;
	struct file_info *f;
	size_t size;
	int ret;

	mutex_lock(&g_manager->compiler_lock);
	package = mi_lb_get_package_info(&info_size->owner, false);
	if (!package) {
		LB_D(95, "app first cold start, No pre-reading");
		ret = -1;
		goto out;
	}
	if (list_empty(&package->file_infos))
		goto no_files;
	size = sizeof(struct lb_app_preread_info);
	list_for_each_entry(f, &package->file_infos, list) {
		if (f->is_collected)
			size += f->bitmap_longs * sizeof(unsigned long) + sizeof(struct lb_app_preread_file_info);
	}
	if (!size)
		goto no_files;
	info_size->info_size = size + 0x110;
	package_info_put(package);
	ret = 0;
	goto out;
no_files:
	LB_D(102, "app has collected, but no pre-reading files");
	ret = -2;
out:
	mutex_unlock(&g_manager->compiler_lock);
	return ret;
}

static inline int get_app_pre_filesinfo(struct package_info *package, struct lb_app_preread_info *info)
{
	struct file_info *f;
	struct lb_app_preread_file_info *pf;
	size_t size = sizeof(*info), next_size, bytes;
	size_t len;
	unsigned int n;
	const char *name;

	info->file_num = 0;
	list_for_each_entry_reverse(f, &package->file_infos, list) {
		if (!f->is_collected)
			continue;
		bytes = (size_t)f->bitmap_longs << 3;
		next_size = size + bytes + sizeof(*pf);
		if (next_size > info->file_info_buf_size) {
			LB_E(38, "Exceeded preread_info memory size %zu", info->file_info_buf_size);
			break;
		}
		pf = (struct lb_app_preread_file_info *)((char *)info + size);
		pf->mtime = f->mtime;
		pf->ino = f->ino;
		pf->nr_pages = f->nr_pages;
		name = f->path;
		len = strlen(name);
		n = len;
		if (n > 255) {
			LB_E(53, "file path is too long %s", name);
			pf->path[0] = '\0';
		} else {
			char *dst = strncpy(pf->path, name, n);

			dst[n] = '\0';
		}
		pf->bitmap_longs = f->bitmap_longs;
		memcpy(pf->bitmaps, f->bitmaps, bytes);
		info->file_num++;
		size = next_size;
	}
	return 0;
}

noinline int mi_lb_dealwith_preread_info_cmd(struct lb_app_preread_info *info)
{
	struct package_info *package;

	mutex_lock(&g_manager->compiler_lock);
	package = mi_lb_get_package_info(&info->owner, false);
	if (!package) {
		mutex_unlock(&g_manager->compiler_lock);
		LB_D(124, "app first cold start, No pre-reading");
		return -1;
	}
	get_app_pre_filesinfo(package, info);
	package_info_put(package);
	mutex_unlock(&g_manager->compiler_lock);
	return 0;
}

noinline void free_one_fileinfo(struct package_info *package, struct file_info *file_entry)
{
	list_del(&file_entry->list);
	package->file_num--;
	package->mm_size -= file_entry->mm_size;
	g_manager->total_memory -= file_entry->mm_size;
	if (file_entry->path)
		kfree(file_entry->path);
	if (file_entry->bitmaps)
		kfree(file_entry->bitmaps);
	kfree(file_entry);
}

noinline bool package_info_do_destroy(struct package_info *package)
{
	struct file_info *f, *tmp;

	mutex_lock(&g_manager->package_list_lock);
	list_del(&package->app_list);
	g_manager->app_nums--;
	g_manager->total_memory -= package->mm_size;
	mutex_unlock(&g_manager->package_list_lock);
	list_for_each_entry_safe(f, tmp, &package->file_infos, list)
		free_one_fileinfo(package, f);
	kfree(package);
	return true;
}

noinline int mi_lb_dealwith_clear_cmd(struct lb_owner *owner)
{
	struct package_info *package;

	LB_D(57, "clear cmd : package uid: %d, pid: %d, packagename: %s", owner->uid, owner->pid, owner->package_name);
	package = mi_lb_get_package_info(owner, false);
	if (!package) {
		LB_D(61, "app not found, don't clear");
		return -1;
	}
	mutex_lock(&package->package_lock);
	if (package->is_cleared) {
		mutex_unlock(&package->package_lock);
		LB_D(67, "app is cleared, don't clear");
	} else {
		package->is_cleared = true;
		mutex_unlock(&package->package_lock);
		package_info_put(package);
	}
	package_info_put(package);
	return 0;
}

noinline int mi_lb_dealwith_get_app_names(struct lb_app_list *app_list)
{
	struct package_info *p;
	unsigned int n = 0;
	struct list_head *head = &g_manager->package_list;

	mutex_lock(&g_manager->package_list_lock);
	list_for_each_entry(p, head, app_list) {
		struct lb_owner *dst = &app_list->owner[n];

		n++;
		memcpy(dst, &p->owner, sizeof(struct lb_owner));
		if (n == 100)
			break;
	}
	app_list->app_nums = n;
	mutex_unlock(&g_manager->package_list_lock);
	return 0;
}

static int lb_collect_show(struct seq_file *m, void *v)
{
	struct package_info *p;
	int i = 0;

	mutex_lock(&g_manager->package_list_lock);
	if (list_empty(&g_manager->package_list)) {
		seq_puts(m, "g_cold_record_list_user0 is empty\n");
	} else {
		seq_printf(m, "total memory size %lu app_num: %u\n", g_manager->total_memory, g_manager->app_nums);
		list_for_each_entry(p, &g_manager->package_list, app_list) {
			seq_printf(m, "[%d] app:%s, uid:%d, file num: %d, file size: %u, hit rate: %d%%, mm size: %d\n",
				   i++, p->owner.package_name, p->owner.uid, p->file_num, p->total_startup_bytes,
				   p->hit_rate, p->mm_size);
		}
	}
	mutex_unlock(&g_manager->package_list_lock);
	return 0;
}

static int lb_collect_file_open(struct inode *inode, struct file *file)
{
	return single_open(file, lb_collect_show, NULL);
}

static const struct proc_ops lb_collect_file_fops = {
	.proc_open = lb_collect_file_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static inline s64 lb_hot_cnt(void)
{
	s64 c = atomic64_read(&g_hot_mps.stat.total_swapin_count);

	return (u64)c > 1 ? c : 1;
}

static int lb_hot_collect_show(struct seq_file *m, void *v)
{
	struct mps_rec *rec;
	int i = 0;
	s64 avg_rlock, avg_swapin, avg_fadvise, avg_total;

	avg_rlock = atomic64_read(&g_hot_mps.stat.total_rlock_time) / lb_hot_cnt();
	avg_swapin = atomic64_read(&g_hot_mps.stat.total_swapin_time) / lb_hot_cnt();
	avg_fadvise = atomic64_read(&g_hot_mps.stat.total_fadvise_time) / lb_hot_cnt();
	avg_total = atomic64_read(&g_hot_mps.stat.total_preread_time) / lb_hot_cnt();
	seq_printf(m, "######################################################################\nmin_swapin, time: %lld, app: %s\nmax_swapin, time: %lld, app: %s\navg time, rlock: %lld, swapin: %lld, fadvise: %lld, total: %lld, total count: %lld\ninterrupt, by ioctl: %lld, by rlock: %lld, by swapin: %lld\nmemory usage, pte_segment: %u (%luMB), pte_record: %u (%luMB)\n######################################################################\n",
		   g_hot_mps.stat.min_swapin_time == LLONG_MAX ? 0 : g_hot_mps.stat.min_swapin_time,
		   g_hot_mps.stat.shortest_app,
		   g_hot_mps.stat.max_swapin_time, g_hot_mps.stat.longest_app,
		   avg_rlock, avg_swapin, avg_fadvise, avg_total,
		   atomic64_read(&g_hot_mps.stat.total_swapin_count),
		   atomic64_read(&g_hot_mps.stat.interrupt_count[0]),
		   atomic64_read(&g_hot_mps.stat.interrupt_count[1]),
		   atomic64_read(&g_hot_mps.stat.interrupt_count[2]),
		   atomic_read(&g_hot_mps.nr_alloc_segs),
		   ((unsigned long)atomic_read(&g_hot_mps.nr_alloc_segs) * sizeof(struct pte_segment)) >> 20,
		   atomic_read(&g_hot_mps.nr_alloc_mps_recs),
		   ((unsigned long)atomic_read(&g_hot_mps.nr_alloc_mps_recs) * sizeof(struct mps_rec)) >> 20);
	spin_lock(&g_hot_mps.lock);
	if (list_empty(&g_hot_mps.rec_list)) {
		seq_puts(m, "g_hot_record_list_user0 is empty\n");
	} else {
		seq_printf(m, "snapshot: %u prepage: %u err: %u\n", g_hot_mps.nr_snapshot, g_hot_mps.nr_prepage, g_hot_mps.nr_error);
		seq_printf(m, "anon: %llu pre_anon: %llu file: %llu pre_file: %llu\n",
			   g_hot_mps.total_anon, g_hot_mps.total_pre_anon, g_hot_mps.total_file, g_hot_mps.total_pre_file);
		list_for_each_entry(rec, &g_hot_mps.rec_list, list) {
			seq_printf(m, "[%d] uid:%d pid:%d , segs %u pages %u %u pre %u %u hit %u %u, refer/active: %u/%u\n",
				   i++, rec->uid, rec->tgid, rec->nr_segs, rec->snap_anon, rec->snap_file,
				   rec->pre_anon, rec->pre_file, rec->f_d8, rec->f_dc, rec->f_b8, rec->f_b4);
		}
	}
	spin_unlock(&g_hot_mps.lock);
	return 0;
}

static int lb_hot_collect_file_open(struct inode *inode, struct file *file)
{
	return single_open(file, lb_hot_collect_show, NULL);
}

static const struct proc_ops lb_hot_collect_file_fops = {
	.proc_open = lb_hot_collect_file_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static inline int mi_lb_create_proc(void)
{
	struct proc_dir_entry *dir;

	dir = proc_mkdir("iorap_node", NULL);
	if (!dir) {
		LB_E(148, "proc_mkdir failed\n");
		return -ENOMEM;
	}
	if (!proc_create("collect", 0, dir, &lb_collect_file_fops)) {
		remove_proc_entry("iorap_node", NULL);
		LB_E(155, "proc_create lb_collect failed\n");
		return -ENOMEM;
	}
	proc_create("hot_collect", 0, dir, &lb_hot_collect_file_fops);
	return 0;
}

noinline int mi_lb_dbg_init(void)
{
	if (mi_lb_create_proc()) {
		LB_E(177, "lb_create_proc failed\n");
		return -ENOMEM;
	}
	return 0;
}

