// SPDX-License-Identifier: GPL-2.0
/* Reconstructed from the stock lb.ko (OS4) by reverse engineering; not original source. (cold start record/preread, real timing) */
#include "lb.h"

struct lb_manager *g_manager;
atomic_t collecting_;

static struct {
	struct package_info *package;
	unsigned int nr_pids;
	bool by_tgid;
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

	if (g_active_package.by_tgid) {
		if (!g_active_package.package || !lb_pid_collecting(current->tgid))
			return;
	} else {
		if (!g_active_package.package || !lb_pid_collecting(current->pid))
			return;
	}

	if (!file->f_inode->i_sb)
		return;
	magic = file->f_inode->i_sb->s_magic;
	if (magic != 0xef53 && magic != 0xf2f52010 && magic != 0xe0f5e1e2)
		return;

	mgr = g_manager;
	if (time_after(jiffies, mgr->last_jiffies + 500))
		return;

	spin_lock(&g_manager->lock);
	if (!g_active_package.package)
		goto out;
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
	if (g_manager->nr_records < 0xac00) {
		struct collect_info *rec = &g_manager->records[g_manager->nr_records];

		rec->file = cached;
		rec->index = index;
		rec->nr = nr_pages;
		g_manager->nr_records++;
	} else if (g_manager->extra_records && g_manager->nr_extra_records < 0x15800) {
		struct collect_info *rec = &g_manager->extra_records[g_manager->nr_extra_records];

		rec->file = cached;
		rec->index = index;
		rec->nr = nr_pages;
		g_manager->nr_extra_records++;
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
		mi_lb_pagecache_collect(file, orig_start_pgoff, last_pgoff - orig_start_pgoff + 1, true);
}

/* ---- vendor hooks: stop background tasks from reading ahead while an app launch is recorded ---- */
static inline bool lb_task_is_background(struct task_struct *p)
{
	struct css_set *cset;
	struct cgroup_subsys_state *css;
	struct cgroup *cgrp;
	struct kernfs_node *kn;

	if (!p)
		return false;
	cset = READ_ONCE(p->cgroups);
	css = cset->subsys[0];
	if (!css)
		return false;
	cgrp = css->cgroup;
	if (!cgrp)
		return false;
	kn = cgrp->kn;
	if (!kn)
		return false;
	return !strncmp(kn->name, "background", 11);
}

static inline bool lb_skip_ra_task(void)
{
	struct task_struct *p = current;

	if (!lb_task_is_background(p))
		return false;
	if (p->prio < MAX_RT_PRIO)
		return false;
	return !lb_pid_collecting(p->tgid);
}

void adjust_readahead(void *data, struct readahead_control *ractl, unsigned long *max_page)
{
	struct file_ra_state *ra;

	if (!g_skip_background_readahead_enable || !g_active_package.package)
		return;
	ra = ractl->ra;
	if (!lb_skip_ra_task())
		return;
	*max_page = min_t(long, *max_page, ra->ra_pages >> 1);
}

void adjust_readaround(void *data, unsigned int ra_pages, pgoff_t pgoff, pgoff_t *start, unsigned int *size, unsigned int *async_size)
{
	if (!g_skip_background_readahead_enable || !g_active_package.package)
		return;
	if (!lb_skip_ra_task())
		return;
	*start = max_t(long, pgoff - (ra_pages >> 2), 0);
	*size = ra_pages >> 1;
	*async_size = ra_pages >> 3;
}

void mi_lb_skip_async_mmap_readahead(void *data, struct vm_fault *vmf, struct folio *folio, bool *skip)
{
	if (!g_skip_background_readahead_enable || !g_active_package.package)
		return;
	if (!lb_skip_ra_task())
		return;
	*skip = true;
}

void should_skip_fault_around(void *data, struct vm_fault *vmf, bool *should_around)
{
	if (!g_background_faultaround_disable || !g_active_package.package)
		return;
	if (!lb_skip_ra_task())
		return;
	*should_around = false;
}

/* put every file reference taken by the collect hooks and reset the buffers */
static inline void mi_lb_record_buf_clear(void)
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
	if (g_manager->extra_records) {
		kvfree(g_manager->extra_records);
		g_manager->extra_records = NULL;
		g_manager->nr_extra_records = 0;
		g_manager->total_memory -= 0x204000;
	}
}

static __always_inline void lb_pid_append(struct lb_owner *owner)
{
	if (g_active_package.package && g_active_package.nr_pids < 5) {
		int *slot = &g_active_package.pids[g_active_package.nr_pids];

		*slot = owner->pid;
		g_active_package.nr_pids++;
	}
}

/* ---- package creation ---- */
static noinline struct package_info *real_timing_package_init(struct lb_owner *owner)
{
	struct package_info *package;

	while (g_manager->nr_real_timing_apps >= g_max_real_timing_apps) {
		struct package_info *oldest = mi_lb_get_oldest_package_info(true);

		if (!oldest) {
			LB_E(433, "no old package to free");
			break;
		}
		package_info_put(oldest);
	}
	package = kzalloc(sizeof(*package), GFP_KERNEL);
	if (!package) {
		LB_E(440, "alloc package info fail");
		return NULL;
	}
	package->mm_size = sizeof(*package);
	package->is_cleared = false;
	package->is_real_timing = true;
	g_manager->total_memory += sizeof(*package);
	memcpy(&package->owner, owner, sizeof(*owner));
	return package;
}

