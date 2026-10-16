/* SPDX-License-Identifier: GPL-2.0 */
/* Reconstructed from the stock lb.ko (Xiaomi 15 "dada", "iorap"/launch booster) by reverse engineering; not original source. */
#ifndef LB_H
#define LB_H

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/mm.h>
#include <linux/mmap_lock.h>
#include <linux/pagewalk.h>
#include <linux/pid.h>
#include <linux/sched/mm.h>
#include <linux/sched/signal.h>
#include <linux/sched/task.h>
#include <linux/kthread.h>
#include <linux/uaccess.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/xarray.h>
#include <linux/swap.h>
#include <linux/rcupdate.h>
#include <linux/ktime.h>
#include <linux/jiffies.h>
#include <linux/bitmap.h>
#include <linux/string.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/vmalloc.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/workqueue.h>
#include <linux/atomic.h>
#include <linux/mman.h>
#include <linux/fadvise.h>
#include <trace/hooks/vendor_hooks.h>
#include <trace/hooks/mm.h>
#include <trace/hooks/sched.h>


/* the stock (LTO) link merges every zero-size lock key into one .bss address: emulate with shared sections */
#define LB_KEY_A	__attribute__((section(".bss.lb_keys_a")))
#define LB_KEY_B	__attribute__((section(".bss.lb_keys_b")))
#undef mutex_init
#undef init_waitqueue_head
#define mutex_init(mutex) do {							static struct lock_class_key __key LB_KEY_A;				__mutex_init((mutex), #mutex, &__key);				} while (0)
#define mutex_init_b(mutex) do {						static struct lock_class_key __key LB_KEY_B;				__mutex_init((mutex), #mutex, &__key);				} while (0)
#define init_waitqueue_head(wq_head) do {					static struct lock_class_key __key LB_KEY_A;				__init_waitqueue_head((wq_head), #wq_head, &__key);		} while (0)

/* ---- logging ---- */
#define LB_E(line, fmt, ...) do { if (g_lb_msg_level >= 0) printk(KERN_NOTICE "[LB E]:%s %d: " fmt, __func__, line, ##__VA_ARGS__); } while (0)
#define LB_D(line, fmt, ...) do { if (g_lb_msg_level >= 3) printk(KERN_NOTICE "[LB D]:%s %d: " fmt, __func__, line, ##__VA_ARGS__); } while (0)
#define LB_I(line, fmt, ...) do { if (g_lb_msg_level >= 2) printk(KERN_NOTICE "[LB I]:%s %d: " fmt, __func__, line, ##__VA_ARGS__); } while (0)

/* ---- ABI structs (user visible) ---- */
struct lb_owner {
	unsigned int uid;
	int pid;
	char package_name[256];
};

enum APP_LAUNCH_CMD {
	APP_LAUNCH_NONE = 0,
	APP_COLLECT_START,
	APP_PREREAD_START,
	APP_LAUNCH_FINISH,
	APP_LAUNCH_ABORT,
	APP_LAUNCH_MAX,
};

struct lb_app_list {
	unsigned int app_nums;
	struct lb_owner owner[100];
};

struct lb_app_preread_info_size {
	struct lb_owner owner;
	size_t info_size;
};

struct lb_app_preread_file_info {
	struct timespec64 mtime;
	unsigned long ino;
	loff_t nr_pages;
	char path[256];
	unsigned int bitmap_longs;
	unsigned long bitmaps[];
};

struct lb_app_preread_info {
	struct lb_owner owner;
	unsigned int file_num;
	size_t file_info_buf_size;
	struct lb_app_preread_file_info preread_file_info[0];
};

/* ---- internal structs ---- */
struct package_info {
	struct list_head app_list;
	struct list_head file_infos;
	struct lb_owner owner;
	struct mutex package_lock;
	unsigned int file_num;
	unsigned int total_startup_bytes;
	unsigned int app_startup_bytes;
	int hit_rate;
	unsigned int mm_size;
	bool is_cleared;
	atomic_t refcount;
};

struct file_info {
	struct list_head list;
	int ref;
	unsigned long ino;
	unsigned int dev;
	char *path;
	struct timespec64 mtime;
	unsigned long collect_num;
	loff_t nr_pages;
	bool is_moved;
	bool is_collected;
	unsigned int mm_size;
	unsigned int bitmap_longs;
	unsigned long *bitmaps;
};

struct lb_file_key {
	unsigned long ino;
	unsigned int dev;
	struct file *file;
};

struct lb_record {
	struct file *file;
	unsigned long index;
	unsigned int nr;
};

