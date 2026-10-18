/* SPDX-License-Identifier: GPL-2.0 */
/*
 * g3l: functions of the Xiaomi stock sia91xx_dlkm (SIA driver 3.1.11-250813) that the Android S template (3.0.7) lacks,
 * written from the disassembly of the OS4 stock. Included into sipa.c (needs the static sipa_resume/suspend/reg_init).
 * The stock module has no BTF: struct offsets (sipa_dev_t) were recovered from the instruction offsets, see sipa_common.h.
 */

#define SIPA_KC_DRVDATA(kcontrol) \
	snd_soc_component_get_drvdata(snd_soc_kcontrol_component(kcontrol))

/* stock: scene < 6 (Playback, Voice, Voip, Receiver, Factory, FM); the template tables only know AUDIO_SCENE_NUM (4) scenes */
static void sipa_scene_set(sipa_dev_t *si_pa, unsigned int scene)
{
	if (scene >= AUDIO_SCENE_NUM) {
		pr_err("[  err][%s] %s: set audio scene val = %u !!! \n", LOG_FLAG, __func__, scene);
		si_pa->scene = AUDIO_SCENE_PLAYBACK;
	} else {
		si_pa->scene = scene;
	}

	if (sipa_chip_is_digital(si_pa->chip_type)) {
		if (si_pa->power_mode) {
			sipa_regmap_set_chip_off(si_pa);
			sipa_reg_init(si_pa);
			sia91xx_smartpa_start(si_pa, SNDRV_PCM_STREAM_PLAYBACK);
		}
	} else if (sipa_is_chip_en(si_pa)) {
		if (!sipa_suspend(si_pa)) {
			usleep_range(5000, 6000);
			sipa_resume(si_pa);
		}
	}
}

static int sipa_pvdd_limit_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = 0;
	return 0;
}

static int sipa_pvdd_limit_set(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	sipa_dev_t *si_pa = SIPA_KC_DRVDATA(kcontrol);

	if (!si_pa) {
		pr_err("[  err][%s] %s: si_pa is NULL\n", LOG_FLAG, __func__);
		return -EINVAL;
	}

	pr_debug("[debug][%s] %s: ucontrol = %ld, channel_num = %d \n",
		LOG_FLAG, __func__, ucontrol->value.integer.value[0], si_pa->channel_num);
	sipa_regmap_set_pvdd_limit(si_pa->regmap, si_pa->chip_type, si_pa->channel_num,
		ucontrol->value.integer.value[0]);
	return 0;
}

static int sipa_status_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	sipa_dev_t *si_pa = SIPA_KC_DRVDATA(kcontrol);

	if (!si_pa) {
		pr_err("[  err][%s] %s: si_pa is NULL\n", LOG_FLAG, __func__);
		return -EINVAL;
	}

	ucontrol->value.integer.value[0] = si_pa->status;
	pr_debug("[debug][%s] %s: ucontrol = 0x%ld, channle = %d \n",
		LOG_FLAG, __func__, ucontrol->value.integer.value[0], si_pa->channel_num);
	return 0;
}

static int sipa_status_set(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	return 0;
}

static int sipa_chip_type_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	sipa_dev_t *si_pa = SIPA_KC_DRVDATA(kcontrol);

	if (!si_pa) {
		pr_err("[  err][%s] %s: si_pa is NULL\n", LOG_FLAG, __func__);
		return -EINVAL;
	}

	ucontrol->value.integer.value[0] = si_pa->chip_type;
	pr_debug("[debug][%s] %s: ucontrol = 0x%ld, channle = %d \n",
		LOG_FLAG, __func__, ucontrol->value.integer.value[0], si_pa->channel_num);
	return 0;
}

static int sipa_chip_type_set(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	return 0;
}

static int sipa_mute_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	sipa_dev_t *si_pa = SIPA_KC_DRVDATA(kcontrol);

	if (!si_pa) {
		pr_err("[  err][%s] %s: si_pa is NULL\n", LOG_FLAG, __func__);
		return -EINVAL;
	}

	ucontrol->value.integer.value[0] = si_pa->mute;
	pr_debug("[debug][%s] %s: ucontrol = 0x%ld, channle = %d \n",
		LOG_FLAG, __func__, ucontrol->value.integer.value[0], si_pa->channel_num);
	return 0;
}

static int sipa_mute_set(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	sipa_dev_t *si_pa = SIPA_KC_DRVDATA(kcontrol);

	if (!si_pa) {
		pr_err("[  err][%s] %s: si_pa is NULL\n", LOG_FLAG, __func__);
		return -EINVAL;
	}

	pr_debug("[debug][%s] %s: ucontrol = %ld, channel_num = %d \n",
		LOG_FLAG, __func__, ucontrol->value.integer.value[0], si_pa->channel_num);
	if (si_pa->power_mode)
		return 0;

	si_pa->mute = (ucontrol->value.integer.value[0] == 1) ? 1 : 0;
	return 0;
}