static __always_inline struct package_info *file_sequential_package_init(struct lb_owner *owner)
{
	struct package_info *package;

	while (g_manager->nr_file_sequential_apps >= g_max_file_sequential_apps) {
		struct package_info *oldest = mi_lb_get_oldest_package_info(false);

		if (!oldest) {
			LB_E(461, "no old package to free");
			break;
		}
		package_info_put(oldest);
	}
	package = kzalloc(sizeof(*package), GFP_KERNEL);
	if (!package) {
		LB_E(468, "alloc package info fail");
		return NULL;
	}
	INIT_LIST_HEAD(&package->file_infos);
	package->is_real_timing = false;
	package->file_num = 0;
	package->mm_size = sizeof(*package);
	g_manager->total_memory += sizeof(*package);
	package->is_cleared = false;
	memcpy(&package->owner, owner, sizeof(*owner));
	return package;
}

static inline int mb_lb_dealwith_collect_start(struct cmd_app_info *app_info)
{
	struct lb_owner *owner = &app_info->owner;
	struct package_info *package;
	struct lb_manager *mgr;
	const char *name;

	mi_lb_record_buf_clear();
	package = mi_lb_get_package_info(app_info, true);
	if (!package) {
		if (app_info->is_real_timing)
			package = real_timing_package_init(owner);
		else
			package = file_sequential_package_init(owner);
		if (!package) {
			LB_E(516, "alloc package info fail");
			return -1;
		}
		atomic_inc(&package->refcount);
	}
	g_active_package.package = package;
	g_active_package.by_tgid = false;
	name = owner->package_name;
	mgr = g_manager;
	mgr->last_jiffies = jiffies;
	if (!strcmp(name, "com.tencent.jkchess") || !strcmp(name, "com.tencent.KiHan") ||
	    !strcmp(name, "com.tencent.tmgp.supercell.brawlstars") || !strcmp(name, "com.tencent.msgame")) {
		if (!mgr->extra_records) {
			mgr->extra_records = kvmalloc(0x204000, GFP_KERNEL);
			mgr = g_manager;
			if (mgr->extra_records) {
				mgr->nr_extra_records = 0;
				mgr->total_memory += 0x204000;
			} else {
				LB_E(536, "failed to pre-allocate extra 2MB buffer for %s", name);
			}
		}
	}
	queue_delayed_work(system_wq, &mgr->compiler_work, 500);
	lb_pid_append(owner);
	return 0;
}

static inline int mb_lb_dealwith_collect_finish(struct lb_owner *owner)
{
	struct package_info *package;

	package = g_active_package.package;
	if (!package) {
		LB_E(346, "active package is NULL");
		return 0;
	}
	if (package->owner.uid == owner->uid && !strcmp(package->owner.package_name, owner->package_name)) {
		cancel_delayed_work_sync(&g_manager->compiler_work);
		wake_up_all(&g_manager->compiler_wait);
	} else {
		LB_E(356, "finish collecting app[%s] != active app[%s]", package->owner.package_name, owner->package_name);
	}
	return 0;
}

noinline int mi_lb_dealwith_collect_cmd(enum APP_LAUNCH_CMD cmd, struct cmd_app_info *app_info)
{
	struct lb_owner *owner = &app_info->owner;
	struct task_struct *task;

	LB_D(553, "cmd: %d, readahead mode %d uid: %d, pid: %d, packagename: %s", cmd, app_info->is_real_timing,
	     owner->uid, owner->pid, owner->package_name);
	switch (cmd) {
	case APP_COLLECT_START:
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
		task = get_pid_task(find_vpid(owner->pid), PIDTYPE_PID);
		if (!task) {
			LB_E(560, "get_pid_task fail");
			goto fail;
		}
		if (!strcmp(task->comm, "RenderThread")) {
			LB_E(565, "first launch process is %s, maybe timeout 2s", task->comm);
			put_task_struct(task);
			goto fail;
		}
		put_task_struct(task);
		if (mb_lb_dealwith_collect_start(app_info)) {
			LB_E(575, "launch app %s failed", owner->package_name);
			goto fail;
		}
		break;
	case APP_LAUNCH_FINISH:
		if (!atomic_read(&collecting_)) {
			LB_D(590, "finish: the app[%s] is not collecting", owner->package_name);
			break;
		}
		mb_lb_dealwith_collect_finish(owner);
		break;
	default:
		LB_E(594, "unknown cold start cmd is %u", cmd);
		break;
	}
	return 0;
fail:
	atomic_set(&collecting_, 0);
	return -1;
}

static void mi_lb_do_compile(struct work_struct *work)
{
	if (g_active_package.package)
		LB_D(366, "timeout 2s, wake up all %s", g_active_package.package->owner.package_name);
	wake_up_all(&g_manager->compiler_wait);
}

static int mi_lb_compiler_thread(void *data);