struct lb_manager {
	spinlock_t lock;
	struct mutex package_list_lock;
	struct task_struct *compiler_thread;
	char path_buf[4096];
	struct list_head package_list;
	struct lb_file_key files[2048];
	unsigned short nr_files;
	struct lb_record *records;
	unsigned int nr_records;
	wait_queue_head_t compiler_wait;
	struct mutex compiler_lock;
	struct delayed_work compiler_work;
	unsigned long total_memory;
	unsigned int app_nums;
	unsigned long last_jiffies;
};

struct pte_segment {
	unsigned long pagemap[8];
};

struct lb_hot_stat {
	s64 max_swapin_time;
	char longest_app[256];
	s64 min_swapin_time;
	char shortest_app[256];
	atomic64_t total_rlock_time;
	atomic64_t total_swapin_time;
	atomic64_t total_fadvise_time;
	atomic64_t total_preread_time;
	atomic64_t total_swapin_count;
	atomic64_t interrupt_count[3];
};

struct lb_hot_mps {
	spinlock_t lock;
	unsigned int nr_mps_recs;
	unsigned int max_records;
	unsigned int mps_flags;
	struct kmem_cache *seg_cachep;
	struct list_head rec_list;
	struct xarray rec_tree;
	atomic_t nr_alloc_segs;
	atomic_t nr_alloc_mps_recs;
	unsigned int nr_snapshot;
	unsigned int nr_prepage;
	unsigned int nr_start_fra;
	unsigned int nr_error;
	u64 total_anon;
	u64 total_pre_anon;
	u64 total_file;
	u64 total_pre_file;
	char pad70[128];
	struct lb_hot_stat stat;
};

struct mps_rec {
	unsigned int tgid;
	unsigned int uid;
	struct mm_struct *mm;
	struct xarray seg_tree[2];
	struct rcu_head rcu;
	struct mutex rec_lock;
	struct pte_segment *cur_seg;
	unsigned long cur_idx;
	unsigned int cur_type;
	atomic_t refcount;
	struct lb_hot_mps *hot;
	struct list_head list;
	unsigned long last_jiffies;
	unsigned int nr_segs;
	unsigned int snap_anon;
	unsigned int snap_file;
	unsigned int f_b4;
	unsigned int f_b8;
	unsigned int f_bc;
	unsigned int f_c0;
	unsigned int pre_anon;
	unsigned int pre_file;
	char padcc[0xd8 - 0xcc];
	unsigned int f_d8;
	unsigned int f_dc;
	int interrupted;
	char pade4[4];
	s64 rlock_start;
	s64 rlock_total;
	s64 swapin_start;
	s64 swapin_total;
	s64 fadvise_start;
	s64 fadvise_total;
	s64 total_start;
	s64 total_time;
};

#define MPS_ASSERT(s, m, o) _Static_assert(offsetof(s, m) == (o), #s "." #m)
MPS_ASSERT(struct package_info, owner, 0x20);
MPS_ASSERT(struct package_info, package_lock, 0x128);
MPS_ASSERT(struct package_info, file_num, 0x158);
MPS_ASSERT(struct package_info, refcount, 0x170);
_Static_assert(sizeof(struct package_info) == 0x178, "package_info size");
MPS_ASSERT(struct file_info, ino, 0x18);
MPS_ASSERT(struct file_info, path, 0x28);
MPS_ASSERT(struct file_info, mtime, 0x30);
MPS_ASSERT(struct file_info, nr_pages, 0x48);
MPS_ASSERT(struct file_info, is_moved, 0x50);
MPS_ASSERT(struct file_info, bitmap_longs, 0x58);
MPS_ASSERT(struct file_info, bitmaps, 0x60);
_Static_assert(sizeof(struct file_info) == 0x68, "file_info size");
MPS_ASSERT(struct lb_manager, package_list_lock, 8);
MPS_ASSERT(struct lb_manager, compiler_thread, 0x38);
MPS_ASSERT(struct lb_manager, path_buf, 0x40);
MPS_ASSERT(struct lb_manager, package_list, 0x1040);
MPS_ASSERT(struct lb_manager, files, 0x1050);
MPS_ASSERT(struct lb_manager, nr_files, 0xd050);
MPS_ASSERT(struct lb_manager, records, 0xd058);
MPS_ASSERT(struct lb_manager, nr_records, 0xd060);
MPS_ASSERT(struct lb_manager, compiler_wait, 0xd068);
MPS_ASSERT(struct lb_manager, compiler_lock, 0xd080);
MPS_ASSERT(struct lb_manager, compiler_work, 0xd0b0);
MPS_ASSERT(struct lb_manager, compiler_work.timer, 0xd0e0);
MPS_ASSERT(struct lb_manager, total_memory, 0xd138);
_Static_assert(sizeof(struct lb_manager) == 0xd150, "manager size");
MPS_ASSERT(struct lb_hot_mps, seg_cachep, 0x10);
MPS_ASSERT(struct lb_hot_mps, rec_list, 0x18);
MPS_ASSERT(struct lb_hot_mps, rec_tree, 0x28);
MPS_ASSERT(struct lb_hot_mps, nr_alloc_segs, 0x38);
MPS_ASSERT(struct lb_hot_mps, total_anon, 0x50);
MPS_ASSERT(struct lb_hot_mps, stat, 0xf0);
MPS_ASSERT(struct lb_hot_mps, stat.min_swapin_time, 0x1f8);
MPS_ASSERT(struct lb_hot_mps, stat.total_rlock_time, 0x300);
_Static_assert(sizeof(struct lb_hot_mps) == 0x340, "hot_mps size");
MPS_ASSERT(struct mps_rec, mm, 8);
MPS_ASSERT(struct mps_rec, seg_tree, 0x10);
MPS_ASSERT(struct mps_rec, rcu, 0x30);
MPS_ASSERT(struct mps_rec, rec_lock, 0x40);
MPS_ASSERT(struct mps_rec, cur_seg, 0x70);
MPS_ASSERT(struct mps_rec, refcount, 0x84);
MPS_ASSERT(struct mps_rec, hot, 0x88);
MPS_ASSERT(struct mps_rec, list, 0x90);
MPS_ASSERT(struct mps_rec, last_jiffies, 0xa0);
MPS_ASSERT(struct mps_rec, nr_segs, 0xa8);
MPS_ASSERT(struct mps_rec, snap_anon, 0xac);
MPS_ASSERT(struct mps_rec, pre_anon, 0xc4);
MPS_ASSERT(struct mps_rec, f_d8, 0xd8);
MPS_ASSERT(struct mps_rec, interrupted, 0xe0);
MPS_ASSERT(struct mps_rec, total_time, 0x120);
_Static_assert(sizeof(struct mps_rec) == 0x128, "mps_rec size");


