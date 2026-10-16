// SPDX-License-Identifier: GPL-2.0
/* Reconstructed from the stock lb.ko by reverse engineering; not original source. (hot pte snapshot/preread) */
/* the stock build keeps xa_insert() (a header inline) out of line: provide a noinline copy below */
#define xa_insert __lb_hdr_xa_insert
#include "lb.h"
#undef xa_insert
static noinline __must_check int xa_insert(struct xarray *xa, unsigned long index, void *entry, gfp_t gfp)
{
	int err;

	might_alloc(gfp);
	xa_lock(xa);
	err = __xa_insert(xa, index, entry, gfp);
	xa_unlock(xa);
	return err;
}


struct lb_hot_mps g_hot_mps;

/* ================= hot (pte snapshot / preread) part ================= */

struct swap_iocb;
extern struct page *read_swap_cache_async(swp_entry_t entry, gfp_t gfp_mask, struct vm_area_struct *vma,
					  unsigned long addr, struct swap_iocb **plug);

static void mps_delayed_free_rec(struct rcu_head *head);

static noinline struct pte_segment *mps_alloc_seg(struct lb_hot_mps *hot, gfp_t gfp)
{
	struct pte_segment *seg;

	if (atomic_read(&hot->nr_alloc_segs) >= g_max_pte_segment_count)
		return NULL;
	seg = kmem_cache_zalloc(hot->seg_cachep, gfp);
	if (seg)
		atomic_inc(&hot->nr_alloc_segs);
	return seg;
}

static noinline void mps_free_seg(struct lb_hot_mps *hot, struct pte_segment *seg)
{
	atomic_dec(&hot->nr_alloc_segs);
	kmem_cache_free(hot->seg_cachep, seg);
}

static noinline struct pte_segment *mps_rec_load_seg(struct mps_rec *rec, unsigned long index, unsigned int type)
{
	struct xarray *xa = &rec->seg_tree[type];
	struct pte_segment *seg;

	seg = xa_load(xa, index);
	if (unlikely(!seg)) {
		struct lb_hot_mps *hot = rec->hot;
		int err;

		seg = mps_alloc_seg(hot, GFP_NOWAIT);
		if (!seg) {
			LB_E(77, "alloc seg failed");
			return NULL;
		}
		err = xa_insert(xa, index, seg, GFP_NOWAIT);
		if (err) {
			mps_free_seg(hot, seg);
			LB_E(83, "insert seg failed");
			return NULL;
		}
		rec->nr_segs++;
	}
	rec->cur_seg = seg;
	rec->cur_idx = index;
	rec->cur_type = type;
	return seg;
}

static inline void mps_free_tree(struct lb_hot_mps *hot, struct xarray *xa)
{
	unsigned long index;
	void *seg;

	xa_for_each(xa, index, seg) {
		atomic_dec(&hot->nr_alloc_segs);
		kmem_cache_free(hot->seg_cachep, seg);
		xa_erase(xa, index);
	}
}

static void mps_delayed_free_rec(struct rcu_head *head)
{
	struct mps_rec *rec = container_of(head, struct mps_rec, rcu);
	struct lb_hot_mps *hot = rec->hot;

	mmdrop(rec->mm);
	mps_free_tree(hot, &rec->seg_tree[0]);
	mps_free_tree(hot, &rec->seg_tree[1]);
	kfree(rec);
	atomic_dec(&hot->nr_alloc_mps_recs);
}

static noinline void mps_unlink_rec_locked(struct mm_struct *mm)
{
	struct mps_rec *rec;

	rec = xa_load(&g_hot_mps.rec_tree, (unsigned long)mm);
	if (!rec || rec->mm != mm)
		return;
	if (atomic_read(&rec->refcount) != 1)
		return;
	g_hot_mps.total_pre_anon += rec->pre_anon;
	g_hot_mps.total_pre_file += rec->pre_file;
	g_hot_mps.nr_snapshot--;
	list_del(&rec->list);
	xa_erase(&g_hot_mps.rec_tree, (unsigned long)mm);
	if (atomic_dec_and_test(&rec->refcount))
		call_rcu(&rec->rcu, mps_delayed_free_rec);
}