noinline int mi_lb_init_manager(void)
{
	struct task_struct *t;
	struct lb_manager *m;
	struct collect_info *records;
	int ret;

	g_manager = kzalloc(sizeof(*g_manager), GFP_KERNEL);
	if (!g_manager)
		return -ENOMEM;
	records = kvmalloc(0x102000, GFP_KERNEL);
	m = g_manager;
	m->records = records;
	if (!records) {
		ret = -ENOMEM;
		goto free_mgr;
	}
	m->extra_records = NULL;
	m->nr_extra_records = 0;
	m->total_memory = sizeof(*g_manager) + 0x102000;
	spin_lock_init(&m->lock);
	mutex_init(&g_manager->compiler_lock);
	mutex_init(&g_manager->clear_lock);
	mutex_init(&g_manager->file_sequential_list_lock);
	mutex_init(&g_manager->real_timing_list_lock);
	INIT_LIST_HEAD(&g_manager->file_sequential_list);
	INIT_LIST_HEAD(&g_manager->real_timing_list);
	INIT_DELAYED_WORK(&g_manager->compiler_work, mi_lb_do_compile);
	g_manager->nr_records = 0;
	g_manager->nr_files = 0;
	init_waitqueue_head(&g_manager->compiler_wait);
	t = kthread_create(mi_lb_compiler_thread, g_manager, "lb:g_manager");
	if (IS_ERR(t)) {
		ret = PTR_ERR(t);
		g_manager->compiler_thread = t;
		LB_E(1247, "create compiler_thread fail, ret: %d", ret);
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
	g_manager = NULL;
	return ret;
}

static const char * const file_type_list[] = { "so", "apk", "dex", "vdex", "odex", "art", "jar", NULL };

/* the path of a file as the user space sees it: mount root prefix (unless /data or /media) + d_path */
static noinline char *lb_file_path(struct file *file, char *buf)
{
	char mnt_path[32];
	char path_buf[240];
	char *mnt_root_path, *path;
	unsigned int prefix_len = 0;
	unsigned int len;

	mnt_root_path = dentry_path_raw(file->f_path.mnt->mnt_root, mnt_path, sizeof(mnt_path));
	if (!IS_ERR(mnt_root_path) && strncmp(mnt_root_path, "/data", 5) && strncmp(mnt_root_path, "/media", 6)) {
		prefix_len = min_t(unsigned int, strlen(mnt_root_path), 4095);
		memcpy(buf, mnt_root_path, prefix_len);
	}
	path = d_path(&file->f_path, path_buf, sizeof(path_buf));
	if (IS_ERR(path)) {
		LB_D(54, "d_path failed");
		path = file_path(file, path_buf, sizeof(path_buf));
		if (IS_ERR(path)) {
			LB_D(57, "file_path also failed");
			buf[0] = '\0';
			return ERR_PTR(-ENOENT);
		}
	}
	len = strlen(path);
	if (prefix_len + len > 4095)
		len = 4095 - prefix_len;
	memcpy(buf + prefix_len, path, len);
	buf[prefix_len + len] = '\0';
	return buf;
}

/* list_del() is not inlined into the big get_fileinfo in the stock link */
static __always_inline void lb_list_del(struct list_head *entry)
{
	if (__list_del_entry_valid(entry)) {
		entry->next->prev = entry->prev;
		WRITE_ONCE(entry->prev->next, entry->next);
	}
	entry->next = LIST_POISON1;
	entry->prev = LIST_POISON2;
}

static __always_inline int file_sequential_launch_reset_fileinfo(struct file_info *info, struct file *file)
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
		LB_E(647, "alloc file_info bitmaps err");
		if (info->path)
			kfree(info->path);
		lb_list_del(&info->list);
		if (info)
			kfree(info);
		return -ENOMEM;
	}
	return 0;
}

static noinline int file_sequential_get_fileinfo(struct package_info *active_package, struct file *file, struct file_info **pinfo)
{
	struct file_info *info;
	struct list_head *head = &active_package->file_infos;
	struct inode *inode;
	char *path;
	size_t len;
	int plen;
	loff_t size;
	int i;

	path = lb_file_path(file, g_manager->path_buf);
	if (IS_ERR(path)) {
		LB_E(671, "find filepath error\n");
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
				*pinfo = info;
				inode = file->f_inode;
				if (info->ino != inode->i_ino || inode->i_mtime.tv_sec != info->mtime.tv_sec) {
					if (file_sequential_launch_reset_fileinfo(info, file))
						return -1;
				}
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
		LB_E(693, "alloc file_info err");
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
		LB_E(709, "alloc file_info bitmaps err");
		goto free_info;
	}
	plen = strlen(path);
	info->path = kzalloc(plen + 1, GFP_KERNEL);
	if (!info->path) {
		LB_E(716, "info->path alloc fail\n");
		bitmap_free(info->bitmaps);
		goto free_info;
	}
	strncpy(info->path, path, plen + 1);
	*pinfo = info;
	list_add(&info->list, head);
	active_package->file_num++;
	{
		unsigned long sz = plen + 1 + info->bitmap_longs * sizeof(unsigned long) + sizeof(*info);

		info->mm_size = sz;
		active_package->mm_size += sz;
		g_manager->total_memory += info->mm_size;
	}
	return 0;
free_info:
	kfree(info);
	return -1;
}

static inline bool mi_lb_file_is_collected(struct file_info *info)
{
	unsigned int k;

	for (k = 0; k < g_manager->nr_files; k++) {
		if (g_manager->files[k].dev == info->dev && g_manager->files[k].ino == info->ino)
			break;
	}
	return k < g_manager->nr_files;
}