/*
 * "Sipa reg" bytes control: 12 byte record, buf[8..9] = register address (big endian), buf[10..11] = value (big endian).
 * get: reads the register, set: writes it.
 */
static int sipa_reg_get(struct snd_kcontrol *kcontrol, unsigned int __user *bytes, unsigned int size)
{
	sipa_dev_t *si_pa = SIPA_KC_DRVDATA(kcontrol);
	u8 buf[12] = { 0 };
	unsigned int val = 0;
	unsigned int addr;
	int ret;

	if (!si_pa || !si_pa->regmap) {
		pr_err("[  err][%s] pa or i2c not init\n", __func__);
		return -EFAULT;
	}

	/* g3r: the stock calls _copy_from_user directly with the unclamped size (no check_object_size) */
	if (_copy_from_user(buf, bytes, size)) {
		pr_err("[  err][%s] ,Fail copy to user Ptr:%p\n", __func__, bytes);
		return -EFAULT;
	}

	addr = (buf[8] << 8) | buf[9];
	regmap_read(si_pa->regmap, addr, &val);
	pr_debug("[debug][%s] %s: channle = %d size:%d, [%d, %d] 0x%x, 0x%x, 0x%x, 0x%x, addr:0x%x = 0x%x\n",
		LOG_FLAG, __func__, si_pa->channel_num, size, ((u32 *)buf)[0], ((u32 *)buf)[1],
		buf[8], buf[9], buf[10], buf[11], addr, val);
	buf[10] = (val >> 8) & 0xff;
	buf[11] = val & 0xff;

	ret = 0;
	if (copy_to_user(bytes, buf, sizeof(buf))) {
		pr_err("[  err][%s] ,Fail copy to user Ptr:%p\n", __func__, bytes);
		ret = -EFAULT;
	}

	pr_debug("[debug][%s] %s: channle = %d size:%d\n",
		LOG_FLAG, __func__, si_pa->channel_num, size);
	return ret;
}

static int sipa_reg_set(struct snd_kcontrol *kcontrol, const unsigned int __user *bytes, unsigned int size)
{
	sipa_dev_t *si_pa = SIPA_KC_DRVDATA(kcontrol);
	u8 buf[12] = { 0 };
	unsigned int addr, val;

	if (!si_pa || !si_pa->regmap) {
		pr_err("[  err][%s] pa or i2c not init\n", __func__);
		return -EFAULT;
	}

	if (_copy_from_user(buf, bytes, size)) {
		pr_err("[  err][%s] ,Fail copy to user Ptr:%p\n", __func__, bytes);
		return -EFAULT;
	}

	addr = (buf[8] << 8) | buf[9];
	val = (buf[10] << 8) | buf[11];
	pr_debug("[debug][%s] %s: channle = %d size:%d, [%d,%d] 0x%x,0x%x,0x%x,0x%x addr:0x%x = 0x%x \n",
		LOG_FLAG, __func__, si_pa->channel_num, size, ((u32 *)buf)[0], ((u32 *)buf)[1],
		buf[8], buf[9], buf[10], buf[11], addr, val);
	regmap_write(si_pa->regmap, addr, val);
	return 0;
}

/* i2c register access with 5 retries (5 ms apart), as in the stock */
int sipa_read_reg(struct regmap *regmap, unsigned char reg, unsigned int *val)
{
	unsigned int tmp = 0;
	int retries = 5;
	int ret = regmap_read(regmap, reg, &tmp);

	while (ret < 0) {
		pr_warn("i2c read error, addr: 0x%x, retries left: %d\n", reg, retries);
		if (retries < 0)
			return -1;
		usleep_range(5000, 5000);
		ret = regmap_read(regmap, reg, &tmp);
		retries--;
	}
	*val = tmp;
	return 0;
}

int sipa_write_reg(struct regmap *regmap, unsigned char reg, unsigned int val)
{
	int retries = 5;
	int ret = regmap_write(regmap, reg, val);

	while (ret < 0) {
		pr_warn("i2c write error, addr: 0x%x, retries left: %d\n", reg, retries);
		if (retries < 0)
			return -1;
		usleep_range(5000, 5000);
		ret = regmap_write(regmap, reg, val);
		retries--;
	}
	return 0;
}

/* ---- multi channel API (exported, used by the machine driver) ---- */
int sipa_get_channels(void)
{
	int i, n = 0;

	for (i = 0; i < SIPA_CHANNEL_NUM; i++)
		if (g_sipa_dev[i])
			n++;
	return n;
}
EXPORT_SYMBOL(sipa_get_channels);

int sipa_multi_channel_load_fw(char *fwname)
{
	int i;

	pr_debug("[debug][%s] %s: fwname:%s \n", LOG_FLAG, __func__, fwname);
	mutex_lock(&sipa_mutex);
	for (i = 0; i < SIPA_CHANNEL_NUM; i++)
		if (g_sipa_dev[i])
			sipa_param_load_fw(&g_sipa_dev[i]->pdev->dev, fwname);
	mutex_unlock(&sipa_mutex);
	return 0;
}
EXPORT_SYMBOL(sipa_multi_channel_load_fw);