static void mps_rec_mmput_hook(void *data, struct mm_struct *mm)
{
	if (!mm)
		return;
	spin_lock(&g_hot_mps.lock);
	mps_unlink_rec_locked(mm);
	spin_unlock(&g_hot_mps.lock);
}

noinline int lb_hot_init(void)
{
	LB_I(860, "init");
	memset(&g_hot_mps, 0, sizeof(g_hot_mps));
	g_hot_mps.seg_cachep = kmem_cache_create("pte_segment", sizeof(struct pte_segment), 8, SLAB_ACCOUNT, NULL);
	if (!g_hot_mps.seg_cachep)
		return -ENOMEM;
	INIT_LIST_HEAD(&g_hot_mps.rec_list);
	xa_init(&g_hot_mps.rec_tree);
	atomic_set(&g_hot_mps.nr_alloc_segs, 0);
	atomic_set(&g_hot_mps.nr_alloc_mps_recs, 0);
	g_hot_mps.stat.min_swapin_time = LLONG_MAX;
	register_trace_android_vh_mmput_mm(mps_rec_mmput_hook, NULL);
	return 0;
}

noinline void lb_hot_exit(void)
{
	struct mps_rec *rec, *next;

	LB_I(869, "exit");
	unregister_trace_android_vh_mmput_mm(mps_rec_mmput_hook, NULL);
	spin_lock(&g_hot_mps.lock);
	list_for_each_entry_safe(rec, next, &g_hot_mps.rec_list, list)
		mps_unlink_rec_locked(rec->mm);
	spin_unlock(&g_hot_mps.lock);
	rcu_barrier();
	kmem_cache_destroy(g_hot_mps.seg_cachep);
}

static inline void mps_rec_put(struct mps_rec *rec)
{
	if (atomic_dec_and_test(&rec->refcount))
		call_rcu(&rec->rcu, mps_delayed_free_rec);
}

static inline int mps_link_rec(struct mps_rec *rec, struct mm_struct *mm)
{
	struct lb_hot_mps *hot = &g_hot_mps;
	int err;

	spin_lock(&hot->lock);
	if (g_hot_mps.nr_snapshot > g_pte_max_active_apps - 1) {
		struct mps_rec *oldest = list_last_entry(&g_hot_mps.rec_list, struct mps_rec, list);

		LB_D(218, "shrink app, uid: %u, pid: %u", oldest->uid, oldest->tgid);
		mps_unlink_rec_locked(oldest->mm);
	}
	spin_lock(&g_hot_mps.rec_tree.xa_lock);
	err = __xa_insert(&g_hot_mps.rec_tree, (unsigned long)mm, rec, GFP_NOWAIT);
	spin_unlock(&g_hot_mps.rec_tree.xa_lock);
	if (err) {
		LB_E(223, "insert mps_rec encounter: %d", err);
		spin_unlock(&rec->hot->lock);
		return err;
	}
	atomic_inc(&rec->refcount);
	list_add(&rec->list, &hot->rec_list);
	g_hot_mps.nr_snapshot++;
	spin_unlock(&rec->hot->lock);
	return 0;
}

static inline struct mps_rec *mps_alloc_record(unsigned int tgid, unsigned int uid, struct mm_struct *mm)
{
	struct mps_rec *mps_rec;

	mps_rec = kzalloc(sizeof(*mps_rec), GFP_NOWAIT);
	if (!mps_rec) {
		LB_E(243, "alloc mps_rec %u failed", tgid);
		return NULL;
	}
	atomic_inc(&g_hot_mps.nr_alloc_mps_recs);
	mps_rec->tgid = tgid;
	mps_rec->uid = uid;
	xa_init(&mps_rec->seg_tree[0]);
	xa_init(&mps_rec->seg_tree[1]);
	mutex_init_b(&mps_rec->rec_lock);
	atomic_set(&mps_rec->refcount, 1);
	mps_rec->hot = &g_hot_mps;
	mps_rec->last_jiffies = jiffies;
	mps_rec->mm = mm;
	if (mps_link_rec(mps_rec, mm)) {
		LB_E(259, "mps_rec %u can not link", tgid);
		kfree(mps_rec);
		atomic_dec(&g_hot_mps.nr_alloc_mps_recs);
		return NULL;
	}
	mmgrab(mm);
	return mps_rec;
}