noinline void file_sequential_free_one_fileinfo(struct package_info *package, struct file_info *file_entry)
{
	list_del(&file_entry->list);
	package->file_num--;
	if (file_entry->path)
		kfree(file_entry->path);
	if (file_entry->bitmaps)
		kfree(file_entry->bitmaps);
	kfree(file_entry);
}

/* ---- real timing: file index lookup, dex bitmaps, ranges ---- */
static noinline unsigned short real_timing_lookup_file_index(struct package_info *package, struct real_timing_file_node *file_lists,
							      unsigned short *filename_count, struct collect_info *collect_info)
{
	unsigned short idx = *filename_count;
	unsigned short nr_files = g_manager->nr_files;
	struct real_timing_file_node *node;
	struct inode *inode;
	char *path;
	unsigned int len;
	int i;

	for (i = 0; i < idx; i++) {
		inode = collect_info->file->f_inode;
		if (file_lists[i].dev == inode->i_sb->s_dev && file_lists[i].ino == inode->i_ino)
			return i;
	}
	if (idx >= nr_files)
		return nr_files;
	path = lb_file_path(collect_info->file, g_manager->path_buf);
	if (IS_ERR(path)) {
		LB_E(758, "find filepath error\n");
		return g_manager->nr_files;
	}
	len = strlen(path) + 1;
	node = &file_lists[idx];
	node->path_len = len;
	node->path = kzalloc(len, GFP_KERNEL);
	if (!node->path) {
		LB_E(764, "info->path alloc fail\n");
		return g_manager->nr_files;
	}
	node->dev = collect_info->file->f_inode->i_sb->s_dev;
	node->ino = collect_info->file->f_inode->i_ino;
	memcpy(node->path, path, node->path_len);	/* G55a: stock reloads path_len after kzalloc (FORTIFY compares it with the allocation size) */
	node->i_mtime = collect_info->file->f_inode->i_mtime.tv_sec;
	INIT_LIST_HEAD(&node->file_head);
	if (g_file_page_reclaim_supported && package->is_file_page_reclaimed && strstr(node->path, package->owner.package_name))
		node->is_dex_file = true;
	package->file_info.paths_buffer_size += node->path_len;
	package->file_info.file_nums++;
	(*filename_count)++;
	return idx;
}

static noinline void dex_files_for_bitmap_record(struct package_info *package, struct collect_info *collect_info,
						 unsigned short file_index, struct real_timing_file_node *file_lists)
{
	struct real_timing_file_node *node = &file_lists[file_index];
	struct reclaimed_file *rf;
	const char *name;
	loff_t size;
	unsigned int type;
	unsigned int nr_pages;

	if (!node->is_dex_file)
		return;
	name = node->path;
	if (strstr(name, ".apk"))
		type = 0;
	else if (strstr(name, ".odex"))
		type = 1;
	else if (strstr(name, ".vdex"))
		type = 2;
	else
		return;
	rf = &package->reclaimed_files[type];
	size = min_t(loff_t, i_size_read(collect_info->file->f_inode), 1UL << 31);
	size += 0xfff;
	nr_pages = size >> 12;
	if (!rf->bitmaps) {
		rf->bitmap_longs = BITS_TO_LONGS(nr_pages);
		rf->bitmaps = bitmap_zalloc(size >> 12, GFP_KERNEL);
		if (!rf->bitmaps) {
			LB_E(807, "alloc file_info bitmaps err");
			return;
		}
		{
			unsigned int bytes = rf->bitmap_longs * sizeof(unsigned long);	/* G55a: stock shifts in 32 bit (lsl w8,w8,#3) and adds the same value to both */

			package->mm_size += bytes;
			g_manager->total_memory += bytes;
		}
	}
	if (collect_info->index + collect_info->nr > nr_pages)
		return;
	bitmap_set(rf->bitmaps, collect_info->index, collect_info->nr);
}

static int real_timing_file_range_cmp(void *priv, const struct list_head *a, const struct list_head *b)
{
	const struct real_timing_range_node *ra = list_entry(a, struct real_timing_range_node, list);
	const struct real_timing_range_node *rb = list_entry(b, struct real_timing_range_node, list);

	if (ra->start < rb->start)
		return -1;
	if (ra->start != rb->start)
		return 1;
	if (ra->end < rb->end)
		return -1;
	return ra->end != rb->end;
}

