/* SPDX-License-Identifier: GPL-2.0 */
/* mi_game: module-param callbacks of the migt TU (G1x). Included from migt_b.h. NOT hardware-tested: DO NOT LOAD. */

/* game thread list of the "game_task_list" param: one entry = process name + thread name (+ pid / uid for the wrender list) */
struct game_spid {
	struct hlist_node node;
	char process_name[16];
	char thread_name[16];
	int pid, uid;
};
struct hlist_head game_task_render_list, game_task_wrender_list;
static int game_list_length, game_list_wlength;
static DEFINE_SPINLOCK(game_task_lock);

static int get_migt_freq(char *buf, const struct kernel_param *kp)
{
	int n = strlen(kp->name);
	char *ceil;
	int len = 0, cpu;

	strnstr(kp->name, "migt_freq", n);
	ceil = strnstr(kp->name, "migt_ceiling_freq", n);
	for_each_possible_cpu(cpu)
		len += snprintf(buf + len, PAGE_SIZE - len, "%d:%u ", cpu,
				ceil ? per_cpu(migt_info, cpu).ceiling_freq : per_cpu(migt_info, cpu).boost_freq);
	return len + snprintf(buf + len, PAGE_SIZE - len, "\n");
}

/* "<freq>" sets every cpu, "cpu:freq cpu:freq ..." sets single cpus; migt_ceiling_freq writes the ceiling slot */
static int set_migt_freq(const char *val, const struct kernel_param *kp)
{
	int n = strlen(kp->name);
	unsigned int cpu_freq[2] = { 0, 0 };	/* sscanf targets: [0] cpu, [1] freq */
	char *ceil, *p = (char *)val;
	int nsep = -1, i, cpu;

	strnstr(kp->name, "migt_freq", n);
	ceil = strnstr(kp->name, "migt_ceiling_freq", n);
	do {
		p = strpbrk(p + 1, " :");
		nsep++;
	} while (p);
	p = (char *)val;
	if (nsep == 0) {
		if (sscanf(p, "%u\n", &cpu_freq[1]) != 1)
			return -EINVAL;
		for_each_possible_cpu(cpu) {
			if (ceil)
				per_cpu(migt_info, cpu).ceiling_freq = cpu_freq[1];
			else
				per_cpu(migt_info, cpu).boost_freq = cpu_freq[1];
		}
	} else {
		if (!(nsep & 1))
			return -EINVAL;
		i = 0;
		do {
			if (sscanf(p, "%u:%u", &cpu_freq[0], &cpu_freq[1]) != 2)
				return -EINVAL;
			if (cpu_freq[0] >= num_possible_cpus())
				return -EINVAL;
			if (ceil)
				per_cpu(migt_info, cpu_freq[0]).ceiling_freq = cpu_freq[1];
			else
				per_cpu(migt_info, cpu_freq[0]).boost_freq = cpu_freq[1];
			p = strchr(p, ' ') + 1;
			i += 2;
		} while (i < nsep);
	}
	migt_enabled = 0;
	for_each_possible_cpu(cpu) {
		if (per_cpu(migt_info, cpu).boost_freq) {
			migt_enabled = 1;
			break;
		}
	}
	return 0;
}

static const struct kernel_param_ops param_ops_migt_freq = { .set = set_migt_freq, .get = get_migt_freq };
module_param_cb(migt_freq, &param_ops_migt_freq, NULL, 0644);
module_param_cb(migt_ceiling_freq, &param_ops_migt_freq, NULL, 0644);
module_param_named(boost_policy, migt_boost_policy, uint, 0644);

/* add_vip_group / add_ip_group / clean_user_group: "<pid>" tags (or untags) the task as user vip / ip */
static int set_migt_flag(const char *val, const struct kernel_param *kp)
{
	struct task_struct *task;
	struct pid *pid;
	int nr = 0;

	if (sscanf(val, "%d", &nr) != 1)
		return -EINVAL;
	pid = find_get_pid(nr);
	if (!pid) {
		migt_err("%s fail to find_get_pid\n", __func__);
		return -EINVAL;
	}
	task = get_pid_task(pid, PIDTYPE_PID);
	if (!task) {
		migt_err("%s fail to get_pid_task\n", __func__);
		put_pid(pid);
		return -EINVAL;
	}
	if (strnstr(kp->name, "add_vip_group", strlen(kp->name)))
		user_set_game_vip(task, true);
	if (strnstr(kp->name, "add_ip_group", strlen(kp->name)))
		user_set_game_ip(task, true);
	if (strnstr(kp->name, "clean_user_group", strlen(kp->name))) {
		user_set_game_vip(task, false);
		user_set_game_ip(task, false);
	}
	put_task_struct(task);
	put_pid(pid);
	return 0;
}