static inline struct mps_rec *mps_find_check_record(struct mps_rec *rec, unsigned int tgid, unsigned int uid)
{
	if (rec && !(rec->tgid == tgid && rec->uid == uid)) {
		LB_E(295, "mps_rec %u %d not equal task %u %u", rec->tgid, rec->uid, tgid, uid);
		mps_rec_put(rec);
		return NULL;
	}
	return rec;
}

static struct mps_rec *mps_find_get_record(int pid, bool create)
{
	struct task_struct *task;
	struct mm_struct *mm;
	struct mps_rec *rec;
	unsigned int tgid;
	unsigned int uid;

	task = get_pid_task(find_vpid(pid), PIDTYPE_PID);
	if (!task) {
		LB_E(314, "get_pid_task %u failed", pid);
		return NULL;
	}
	mm = get_task_mm(task);
	if (!mm) {
		rec = NULL;
		goto out_task;
	}
	tgid = task->tgid;
	uid = task_uid(task).val;

	spin_lock(&g_hot_mps.lock);
	rec = xa_load(&g_hot_mps.rec_tree, (unsigned long)mm);
	if (rec) {
		if (rec->mm != mm || !atomic_inc_not_zero(&rec->refcount))
			rec = NULL;
	}
	spin_unlock(&g_hot_mps.lock);

	if (!create) {
		rec = mps_find_check_record(rec, tgid, uid);
	} else if (rec) {
		mps_rec_put(rec);
		rec = NULL;
	} else {
		rec = mps_alloc_record(tgid, uid, mm);
	}
	mmput(mm);
out_task:
	put_task_struct(task);
	return rec;
}

noinline int mi_lb_dealwith_pte_clean_cmd(struct lb_owner *owner)
{
	struct mps_rec *rec, *tmp;

	spin_lock(&g_hot_mps.lock);
	list_for_each_entry_safe(rec, tmp, &g_hot_mps.rec_list, list) {
		if (rec->tgid == owner->pid && rec->uid == owner->uid) {
			mps_unlink_rec_locked(rec->mm);
			break;
		}
	}
	spin_unlock(&g_hot_mps.lock);
	return 0;
}

noinline int mi_lb_dealwith_pte_interrupt_cmd(struct lb_owner *owner)
{
	struct mps_rec *rec;
	struct mm_struct *mm;

	LB_D(776, "uid: %d, pid: %d, packagename: %s", owner->uid, owner->pid, owner->package_name);
	rec = mps_find_get_record(owner->pid, false);
	if (!rec) {
		LB_E(780, "mps_rec %u maybe no snapshot", owner->pid);
		return -2;
	}
	LB_D(784, "uid: %d, pid: %d, packagename: %s, interrupt preread", owner->uid, owner->pid, owner->package_name);
	rec->interrupted = 1;
	atomic64_inc(&g_hot_mps.stat.interrupt_count[0]);
	mm = rec->mm;
	mps_rec_put(rec);
	spin_lock(&g_hot_mps.lock);
	mps_unlink_rec_locked(mm);
	spin_unlock(&g_hot_mps.lock);
	return 0;
}