/* one range node per collected record, chained per file and in one global list */
static __always_inline void real_timing_file_range_add_to_list(struct package_info *package, struct real_timing_file_node *file_lists,
							       unsigned short *filename_count, struct list_head *range_list,
							       size_t lists_size)
{
	struct real_timing_range_node *range;
	int i;
	unsigned short file_index;

	for (i = 0; i < g_manager->nr_records; i++) {
		range = kzalloc(sizeof(*range), GFP_KERNEL);
		if (!range) {
			LB_E(837, "alloc range_list err");
			continue;
		}
		file_index = real_timing_lookup_file_index(package, file_lists, filename_count, &g_manager->records[i]);
		if (file_index == g_manager->nr_files) {
			LB_E(843, "file_index is g_manager->collected_num");
			kfree(range);
			continue;
		}
		range->seq = i;
		range->file_index = file_index;
		range->start = g_manager->records[i].index;
		range->end = g_manager->records[i].nr + (unsigned int)g_manager->records[i].index;
		range->valid = true;
		list_add_tail(&range->list, &file_lists[file_index].file_head);
		list_add_tail(&range->all_list, range_list);
		if (g_file_page_reclaim_supported && package->is_file_page_reclaimed)
			dex_files_for_bitmap_record(package, &g_manager->records[i], file_index, file_lists);
	}
	if (g_manager->extra_records && g_manager->nr_extra_records) {
		for (i = 0; i < g_manager->nr_extra_records; i++) {
			range = kzalloc(sizeof(*range), GFP_KERNEL);
			if (!range) {
				LB_E(866, "alloc range_list err");
				continue;
			}
			file_index = real_timing_lookup_file_index(package, file_lists, filename_count, &g_manager->extra_records[i]);
			if (file_index == g_manager->nr_files) {
				LB_E(872, "file_index is g_manager->collected_num");
				kfree(range);
				continue;
			}
			range->seq = g_manager->nr_records + i;
			range->file_index = file_index;
			range->start = g_manager->extra_records[i].index;
			range->end = g_manager->extra_records[i].nr + (unsigned int)g_manager->extra_records[i].index;
			range->valid = true;
			list_add_tail(&range->list, &file_lists[file_index].file_head);
			list_add_tail(&range->all_list, range_list);
			if (g_file_page_reclaim_supported && package->is_file_page_reclaimed)
				dex_files_for_bitmap_record(package, &g_manager->extra_records[i], file_index, file_lists);
		}
	}
}

/* sort the ranges of every file and merge the neighbouring ones */
static __always_inline void real_timing_file_range_merge(struct package_info *package, struct real_timing_file_node *file_lists)
{
	int i;

	package->range_nums = 0;
	for (i = 0; i < package->file_info.file_nums; i++) {
		struct list_head *head = &file_lists[i].file_head;
		struct list_head *pos;
		struct real_timing_range_node *prev, *cur;

		if (list_empty(head))
			continue;
		list_sort(NULL, head, real_timing_file_range_cmp);
		package->range_nums++;
		prev = list_first_entry(head, struct real_timing_range_node, list);
		for (pos = head->next; pos != head; pos = pos->next) {
			cur = list_entry(pos, struct real_timing_range_node, list);
			if (cur == prev)
				continue;
			if (prev->end + g_max_gap_range >= cur->start) {
				unsigned int end = max(prev->end, cur->end);
				unsigned int start = min(prev->start, cur->start);

				if (end - prev->start <= g_max_merge_range) {
					if (prev->seq < cur->seq) {
						prev->start = start;
						prev->end = end;
						cur->valid = false;
					} else {
						prev->valid = false;
						cur->start = start;
						cur->end = end;
						prev = cur;
					}
					continue;
				}
			}
			package->range_nums++;
			prev = cur;
		}
	}
}

static __always_inline void real_timing_file_range_copy_to_array(struct package_info *package, struct list_head *range_list)
{
	struct real_timing_range_node *r, *tmp;
	int n = 0;	/* G55a: stock indexes the ranges array with a signed counter (smaddl) */

	package->ranges = kvmalloc(package->range_nums * sizeof(*package->ranges), GFP_KERNEL);
	if (!package->ranges) {
		LB_E(971, "alloc package->ranges err");
		return;
	}
	package->ranges_buffer_size = package->range_nums * sizeof(*package->ranges);
	package->mm_size += package->ranges_buffer_size;
	g_manager->total_memory += package->ranges_buffer_size;
	list_for_each_entry_safe(r, tmp, range_list, all_list) {
		if (r->valid) {
			package->ranges[n].file_index = r->file_index;
			package->ranges[n].start = r->start;
			package->ranges[n].end = r->end;
			n++;
		}
		list_del(&r->all_list);
		kfree(r);
	}
}

static __always_inline int real_timing_file_path_copy_to_array(struct package_info *package, struct real_timing_file_node *file_lists)
{
	char *pos;
	int i;

	package->file_info.file_paths = kvmalloc(package->file_info.paths_buffer_size, GFP_KERNEL);
	if (!package->file_info.file_paths) {
		LB_E(1002, "alloc package->file_info.file_paths err");
		return -ENOMEM;
	}
	package->mm_size += package->file_info.paths_buffer_size;
	g_manager->total_memory += package->file_info.paths_buffer_size;
	package->file_info.i_mtime_buffer_size = package->file_info.file_nums * sizeof(unsigned int);
	package->file_info.i_mtime_buffer = kzalloc(package->file_info.i_mtime_buffer_size, GFP_KERNEL);
	if (!package->file_info.i_mtime_buffer) {
		LB_E(1011, "alloc package->file_info.i_mtime_buffer err");
		kvfree(package->file_info.file_paths);
		return -ENOMEM;
	}
	package->mm_size += package->file_info.i_mtime_buffer_size;
	g_manager->total_memory += package->file_info.i_mtime_buffer_size;
	pos = package->file_info.file_paths;
	for (i = 0; i < package->file_info.file_nums; i++) {
		if (pos + file_lists[i].path_len > package->file_info.file_paths + package->file_info.paths_buffer_size) {
			LB_E(1021, "pos + file_lists[i].path_len > package->file_info.file_paths + package->file_info.paths_buffer_size");
			return -ENOMEM;
		}
		memcpy(pos, file_lists[i].path, file_lists[i].path_len);
		package->file_info.i_mtime_buffer[i] = file_lists[i].i_mtime;
		kfree(file_lists[i].path);
		file_lists[i].path = NULL;
		pos += file_lists[i].path_len;
	}
	return 0;
}