static const struct kernel_param_ops param_ops_migt_flag = { .set = set_migt_flag, .get = get_migt_flag };
module_param_cb(add_ip_group, &param_ops_migt_flag, NULL, 0644);
module_param_cb(add_vip_group, &param_ops_migt_flag, NULL, 0644);
module_param_cb(clean_user_group, &param_ops_migt_flag, NULL, 0644);

static int get_game_task(char *buffer, const struct kernel_param *kp)
{
	struct game_spid *s;
	char *buf = kzalloc(2048, GFP_KERNEL);
	int len, len2, ret;

	if (!buf)
		return 0;
	len = scnprintf(buf, 2048, "game_list_length:%d\n", game_list_length);
	len += scnprintf(buf + len, 2048 - len, "%s\t%s\n", "process_name", "thread_name");
	spin_lock(&game_task_lock);
	s = (struct game_spid *)game_task_render_list.first;
	while (s && len <= 2046) {
		len += scnprintf(buf + len, 2048 - len, "%s\t%s\n", s->process_name, s->thread_name);
		s = (struct game_spid *)s->node.next;
	}
	if (len > 2046)
		goto out;
	len += scnprintf(buf + len, 2048 - len, "\ngame_list_wlength:%d\n", game_list_wlength);
	if (len > 2046)
		goto out;
	len2 = scnprintf(buf + len, 2048 - len, "%s\t%s\t%s\t%s\n", "process_name", "thread_name", "pid", "uid");
	len += len2;
	s = (struct game_spid *)game_task_wrender_list.first;
	while (s && len <= 2046) {
		len += scnprintf(buf + len, 2048 - len, "%s\t%s\t%d\t%d\n", s->process_name, s->thread_name, s->pid, s->uid);
		s = (struct game_spid *)s->node.next;
	}
out:
	spin_unlock(&game_task_lock);
	ret = scnprintf(buffer, PAGE_SIZE, "%s", buf);
	kfree(buf);
	return ret;
}

/* "0 0" clears both lists, "<process> <thread>" adds a render thread entry (max 50) */
static int set_game_task(const char *val, const struct kernel_param *kp)
{
	char process[16] = "", thread[16] = "";
	struct game_spid *s;
	struct hlist_node *tmp;
	struct hlist_node *pos;
	char *buf = kzalloc(2048, GFP_KERNEL);
	int ret;

	if (!buf)
		return -ENOMEM;
	if (!scnprintf(buf, 2048, "%s", val)) {
		ret = 0;
		goto out;
	}
	if (sscanf(buf, "%15s %15s", process, thread) != 2) {
		ret = -EINVAL;
		goto out;
	}
	if (process[0] == '0' && thread[0] == '0') {
		spin_lock(&game_task_lock);
		hlist_for_each_safe(pos, tmp, &game_task_render_list) {
			hlist_del(pos);
			kvfree(pos);
			game_list_length--;
		}
		hlist_for_each_safe(pos, tmp, &game_task_wrender_list) {
			hlist_del(pos);
			kvfree(pos);
			game_list_wlength--;
		}
		spin_unlock(&game_task_lock);
		ret = 0;
		goto out;
	}
	if (game_list_length > 49) {
		ret = -ENOMEM;
		goto out;
	}
	s = kzalloc(sizeof(*s), GFP_KERNEL);
	if (!s) {
		ret = -ENOMEM;
		goto out;
	}
	spin_lock(&game_task_lock);
	hlist_add_head(&s->node, &game_task_render_list);
	game_list_length++;
	spin_unlock(&game_task_lock);
	strncpy(s->process_name, process, 16);
	s->process_name[15] = 0;
	strncpy(s->thread_name, thread, 16);
	s->thread_name[15] = 0;
	ret = 0;
out:
	kfree(buf);
	return ret;
}

static const struct kernel_param_ops param_ops_game_task = { .set = set_game_task, .get = get_game_task };
module_param_cb(game_task_list, &param_ops_game_task, NULL, 0644);