static int mps_hot_pte_range(pmd_t *pmd, unsigned long addr, unsigned long end, struct mm_walk *walk)
{
	struct mps_rec *rec = walk->private;
	struct vm_area_struct *vma = walk->vma;
	struct file *file;
	spinlock_t *ptl = NULL;
	pte_t *pte;
	int ret = 0;
	unsigned int type;
	int nr = 0;
	int huge = pmd_trans_huge(*pmd);

	if (pmd_devmap(*pmd) || huge)
		return 0;
	file = vma->vm_file;
	if (!g_pte_anon_swapin_enable && !file)
		return 0;
	if (!g_pte_fadvise_enable && file)
		return 0;
	type = g_pte_mark_file_anon_enable && file;
	pte = pte_offset_map_lock(vma->vm_mm, pmd, addr, &ptl);
	if (!pte)
		return 0;
	for (; addr != end; pte++, addr += PAGE_SIZE) {
		pte_t ptent = *pte;
		struct page *page;
		struct pte_segment *seg;
		unsigned long idx;

		if (!pte_present(ptent))
			continue;
		page = vm_normal_page(vma, addr, ptent);
		if (!page)
			continue;
		if (PageActive(page)) {
			rec->f_b4++;
		} else {
			bool referenced = PageReferenced(page);

			if (!pte_young(ptent)) {
				if (referenced)
					rec->f_bc++;
				continue;
			}
			rec->f_c0++;
			if (!referenced)
				continue;
			rec->f_bc++;
			rec->f_b8++;
		}
		idx = addr >> 21;
		seg = NULL;
		if (idx == rec->cur_idx && type == rec->cur_type)
			seg = rec->cur_seg;
		if (!seg)
			seg = mps_rec_load_seg(rec, idx, type);
		if (unlikely(!seg)) {
			ret = -ENOMEM;
			break;
		}
		seg->pagemap[(addr >> 18) & 7] |= 1UL << ((addr >> 12) & 63);
		nr++;
	}
	pte_unmap_unlock(pte, ptl);
	if (!vma->vm_file)
		rec->snap_anon += nr;
	else
		rec->snap_file += nr;
	return ret;
}

struct mps_swapin_priv {
	struct mps_rec *rec;
	struct vm_area_struct *vma;
};

static int mps_swapin_walk_pmd_entry(pmd_t *pmd, unsigned long addr, unsigned long end, struct mm_walk *walk)
{
	struct mps_swapin_priv *priv = walk->private;
	struct mps_rec *rec = priv->rec;
	struct vm_area_struct *vma = priv->vma;
	spinlock_t *ptl;
	pte_t *pte = NULL;

	for (; addr < end; addr += PAGE_SIZE) {
		pte_t ptent;
		swp_entry_t entry;
		struct page *page;

		if (!pte) {
			pte = pte_offset_map_lock(vma->vm_mm, pmd, addr, &ptl);
			if (!pte)
				return 0;
		} else {
			pte++;
		}
		ptent = READ_ONCE(*pte);
		if (!is_swap_pte(ptent))
			continue;
		entry = pte_to_swp_entry(ptent);
		if (non_swap_entry(entry))
			continue;
		pte_unmap_unlock(pte, ptl);
		page = read_swap_cache_async(entry, GFP_HIGHUSER_MOVABLE, vma, addr, NULL);
		pte = NULL;
		if (page) {
			put_page(page);
			rec->pre_anon++;
		}
	}
	if (pte)
		pte_unmap_unlock(pte, ptl);
	return 0;
}

noinline int mi_lb_dealwith_pte_snapshot_cmd(struct lb_owner *owner)
{
	struct mps_rec *rec;
	struct mm_struct *mm;
	struct vm_area_struct *vma;
	int ret = 0;

	LB_D(688, "uid: %d, pid: %d, packagename: %s", owner->uid, owner->pid, owner->package_name);
	rec = mps_find_get_record(owner->pid, true);
	if (!rec) {
		LB_E(692, "mps_rec %u maybe has snapshot", owner->pid);
		return 0;
	}
	mutex_lock(&rec->rec_lock);
	mm = rec->mm;
	struct mm_walk_ops ops = { .pmd_entry = mps_hot_pte_range };
	VMA_ITERATOR(vmi, mm, 0);
	mmap_read_lock(mm);
	for_each_vma(vmi, vma) {
		if (!vma->vm_file && !(vma->vm_end >> 32))
			continue;
		if (vma->vm_flags & (VM_PFNMAP | VM_MIXEDMAP))
			continue;
		ret = walk_page_range(mm, vma->vm_start, vma->vm_end, &ops, rec);
		if (ret)
			break;
	}
	mmap_read_unlock(mm);
	spin_lock(&g_hot_mps.lock);
	g_hot_mps.total_anon += rec->snap_anon;
	g_hot_mps.total_file += rec->snap_file;
	spin_unlock(&g_hot_mps.lock);
	mutex_unlock(&rec->rec_lock);
	mps_rec_put(rec);
	if (ret)
		g_hot_mps.nr_error++;
	return ret;
}