static __always_inline void real_timing_app_compiler(struct package_info *package)
{
	struct real_timing_file_node *file_lists;
	struct list_head range_list;
	unsigned short collected_num = 0;
	unsigned int nr_files;
	int i;

	if (g_manager->nr_records + g_manager->nr_extra_records == 0 || !g_manager->nr_files) {
		LB_E(1041, "total collected == 0 || g_manager->collected_num == 0");
		goto done;
	}
	nr_files = g_manager->nr_files;
	file_lists = kzalloc(nr_files * sizeof(*file_lists), GFP_KERNEL);
	if (!file_lists) {
		LB_E(1047, "alloc file_list err");
		goto done;
	}
	INIT_LIST_HEAD(&range_list);
	real_timing_file_range_add_to_list(package, file_lists, &collected_num, &range_list, nr_files * sizeof(*file_lists));
	real_timing_file_range_merge(package, file_lists);
	real_timing_file_range_copy_to_array(package, &range_list);
	if (real_timing_file_path_copy_to_array(package, file_lists)) {
		LB_E(1056, "real_timing_file_path_copy_to_array err");
		for (i = 0; i < package->file_info.file_nums; i++)
			if (file_lists[i].path)
				kfree(file_lists[i].path);
	}
	kfree(file_lists);
done:
	mi_lb_add_package_info(package);
	package_info_put(package);
}

static __always_inline bool mi_lb_app_compiler(struct package_info *package)
{
	struct lb_owner owner;

	memcpy(&owner, &package->owner, sizeof(owner));
	mutex_lock(&g_manager->clear_lock);
	if (package->is_cleared) {
		package_info_put(package);
		mutex_unlock(&g_manager->clear_lock);
		return false;
	}
	if (mi_lb_is_in_package_list(package)) {
		package->is_cleared = true;
		package_info_put(package);
		package_info_put(package);
		mutex_unlock(&g_manager->clear_lock);
		package = real_timing_package_init(&owner);
		if (!package) {
			LB_E(1135, "real_timing_package_init err");
			return false;
		}
		atomic_inc(&package->refcount);
	} else {
		mutex_unlock(&g_manager->clear_lock);
	}
	real_timing_app_compiler(package);
	return true;
}

static __always_inline void file_sequential_app_compiler(struct package_info *package)
{
	struct file_info *info, *next;
	int i;

	list_for_each_entry_safe(info, next, &package->file_infos, list)
		info->is_collected = false;

	if (g_manager->nr_records >= 0xac00)
		g_manager->nr_records = 0xac00;
	for (i = 0; i < g_manager->nr_records; i++) {
		struct collect_info *rec = &g_manager->records[i];

		if (file_sequential_get_fileinfo(package, rec->file, &info))
			continue;
		if (rec->index + rec->nr > info->nr_pages)
			continue;
		bitmap_set(info->bitmaps, rec->index, rec->nr);
	}
	if (g_manager->extra_records && g_manager->nr_extra_records) {
		for (i = 0; i < g_manager->nr_extra_records; i++) {
			struct collect_info *rec = &g_manager->extra_records[i];

			if (file_sequential_get_fileinfo(package, rec->file, &info))
				continue;
			if (rec->index + rec->nr > info->nr_pages)
				continue;
			bitmap_set(info->bitmaps, rec->index, rec->nr);
		}
	}
	list_for_each_entry_safe(info, next, &package->file_infos, list) {
		info->is_moved = false;
		if (mi_lb_file_is_collected(info))
			info->ref = 3;
		else if (--info->ref == 0)
			file_sequential_free_one_fileinfo(package, info);
	}
	mi_lb_add_package_info(package);
	package_info_put(package);
}

static __always_inline void mi_lb_compiler_do_work(void)
{
	struct package_info *package;
	ktime_t start, end;

	start = ktime_get();
	mutex_lock(&g_manager->compiler_lock);
	spin_lock(&g_manager->lock);
	package = g_active_package.package;
	memset(&g_active_package, 0, sizeof(g_active_package));
	spin_unlock(&g_manager->lock);
	if (!package) {
		mutex_unlock(&g_manager->compiler_lock);
		LB_E(1164, "package is null");
		return;
	}
	if (package->is_real_timing)
		mi_lb_app_compiler(package);
	else
		file_sequential_app_compiler(package);
	mi_lb_record_buf_clear();
	atomic_set(&collecting_, 0);
	mutex_unlock(&g_manager->compiler_lock);
	end = ktime_get();
	LB_D(1180, "compiler finish, compile_time: %lld ms, total_apps: %u, total_memory: %lu.%02luMB",
	     ktime_to_ms(ktime_sub(end, start)), g_manager->nr_real_timing_apps + g_manager->nr_file_sequential_apps,
	     g_manager->total_memory >> 20, (g_manager->total_memory * 100 >> 20) % 100);
	if (g_manager->total_memory > 0x500000)
		LB_E(1186, "total_memory exceeded 5MB limit: %lu.%02luMB, total_apps: %u", g_manager->total_memory >> 20,
		     (g_manager->total_memory * 100 >> 20) % 100, g_manager->nr_real_timing_apps + g_manager->nr_file_sequential_apps);
}

