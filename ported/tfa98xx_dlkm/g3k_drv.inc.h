/*
 * g3k: driver level pieces of the Xiaomi stock tfa98xx_dlkm (v6.13._xiaomi) that the v6.5.3 template lacks.
 * Included at the end of tfa98xx.c. The debugfs/mixer registration of these items is NOT wired into
 * tfa98xx_probe (g3k-guess: the stock does it inside the 663-instruction tfa98xx_container_loaded / probe code
 * that was not reconstructed); the functions are marked __used so that they exist in the module.
 */
#include <linux/gpio.h>
#include <linux/seq_file.h>
#include <miev/mievent.h>

/* Xiaomi MIEV report: value = keyword, dev = the i2c device (its name is sent as I2cAddress) */
int mievent_report(unsigned int eventid, const char *value, struct device *dev)
{
	struct misight_mievent *mievent;
	size_t len = strlen(dev->kobj.name) + 1;
	char *i2c_info;

	i2c_info = kmalloc(len, GFP_KERNEL);
	if (!i2c_info) {
		dev_err(dev, "%s: Failed to allocate memory for i2c_info\n", __func__);
		return -ENOMEM;
	}
	snprintf(i2c_info, len, "%s", dev->kobj.name);
	dev_info(dev, "%s: reg = %s, KeyWord = %s DFS report\n", __func__, i2c_info, value);
	mievent = cdev_tevent_alloc(eventid);
	if (!mievent) {
		dev_err(dev, "%s: Failed to allocate mievent\n", __func__);
		kfree(i2c_info);
		return -ENOMEM;
	}
	cdev_tevent_add_str(mievent, "I2cAddress", i2c_info);
	cdev_tevent_add_str(mievent, "Keyword", value);
	cdev_tevent_write(mievent);
	cdev_tevent_destroy(mievent);
	kfree(i2c_info);

	return 0;
}

/* stock: tail call of tfa->dev_ops.phase_shift */
enum Tfa98xx_Error tfa98xx_set_phase_shift(struct tfa_device *tfa)
{
	return tfa->dev_ops.phase_shift(tfa);
}

/* tfa9912: low power mode bitfield (0x65c0); returns the negated tfa_set_bf() result, 2 on bad state */
enum Tfa98xx_Error tfa9912_update_lpm(struct tfa_device *tfa, int state)
{
	if ((unsigned int)state > 1)
		return Tfa98xx_Error_Bad_Parameter;

	return -tfa_set_bf(tfa, 0x65c0, state);
}

/* aec output select: g3k-guess, only the tfa1 branch (reg 0x04 bit 11) of the stock was transcribed */
int tfa98xx_aec_output(struct tfa_device *tfa, int enable)
{
	unsigned short v = 0;
	enum Tfa98xx_Error err;

	if (tfa->daimap & Tfa98xx_DAI_TDM)
		return 0;
	if (tfa->tfa_family != 1)
		return 0;

	if (tfa->dev_ops.tfa_reg_read) {
		err = tfa->dev_ops.tfa_reg_read(tfa, 4, &v);
		if (err != Tfa98xx_Error_Ok && err != Tfa98xx_Error_Fail)
			return 0;
	}
	v = (v & ~0x0800) | ((enable != 0) << 11);
	if (tfa->dev_ops.tfa_reg_write) {
		err = tfa->dev_ops.tfa_reg_write(tfa, 4, v);
		if (err != Tfa98xx_Error_Ok && err != Tfa98xx_Error_Fail) {
			pr_err("Error setting bf :%d \n", -100 - (int)err);
			return 0;
		}
	}

	return 0;
}

/* --- debugfs (seq_file / simple attribute flavour of the stock) --- */
static int __used tfa98xx_print_device_name(struct seq_file *m, void *v)
{
	struct tfa98xx *tfa98xx = m->private;
	struct tfa_device *tfa = tfa98xx->tfa;

	seq_printf(m, "%s\n", tfaContDeviceName(tfa->cnt, tfa->dev_idx));
	return 0;
}

static int __used tfa98xx_print_is_probus(struct seq_file *m, void *v)
{
	struct tfa98xx *tfa98xx = m->private;
	int probus;

	mutex_lock(&tfa98xx->dsp_lock);
	probus = tfa_cf_enabled(tfa98xx->tfa);
	mutex_unlock(&tfa98xx->dsp_lock);
	seq_printf(m, "%i\n", probus);
	return 0;
}

static int __used tfa98xx_dbgfs_name_open(struct inode *inode, struct file *file)
{
	struct i2c_client *i2c = inode->i_private;

	return single_open(file, tfa98xx_print_device_name, i2c_get_clientdata(i2c));
}

static int __used tfa98xx_dbgfs_probus_open(struct inode *inode, struct file *file)
{
	struct i2c_client *i2c = inode->i_private;
	struct tfa98xx *tfa98xx = i2c_get_clientdata(i2c);

	if (!tfa98xx->tfa->dev_ops.tfa_init) {
		dev_err(&tfa98xx->i2c->dev, "%s: tfa (0x%02x) not fully probed, wrong config.\n",
			__func__, i2c->addr);
		return -EINVAL;
	}

	return single_open(file, tfa98xx_print_is_probus, tfa98xx);
}