static inline int mps_rec_prepage_seg_range(struct mps_rec *rec, struct pte_segment *seg, unsigned long index,
					     unsigned int flags)
{
	unsigned long base = index << 21;
	unsigned int start, end;
	int ret;

	start = find_next_bit(seg->pagemap, 512, 0);
	end = find_next_zero_bit(seg->pagemap, 512, start + 1);
	while (start < end) {
		struct lb_hot_mps *hot;
		struct mm_struct *mm;
		struct vm_area_struct *vma;
		struct file *file = NULL;
		unsigned long addr, vend, off = 0, len;
		s64 delta;
		bool keep;

		if (READ_ONCE(rec->interrupted))
			return 0;
		hot = rec->hot;
		rec->rlock_start = ktime_get() / 1000;
		mm = rec->mm;
		mmap_read_lock(mm);
		delta = ktime_get() / 1000 - rec->rlock_start;
		keep = g_rlock_expire_interruput_enable && delta > g_rlock_acquire_threshold;
		if (keep) {
			rec->interrupted = 1;
			atomic64_inc(&hot->stat.interrupt_count[1]);
		}
		len = (unsigned long)(end - start) << 12;
		addr = base + ((unsigned long)start << 12);
		vend = addr + len;
		rec->rlock_total += delta;
		vma = find_vma(rec->mm, addr);
		ret = 0;
		if (vma && !(vma->vm_flags & (VM_PFNMAP | VM_MIXEDMAP))) {
			bool skip = false;

			if (vma->vm_file && !(flags & 0x20))
				skip = true;
			if (!vma->vm_file && !(flags & 0x10))
				skip = true;
			if (!skip) {
				if (vma->vm_end < vend)
					vend = vma->vm_end;
				if (vma->vm_file && get_file_rcu(vma->vm_file)) {
					file = vma->vm_file;
					off = (addr - vma->vm_start) + (vma->vm_pgoff << 12);
				} else if (!keep) {
					rec->swapin_start = ktime_get() / 1000;
					struct mm_walk_ops ops = { .pmd_entry = mps_swapin_walk_pmd_entry };
					struct mps_swapin_priv priv = { .rec = rec, .vma = vma };

					ret = walk_page_range(rec->mm, addr, min(vma->vm_end, vend), &ops, &priv);
					if (ret > 0)
						ret = 0;
					rec->swapin_total += ktime_get() / 1000 - rec->swapin_start;
				}
			}
		}
		mm = rec->mm;
		mmap_read_unlock(mm);
		if (file) {
			len = vend - addr;
			if (g_atrace_enable) {
				const char *p = file_path(file, hot->pad70, 256);

				LB_D(590, "pre_hot_file:%s [0x%lld 0x%lld]", p, (long long)off, (long long)(off + len));
			}
			rec->fadvise_start = ktime_get() / 1000;
			ret = vfs_fadvise(file, off, len, POSIX_FADV_WILLNEED);
			if (!ret)
				rec->pre_file += len >> 12;
			fput(file);
			rec->fadvise_total += ktime_get() / 1000 - rec->fadvise_start;
		}
		if (ret)
			return ret;
		start = find_next_bit(seg->pagemap, 512, end + 1);
		end = find_next_zero_bit(seg->pagemap, 512, start + 1);
	}
	return 0;
}