static int mi_lb_compiler_thread(void *data)
{
	struct lb_manager *mgr = data;

	if (!data) {
		LB_E(1195, "data is null");
		return -EINVAL;
	}
	while (!kthread_should_stop()) {
		wait_event_interruptible(mgr->compiler_wait, kthread_should_stop() || atomic_read(&collecting_));
		if (kthread_should_stop())
			break;
		if (!atomic_read(&collecting_))
			continue;
		mi_lb_compiler_do_work();
	}
	return 0;
}

noinline int mi_lb_exit_manager(void)
{
	if (g_manager) {
		kthread_stop(g_manager->compiler_thread);
		mi_lb_record_buf_clear();
		if (g_manager->records)
			kvfree(g_manager->records);
		if (g_manager)
			kfree(g_manager);
		g_manager = NULL;
	}
	return 0;
}

noinline int mi_lb_dealwith_preread_size_cmd(struct lb_app_preread_info_size *info_size)
{
	struct package_info *package;
	struct file_info *f;
	size_t size;
	int ret;

	LB_D(151, "preread size: package uid: %d, packagename: %s", info_size->app_info.owner.uid, info_size->app_info.owner.package_name);
	mutex_lock(&g_manager->compiler_lock);
	package = mi_lb_get_package_info(&info_size->app_info, false);
	if (!package) {
		LB_D(156, "app first cold start, No pre-reading");
		ret = -1;
		goto out;
	}
	if (package->is_real_timing) {
		size = (size_t)package->file_info.i_mtime_buffer_size + package->file_info.paths_buffer_size +
		       package->ranges_buffer_size + sizeof(struct lb_app_preread_info);
		goto set_size;
	}
	if (list_empty(&package->file_infos))
		goto no_files;
	size = sizeof(struct lb_app_preread_info);
	list_for_each_entry(f, &package->file_infos, list) {
		if (f->is_collected)
			size += f->bitmap_longs * sizeof(unsigned long) + sizeof(struct lb_file_sequential_file_info);
	}
	if (!size)
		goto no_files;
set_size:
	info_size->info_size = size;
	package_info_put(package);
	ret = 0;
	goto out;
no_files:
	LB_D(163, "app has collected, but no pre-reading files");
	ret = -2;
out:
	mutex_unlock(&g_manager->compiler_lock);
	return ret;
}

static inline int get_file_sequential_preread_info(struct package_info *package, struct lb_app_preread_info *info)
{
	struct file_info *f;
	struct lb_file_sequential_file_info *pf;
	size_t size = sizeof(*info), next_size, bytes;
	size_t len;
	unsigned int n;
	const char *name;

	info->file_sequential.file_num = 0;
	list_for_each_entry_reverse(f, &package->file_infos, list) {
		if (!f->is_collected)
			continue;
		bytes = (size_t)f->bitmap_longs << 3;
		next_size = size + bytes + sizeof(*pf);
		if (next_size > info->preread_info_buf_size) {
			LB_E(39, "Exceeded preread_info memory size %zu", info->preread_info_buf_size);
			break;
		}
		pf = (struct lb_file_sequential_file_info *)((char *)info + size);
		pf->mtime = f->mtime;
		pf->ino = f->ino;
		pf->nr_pages = f->nr_pages;
		name = f->path;
		len = strlen(name);
		n = len;
		if (n > 255) {
			LB_E(54, "file path is too long %s", name);
			pf->path[0] = '\0';
		} else {
			char *dst = strncpy(pf->path, name, n);

			dst[n] = '\0';
		}
		pf->bitmap_longs = f->bitmap_longs;
		memcpy(pf->bitmaps, f->bitmaps, bytes);
		info->file_sequential.file_num++;
		size = next_size;
	}
	return 0;
}

static inline int get_real_timing_preread_info(struct package_info *package, struct lb_app_preread_info *info)
{
	info->real_timing.file_nums = package->file_info.file_nums;
	info->real_timing.i_mtimes_buffer_size = package->file_info.i_mtime_buffer_size;
	info->real_timing.paths_buffer_size = package->file_info.paths_buffer_size;
	info->real_timing.ranges_buffer_size = package->ranges_buffer_size;
	info->real_timing.range_nums = package->range_nums;
	size_t off;

	memcpy((char *)(info + 1), package->file_info.i_mtime_buffer, package->file_info.i_mtime_buffer_size);
	off = package->file_info.i_mtime_buffer_size + sizeof(*info);
	memcpy((char *)info + off, package->file_info.file_paths, package->file_info.paths_buffer_size);
	memcpy((char *)info + package->file_info.paths_buffer_size + off, package->ranges, package->ranges_buffer_size);
	return 0;
}

noinline int mi_lb_dealwith_preread_info_cmd(struct lb_app_preread_info *info)
{
	struct package_info *package;

	LB_D(183, "preread info uid: %d, packagename: %s", info->app_info.owner.uid, info->app_info.owner.package_name);
	mutex_lock(&g_manager->compiler_lock);
	package = mi_lb_get_package_info(&info->app_info, false);
	if (!package) {
		mutex_unlock(&g_manager->compiler_lock);
		LB_D(189, "app first cold start, No pre-reading");
		return -1;
	}
	if (package->is_real_timing)
		get_real_timing_preread_info(package, info);
	else
		get_file_sequential_preread_info(package, info);
	package_info_put(package);
	mutex_unlock(&g_manager->compiler_lock);
	return 0;
}