int sipa_multi_channel_power_on_and_set_scene(unsigned int scene, unsigned char pa_idx)
{
	int i;

	pr_debug("[debug][%s] %s: scene=0x%x, pa_idx:0x%x \n", LOG_FLAG, __func__, scene, pa_idx);
	mutex_lock(&sipa_mutex);
	for (i = 0; i < SIPA_CHANNEL_NUM; i++) {
		sipa_dev_t *si_pa = g_sipa_dev[i];

		if (si_pa && ((pa_idx >> si_pa->channel_num) & 1)) {
			sipa_scene_set(si_pa, scene);
			sipa_resume(si_pa);
		}
	}
	mutex_unlock(&sipa_mutex);
	return 0;
}
EXPORT_SYMBOL(sipa_multi_channel_power_on_and_set_scene);

int sipa_multi_channel_power_off(unsigned char pa_idx)
{
	int i;

	pr_debug("[debug][%s] %s: pa_idx:0x%x \n", LOG_FLAG, __func__, pa_idx);
	mutex_lock(&sipa_mutex);
	for (i = 0; i < SIPA_CHANNEL_NUM; i++) {
		sipa_dev_t *si_pa = g_sipa_dev[i];

		if (si_pa && ((pa_idx >> si_pa->channel_num) & 1))
			sipa_suspend(si_pa);
	}
	mutex_unlock(&sipa_mutex);
	return 0;
}
EXPORT_SYMBOL(sipa_multi_channel_power_off);

int sipa_multi_channel_reg_dump(void)
{
	int i, r;
	unsigned int v[8];

	pr_debug("[debug][%s] %s\n", LOG_FLAG, __func__);
	for (i = 0; i < SIPA_CHANNEL_NUM; i++) {
		sipa_dev_t *si_pa = g_sipa_dev[i];

		if (!si_pa)
			continue;
		memset(v, 0, sizeof(v));
		for (r = 0; r < 8; r++) {
			int ret = regmap_read(si_pa->regmap, r, &v[r]);

			if (ret)
				pr_info("%s: read fail: reg addr = %02x, ret = %d\n", "sipa_multi_91xx_reg_dump", r, ret);
		}
		pr_info("%s: ch:%d i2caddr:0x%x 0x00=0x%x 0x01=0x%x 0x02=0x%x 0x03=0x%x 0x04=0x%x 0x05=0x%x 0x06=0x%x 0x07=0x%x\n",
			"sipa_multi_91xx_reg_dump", si_pa->channel_num, si_pa->client->addr,
			v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7]);
	}
	return 0;
}
EXPORT_SYMBOL(sipa_multi_channel_reg_dump);

/* stock: suspends everything except the "compat" chip types (template enum: 8100X/81X9/8152X) */
void sipa_i2c_shutdown(struct i2c_client *client)
{
	sipa_dev_t *si_pa;

	pr_info("[ info][%s] %s: running \n", LOG_FLAG, __func__);
	if (!client) {
		pr_err("[  err][%s] %s: client is NULL\n", LOG_FLAG, __func__);
		return;
	}
	si_pa = dev_get_drvdata(&client->dev);
	if (!si_pa) {
		pr_err("[  err][%s] %s: si_pa is NULL \n", LOG_FLAG, __func__);
		return;
	}
	if (si_pa->chip_type != CHIP_TYPE_SIA8100X && si_pa->chip_type != CHIP_TYPE_SIA81X9 &&
	    si_pa->chip_type != CHIP_TYPE_SIA8152X)
		sipa_suspend(si_pa);
}
EXPORT_SYMBOL(sipa_i2c_shutdown);

/* g3o: moved here from sia91xx_g3k.c so that they are local as in the stock */
static int sia91xx_spksw_gpio_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	sipa_dev_t *si_pa = snd_soc_component_get_drvdata(component);
	int level = gpiod_get_raw_value_cansleep(gpio_to_desc(si_pa->spksw_gpio));

	dev_info(&si_pa->client->dev, "spksw level cached = %d, actual = %d\n", si_pa->spksw_level, level);
	if (level < 0) {
		ucontrol->value.integer.value[0] = si_pa->spksw_level;
	} else {
		ucontrol->value.integer.value[0] = level;
		si_pa->spksw_level = level;
	}
	return 0;
}

static int sia91xx_spksw_gpio_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	sipa_dev_t *si_pa = snd_soc_component_get_drvdata(component);
	int level = ucontrol->value.integer.value[0];

	dev_info(&si_pa->client->dev, "spksw = %d\n", level);
	gpiod_set_raw_value_cansleep(gpio_to_desc(si_pa->spksw_gpio), level != 0);
	return 0;
}