/* ---- globals shared between the translation units ---- */
extern int g_lb_msg_level;
extern bool g_only_collect;
extern bool g_atrace_enable;
extern bool g_collect_specify_filetype;
extern bool g_collect_pinfile;
extern bool g_debug_verbose;
extern bool g_rlock_expire_interruput_enable;
extern bool g_swapin_expire_interruput_enable;
extern unsigned long long g_rlock_acquire_threshold;
extern unsigned long long g_swapin_total_time_threshold;
extern bool g_pte_mark_file_anon_enable;
extern bool g_pte_fadvise_enable;
extern bool g_pte_anon_swapin_enable;
extern unsigned int g_pte_max_active_apps;
extern unsigned int g_max_pte_segment_count;
extern struct lb_manager *g_manager;
extern struct lb_owner debug_owner;
extern atomic_t collecting_;
extern struct lb_hot_mps g_hot_mps;

bool package_info_do_destroy(struct package_info *package);
bool package_info_put(struct package_info *package);
bool mi_lb_is_in_package_list(struct package_info *package);
int mi_lb_add_package_info(struct package_info *package);
struct package_info *mi_lb_get_package_info(struct lb_owner *owner, bool move_to_head);
struct package_info *mi_lb_get_oldest_package_info(void);
int mi_lb_dealwith_collect_cmd(enum APP_LAUNCH_CMD cmd, struct lb_owner *owner);
int mi_lb_dealwith_preread_size_cmd(struct lb_app_preread_info_size *app_preread_info_size);
int mi_lb_dealwith_preread_info_cmd(struct lb_app_preread_info *app_preread_info);
int mi_lb_dealwith_clear_cmd(struct lb_owner *owner);
int mi_lb_dealwith_get_app_names(struct lb_app_list *app_list);
int mi_lb_dealwith_pte_clean_cmd(struct lb_owner *owner);
int mi_lb_dealwith_pte_snapshot_cmd(struct lb_owner *owner);
int mi_lb_dealwith_pte_preread_cmd(struct lb_owner *owner);
int mi_lb_dealwith_pte_interrupt_cmd(struct lb_owner *owner);
int mi_lb_exit_manager(void);
int mi_lb_init_manager(void);
int mi_lb_dbg_init(void);
int lb_hot_init(void);
void lb_hot_exit(void);
void free_one_fileinfo(struct package_info *package, struct file_info *file_entry);
void mi_lb_page_collect_on_readfile(void *data, struct file *file, loff_t pos, size_t size);
void mi_lb_page_collect_on_pagefault(void *data, struct file *file, pgoff_t orig_start_pgoff, pgoff_t last_pgoff, vm_fault_t ret);

#endif
