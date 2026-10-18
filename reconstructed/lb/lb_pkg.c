// SPDX-License-Identifier: GPL-2.0
/* Reconstructed from the stock lb.ko by reverse engineering; not original source. (package list) */
#include "lb.h"

/* the file-sequential and the real-timing packages live in two separate lists, each with its own mutex */
#define LB_SELECT_LIST(is_rt, lock, head)						\
	do {										\
		(lock) = (is_rt) ? &g_manager->real_timing_list_lock :			\
				   &g_manager->file_sequential_list_lock;		\
		(head) = (is_rt) ? &g_manager->real_timing_list :			\
				   &g_manager->file_sequential_list;			\
	} while (0)

noinline bool package_info_put(struct package_info *package)
{
	if (atomic_dec_and_test(&package->refcount)) {
		package_info_do_destroy(package);
		return true;
	}
	return false;
}

noinline int mi_lb_add_package_info(struct package_info *package)
{
	struct package_info *first;
	struct mutex *lock;
	struct list_head *head;

	if (!package)
		return -EINVAL;
	LB_SELECT_LIST(package->is_real_timing, lock, head);
	mutex_lock(lock);
	if (list_empty(head)) {
		list_add(&package->app_list, head);
		atomic_inc(&package->refcount);
	} else {
		first = list_first_entry(head, struct package_info, app_list);
		if (first->owner.uid == package->owner.uid &&
		    !strcmp(first->owner.package_name, package->owner.package_name))
			goto out;
		list_add(&package->app_list, head);
		atomic_inc(&package->refcount);
	}
	if (package->is_real_timing)
		g_manager->nr_real_timing_apps++;
	else
		g_manager->nr_file_sequential_apps++;
out:
	mutex_unlock(lock);
	return 0;
}

noinline struct package_info *mi_lb_get_package_info(struct cmd_app_info *app_info, bool move_to_head)
{
	struct package_info *p, *found = NULL;
	struct mutex *lock;
	struct list_head *head;

	if (!app_info)
		return NULL;
	LB_SELECT_LIST(app_info->is_real_timing, lock, head);
	mutex_lock(lock);
	list_for_each_entry(p, head, app_list) {
		if (p->owner.uid == app_info->owner.uid && !strcmp(p->owner.package_name, app_info->owner.package_name)) {
			if (move_to_head)
				list_move(&p->app_list, head);
			atomic_inc(&p->refcount);
			found = p;
			break;
		}
	}
	mutex_unlock(lock);
	return found;
}

noinline bool mi_lb_is_in_package_list(struct package_info *package)
{
	struct package_info *first;
	struct mutex *lock;
	struct list_head *head;

	if (!package)
		return false;
	LB_SELECT_LIST(package->is_real_timing, lock, head);
	mutex_lock(lock);
	first = list_first_entry(head, struct package_info, app_list);
	mutex_unlock(lock);
	return first == package;
}

noinline struct package_info *mi_lb_get_oldest_package_info(bool is_real_timing)
{
	struct package_info *p;
	struct mutex *lock;
	struct list_head *head;

	LB_SELECT_LIST(is_real_timing, lock, head);
	mutex_lock(lock);
	p = list_last_entry(head, struct package_info, app_list);
	mutex_unlock(lock);
	return p;
}

noinline bool mi_lb_remove_package_info_from_list(struct package_info *package)
{
	struct package_info *p;
	struct mutex *lock;
	struct list_head *head;

	if (!package)
		return false;
	LB_SELECT_LIST(package->is_real_timing, lock, head);
	mutex_lock(lock);
	list_for_each_entry(p, head, app_list) {
		if (p != package)
			continue;
		list_del(&package->app_list);
		if (package->is_real_timing)
			g_manager->nr_real_timing_apps--;
		else
			g_manager->nr_file_sequential_apps--;
		g_manager->total_memory -= package->mm_size;
		mutex_unlock(lock);
		if (atomic_dec_and_test(&package->refcount))
			package_info_do_destroy(package);
		return true;
	}
	mutex_unlock(lock);
	return false;
}

void mi_lb_page_collect_on_readfile(void *data, struct file *file, loff_t pos, size_t size);
void mi_lb_page_collect_on_pagefault(void *data, struct file *file, pgoff_t orig_start_pgoff, pgoff_t last_pgoff, vm_fault_t ret);