static int tfa98xx_g3k_attr_get(void *data, u64 *val, int what)
{
	struct i2c_client *i2c = data;
	struct tfa98xx *tfa98xx = i2c_get_clientdata(i2c);
	int value;

	mutex_lock(&tfa98xx->dsp_lock);
	switch (what) {
	case 0: value = tfa_dev_mtp_get(tfa98xx->tfa, TFA_MTP_OTC); break;
	case 1: value = tfa_dev_mtp_get(tfa98xx->tfa, TFA_MTP_EX); break;
	case 2: value = tfa98xx_get_exttemp(tfa98xx->tfa); break;
	default: value = tfa_get_pga_gain(tfa98xx->tfa); break;
	}
	mutex_unlock(&tfa98xx->dsp_lock);
	if (value < 0)
		return -EIO;
	*val = value;
	return 0;
}

static int tfa98xx_g3k_attr_set(void *data, u64 val, int what)
{
	struct i2c_client *i2c = data;
	struct tfa98xx *tfa98xx = i2c_get_clientdata(i2c);
	int err;

	if (what < 2 && val > 1)
		return -EINVAL;
	mutex_lock(&tfa98xx->dsp_lock);
	switch (what) {
	case 0: err = tfa_dev_mtp_set(tfa98xx->tfa, TFA_MTP_OTC, val); break;
	case 1: err = tfa_dev_mtp_set(tfa98xx->tfa, TFA_MTP_EX, val); break;
	case 2: tfa98xx_set_exttemp(tfa98xx->tfa, (short)val); err = 0; break;
	default: err = tfa_set_pga_gain(tfa98xx->tfa, val); break;
	}
	mutex_unlock(&tfa98xx->dsp_lock);
	return err ? -EIO : 0;
}

#define G3K_ATTR(name, sh, idx) \
static int tfa98xx_dbgfs_##sh##_get(void *d, u64 *v) { return tfa98xx_g3k_attr_get(d, v, idx); } \
static int tfa98xx_dbgfs_##sh##_set(void *d, u64 v) { return tfa98xx_g3k_attr_set(d, v, idx); } \
static int __used tfa98xx_dbgfs_##name##_fops_open(struct inode *inode, struct file *file) \
{ return simple_attr_open(inode, file, tfa98xx_dbgfs_##sh##_get, tfa98xx_dbgfs_##sh##_set, "%llu\n"); }

G3K_ATTR(calib_otc, otc, 0)
G3K_ATTR(calib_mtpex, mtpex, 1)
G3K_ATTR(calib_temp, temp, 2)
G3K_ATTR(pga_gain, pga_gain, 3)

/* g3m: the stock registers these through DEFINE_SIMPLE_ATTRIBUTE file_operations of the same names */
#define G3K_FOPS(name) static const struct file_operations tfa98xx_dbgfs_##name##_fops = { 	.owner = THIS_MODULE, 	.open = tfa98xx_dbgfs_##name##_fops_open, 	.release = simple_attr_release, 	.read = simple_attr_read, 	.write = simple_attr_write, 	.llseek = generic_file_llseek, }
G3K_FOPS(calib_otc);
G3K_FOPS(calib_mtpex);
G3K_FOPS(calib_temp);
G3K_FOPS(pga_gain);

/* --- Xiaomi mixer controls --- */
static int tfa98xx_pcm_id, tfa98xx_miid;	/* stock: .bss+0x64 / .bss+0x68, guarded by tfa98xx_mutex */

static int g3k_info_u32(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	mutex_lock(&tfa98xx_mutex);
	uinfo->count = 1;
	mutex_unlock(&tfa98xx_mutex);
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 0xffffffff;
	return 0;
}

static int __used tfa98xx_info_pcm_id(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	return g3k_info_u32(kcontrol, uinfo);
}

static int __used tfa98xx_info_miid(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	return g3k_info_u32(kcontrol, uinfo);
}

static int __used tfa98xx_get_pcm_id(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	mutex_lock(&tfa98xx_mutex);
	ucontrol->value.integer.value[0] = tfa98xx_pcm_id;
	mutex_unlock(&tfa98xx_mutex);
	return 0;
}

static int __used tfa98xx_set_pcm_id(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	mutex_lock(&tfa98xx_mutex);
	tfa98xx_pcm_id = ucontrol->value.integer.value[0];
	mutex_unlock(&tfa98xx_mutex);
	return 1;
}

static int __used tfa98xx_get_miid(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	mutex_lock(&tfa98xx_mutex);
	ucontrol->value.integer.value[0] = tfa98xx_miid;
	mutex_unlock(&tfa98xx_mutex);
	return 0;
}

static int __used tfa98xx_set_miid(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	mutex_lock(&tfa98xx_mutex);
	tfa98xx_miid = ucontrol->value.integer.value[0];
	mutex_unlock(&tfa98xx_mutex);
	return 1;
}

/* speaker switch gpio ("spk-sw-gpio" in the device tree) */
static int tfa98xx_spksw_gpio_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct tfa98xx *tfa98xx = snd_soc_component_get_drvdata(component);
	int level = gpiod_get_raw_value_cansleep(gpio_to_desc(tfa98xx->spksw_gpio));

	dev_info(&tfa98xx->i2c->dev, "spk_sw level cached = %d, actual = %d\n", tfa98xx->spksw_level, level);
	if (level < 0) {
		ucontrol->value.integer.value[0] = tfa98xx->spksw_level;
	} else {
		ucontrol->value.integer.value[0] = level;
		tfa98xx->spksw_level = level;
	}
	return 0;
}

static int tfa98xx_spksw_gpio_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct tfa98xx *tfa98xx = snd_soc_component_get_drvdata(component);
	int level = ucontrol->value.integer.value[0];

	dev_info(&tfa98xx->i2c->dev, "spk_sw = %d\n", level);
	gpiod_set_raw_value_cansleep(gpio_to_desc(tfa98xx->spksw_gpio), level != 0);
	return 0;
}