/* prefer-cluster / prefer-cpu votes: range checked int params */
int set_prefer_cluster_with_check(const char *val, const struct kernel_param *kp)
{
	int v = 0;

	if (sscanf(val, "%d", &v) != 1)
		return -EINVAL;
	if (v < 0 || v >= (int)num_sched_clusters) {
		migt_err("set prefer cluster error, not allowed: %d\n", v);
		return -EINVAL;
	}
	return param_set_int(val, kp);
}

int set_prefer_cpu_with_check(const char *val, const struct kernel_param *kp)
{
	int v = 0, ret;

	if (sscanf(val, "%d", &v) != 1)
		return -EINVAL;
	if (v < -2 || v > 7) {
		printk(KERN_ERR "migt-gtc: set prefer cpu error, not allowed: %d\n", v);
		return -EINVAL;
	}
	ret = param_set_int(val, kp);
	if (ret)
		return ret;
	update_migt_reserve_cpus();
	return 0;
}

int set_prefer_cpu_with_check_and_update(const char *val, const struct kernel_param *kp)
{
	int v = 0, ret;

	if (sscanf(val, "%d", &v) != 1)
		return -EINVAL;
	if (v < -2 || v > 7) {
		printk(KERN_ERR "migt-gtc: set prefer cpu error, not allowed: %d\n", v);
		return -EINVAL;
	}
	ret = param_set_int(val, kp);
	if (ret)
		return ret;
	update_migt_reserve_cpus();
	return 0;
}

static const struct kernel_param_ops param_ops_refer_cluster = { .set = set_prefer_cluster_with_check, .get = param_get_int };
static const struct kernel_param_ops param_ops_refer_cpu = { .set = set_prefer_cpu_with_check, .get = param_get_int };
static const struct kernel_param_ops param_ops_refer_cpu_update = { .set = set_prefer_cpu_with_check_and_update, .get = param_get_int };
#define PREFER_PARAM(ops, name)	module_param_cb(name, &ops, &name, 0644)
PREFER_PARAM(param_ops_refer_cluster, stask_prefer_cluster);
PREFER_PARAM(param_ops_refer_cluster, render_prefer_cluster);
PREFER_PARAM(param_ops_refer_cluster, vip_prefer_cluster);
PREFER_PARAM(param_ops_refer_cluster, ip_prefer_cluster);
PREFER_PARAM(param_ops_refer_cpu, stask_prefer_cpu_high);
PREFER_PARAM(param_ops_refer_cpu, render_prefer_cpu_high);
PREFER_PARAM(param_ops_refer_cpu, vip_prefer_cpu_high);
PREFER_PARAM(param_ops_refer_cpu, stask_prefer_cpu_low);
PREFER_PARAM(param_ops_refer_cpu, render_prefer_cpu_low);
PREFER_PARAM(param_ops_refer_cpu, vip_prefer_cpu_low);
PREFER_PARAM(param_ops_refer_cpu_update, ip_prefer_cpu);
PREFER_PARAM(param_ops_refer_cpu_update, stask_prefer_cpu);
PREFER_PARAM(param_ops_refer_cpu_update, render_prefer_cpu);
PREFER_PARAM(param_ops_refer_cpu_update, vip_prefer_cpu);

/* list helpers used by the ioctl / wrender code: render list max 50 entries, wrender list max 100 */
__used struct game_spid *game_add_task(struct hlist_head *head)
{
	struct game_spid *e;

	if (!head)
		return NULL;
	if (head == &game_task_render_list && game_list_length > 49)
		return NULL;
	if (head == &game_task_wrender_list && game_list_wlength > 99)
		return NULL;
	e = kzalloc(sizeof(*e), GFP_KERNEL);
	if (!e)
		return NULL;
	spin_lock(&game_task_lock);
	hlist_add_head(&e->node, head);
	if (head == &game_task_render_list)
		game_list_length++;
	else if (head == &game_task_wrender_list)
		game_list_wlength++;
	spin_unlock(&game_task_lock);
	return e;
}

__used void game_delete_spid(struct game_spid *e, struct hlist_head *head)
{
	if (!e)
		return;
	spin_lock(&game_task_lock);
	hlist_del(&e->node);
	kvfree(e);
	if (head == &game_task_render_list)
		game_list_length--;
	else if (head == &game_task_wrender_list)
		game_list_wlength--;
	spin_unlock(&game_task_lock);
}