static inline int mps_rec_prepage(struct mps_rec *rec)
{
	struct lb_hot_mps *hot;
	struct pte_segment *seg;
	unsigned long index;
	unsigned int flags = 0x30;
	unsigned int snap_anon, snap_file;
	bool first = true;
	bool keep;
	int type = 0;
	int ret = 0;
	s64 now;

	snap_anon = rec->snap_anon;
	snap_file = rec->snap_file;
	rec->pre_anon = 0;
	rec->pre_file = 0;
	hot = rec->hot;
	rec->total_start = ktime_get() / 1000;

	do {
		struct xarray *xa = &rec->seg_tree[type];

		index = 0;
		for (seg = xa_find(xa, &index, ULONG_MAX, XA_PRESENT); seg;
		     seg = xa_find_after(xa, &index, ULONG_MAX, XA_PRESENT)) {
			if (READ_ONCE(rec->interrupted))
				goto finish;
			ret = mps_rec_prepage_seg_range(rec, seg, index, flags);
			if (!(rec->pre_anon < snap_anon))
				flags &= ~0x10;
			if (!(rec->pre_file < snap_file))
				flags &= ~0x20;
			if (!(flags & 0x30) || ret == -ENOMEM)
				break;
			now = ktime_get() / 1000;
			if (g_swapin_expire_interruput_enable && now - rec->total_start > g_swapin_total_time_threshold) {
				rec->interrupted = 1;
				atomic64_inc(&hot->stat.interrupt_count[2]);
				goto finish;
			}
		}
		keep = g_pte_mark_file_anon_enable && g_pte_fadvise_enable && first;
		first = false;
		type = 1;
	} while (keep);

finish:
	rec->total_time += ktime_get() / 1000 - rec->total_start;
	LB_D(677, "mps_rec %u ret %d prepage %u %u seg %u, rlock: %lld, swapin: %lld, fadvise: %lld, total: %lld, interrupt: %d",
	     rec->tgid, ret, rec->pre_anon, rec->pre_file, rec->nr_segs, rec->rlock_total, rec->swapin_total,
	     rec->fadvise_total, rec->total_time, READ_ONCE(rec->interrupted));
	return ret;
}

noinline int mi_lb_dealwith_pte_preread_cmd(struct lb_owner *owner)
{
	struct mps_rec *rec;
	struct mm_struct *mm;
	int ret;

	LB_D(740, "uid: %d, pid: %d, packagename: %s", owner->uid, owner->pid, owner->package_name);
	rec = mps_find_get_record(owner->pid, false);
	if (!rec) {
		LB_E(744, "mps_rec %u maybe no snapshot", owner->pid);
		return 0;
	}
	mutex_lock(&rec->rec_lock);
	ret = mps_rec_prepage(rec);
	spin_lock(&g_hot_mps.lock);
	g_hot_mps.nr_prepage++;
	spin_unlock(&g_hot_mps.lock);
	mutex_unlock(&rec->rec_lock);
	if (rec->total_time < g_hot_mps.stat.min_swapin_time) {
		g_hot_mps.stat.min_swapin_time = rec->total_time;
		memcpy(g_hot_mps.stat.shortest_app, owner->package_name, 256);
	}
	if (rec->total_time > g_hot_mps.stat.max_swapin_time) {
		g_hot_mps.stat.max_swapin_time = rec->total_time;
		memcpy(g_hot_mps.stat.longest_app, owner->package_name, 256);
	}
	atomic64_add(rec->rlock_total, &g_hot_mps.stat.total_rlock_time);
	atomic64_add(rec->swapin_total, &g_hot_mps.stat.total_swapin_time);
	atomic64_add(rec->fadvise_total, &g_hot_mps.stat.total_fadvise_time);
	atomic64_add(rec->total_time, &g_hot_mps.stat.total_preread_time);
	atomic64_inc(&g_hot_mps.stat.total_swapin_count);
	mm = rec->mm;
	mps_rec_put(rec);
	spin_lock(&g_hot_mps.lock);
	mps_unlink_rec_locked(mm);
	spin_unlock(&g_hot_mps.lock);
	if (ret)
		g_hot_mps.nr_error++;
	return ret;
}