noinline bool package_info_do_destroy(struct package_info *package)
{
	struct file_info *f, *tmp;

	if (package->is_real_timing) {
		mutex_lock(&g_manager->real_timing_list_lock);
		list_del(&package->app_list);
		g_manager->nr_real_timing_apps--;
		g_manager->total_memory -= package->mm_size;
		if (package->ranges)
			kvfree(package->ranges);
		if (package->file_info.file_paths)
			kvfree(package->file_info.file_paths);
		if (package->file_info.i_mtime_buffer)
			kvfree(package->file_info.i_mtime_buffer);
		if (package->reclaimed_files[0].bitmaps)
			kfree(package->reclaimed_files[0].bitmaps);
		if (package->reclaimed_files[1].bitmaps)
			kfree(package->reclaimed_files[1].bitmaps);
		if (package->reclaimed_files[2].bitmaps)
			kfree(package->reclaimed_files[2].bitmaps);
		mutex_unlock(&g_manager->real_timing_list_lock);
		kfree(package);
		return true;
	}
	mutex_lock(&g_manager->file_sequential_list_lock);
	list_del(&package->app_list);
	g_manager->nr_file_sequential_apps--;
	g_manager->total_memory -= package->mm_size;
	mutex_unlock(&g_manager->file_sequential_list_lock);
	list_for_each_entry_safe(f, tmp, &package->file_infos, list) {
		list_del(&f->list);
		package->file_num--;
		if (f->path)
			kfree(f->path);
		if (f->bitmaps)
			kfree(f->bitmaps);
		kfree(f);
	}
	kfree(package);
	return true;
}

noinline int mi_lb_dealwith_clear_cmd(struct cmd_app_info *app_info)
{
	struct package_info *package;

	LB_D(93, "clear cmd : package uid: %d, pid: %d, packagename: %s", app_info->owner.uid, app_info->owner.pid, app_info->owner.package_name);
	mutex_lock(&g_manager->clear_lock);
	package = mi_lb_get_package_info(app_info, false);
	if (!package) {
		mutex_unlock(&g_manager->clear_lock);
		LB_D(99, "app not found, don't clear");
		return -1;
	}
	if (!package->is_cleared) {
		package->is_cleared = true;
		package_info_put(package);
	}
	package_info_put(package);
	mutex_unlock(&g_manager->clear_lock);
	return 0;
}

static int lb_collect_show(struct seq_file *m, void *v)
{
	struct package_info *p;
	int i;

	seq_printf(m, "iorap version: %u.%u.%u\n", 3, 0, 0);
	seq_printf(m, "total memory size %lu app_num: %u\n", g_manager->total_memory,
		   g_manager->nr_file_sequential_apps + g_manager->nr_real_timing_apps);
	mutex_lock(&g_manager->file_sequential_list_lock);
	if (list_empty(&g_manager->file_sequential_list)) {
		seq_puts(m, "file sequential collect app is empty\n");
	} else {
		seq_printf(m, "file sequential mode aps[%u] list:\n", g_manager->nr_file_sequential_apps);
		i = 0;
		list_for_each_entry(p, &g_manager->file_sequential_list, app_list) {
			seq_printf(m, "[%d] app:%s, uid:%d, file num: %d, mm size: %d\n", i++, p->owner.package_name,
				   p->owner.uid, p->file_num, p->mm_size);
		}
	}
	mutex_unlock(&g_manager->file_sequential_list_lock);
	seq_printf(m, "real timing mode apps[%u] list:\n", g_manager->nr_real_timing_apps);
	mutex_lock(&g_manager->real_timing_list_lock);
	if (list_empty(&g_manager->real_timing_list)) {
		seq_puts(m, "file sequential collect app  is empty\n");
	} else {
		i = 0;
		list_for_each_entry(p, &g_manager->real_timing_list, app_list) {
			seq_printf(m, "[%d] app:%s, uid:%d, file num: %d, mm size: %d\n", i++, p->owner.package_name,
				   p->owner.uid, p->file_info.file_nums, p->mm_size);
		}
	}
	mutex_unlock(&g_manager->real_timing_list_lock);
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
		LB_E(170, "proc_mkdir failed\n");
		return -ENOMEM;
	}
	if (!proc_create("collect", 0, dir, &lb_collect_file_fops)) {
		remove_proc_entry("iorap_node", NULL);
		LB_E(177, "proc_create lb_collect failed\n");
		return -ENOMEM;
	}
	if (!proc_create("hot_collect", 0, dir, &lb_hot_collect_file_fops)) {
		remove_proc_entry("collect", dir);
		remove_proc_entry("iorap_node", NULL);
		LB_E(185, "proc_create lb_hot_collect failed\n");
		return -ENOMEM;
	}
	return 0;
}

noinline int mi_lb_dbg_init(void)
{
	if (mi_lb_create_proc()) {
		LB_E(199, "lb_create_proc failed\n");
		return -ENOMEM;
	}
	return 0;
}
