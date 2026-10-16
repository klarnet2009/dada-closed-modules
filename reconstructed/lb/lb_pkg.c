// SPDX-License-Identifier: GPL-2.0
/* Reconstructed from the stock lb.ko by reverse engineering; not original source. (package list) */
#include "lb.h"

noinline bool package_info_put(struct package_info *package)
{
	if (atomic_dec_and_test(&package->refcount)) {
		package_info_do_destroy(package);
		return true;
	}
	return false;
}

noinline bool mi_lb_is_in_package_list(struct package_info *package)
{
	struct package_info *first;

	mutex_lock(&g_manager->package_list_lock);
	first = list_first_entry(&g_manager->package_list, struct package_info, app_list);
	mutex_unlock(&g_manager->package_list_lock);
	return first == package;
}

noinline int mi_lb_add_package_info(struct package_info *package)
{
	struct package_info *first;

	mutex_lock(&g_manager->package_list_lock);
	if (list_empty(&g_manager->package_list)) {
		list_add(&package->app_list, &g_manager->package_list);
		atomic_inc(&package->refcount);
	} else {
		first = list_first_entry(&g_manager->package_list, struct package_info, app_list);
		if (first->owner.uid == package->owner.uid &&
		    !strcmp(first->owner.package_name, package->owner.package_name))
			goto out;
		list_add(&package->app_list, &g_manager->package_list);
		atomic_inc(&package->refcount);
	}
	g_manager->app_nums++;
out:
	mutex_unlock(&g_manager->package_list_lock);
	return 0;
}

noinline struct package_info *mi_lb_get_package_info(struct lb_owner *owner, bool move_to_head)
{
	struct package_info *p, *found = NULL;

	struct list_head *head = &g_manager->package_list;

	mutex_lock(&g_manager->package_list_lock);
	list_for_each_entry(p, head, app_list) {
		if (p->owner.uid == owner->uid && !strcmp(p->owner.package_name, owner->package_name)) {
			if (move_to_head)
				list_move(&p->app_list, head);
			atomic_inc(&p->refcount);
			found = p;
			break;
		}
	}
	mutex_unlock(&g_manager->package_list_lock);
	return found;
}

noinline struct package_info *mi_lb_get_oldest_package_info(void)
{
	struct package_info *p;

	mutex_lock(&g_manager->package_list_lock);
	p = list_last_entry(&g_manager->package_list, struct package_info, app_list);
	mutex_unlock(&g_manager->package_list_lock);
	return p;
}

void mi_lb_page_collect_on_readfile(void *data, struct file *file, loff_t pos, size_t size);
void mi_lb_page_collect_on_pagefault(void *data, struct file *file, pgoff_t orig_start_pgoff, pgoff_t last_pgoff, vm_fault_t ret);

