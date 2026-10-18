/*
 * Copyright (C) 2018, SI-IN, Yun Shi (yun.shi@si-in.com).
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */


#define DEBUG
#define LOG_FLAG	"sipa_driver"


#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/firmware.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <linux/init.h>
#include <linux/dma-mapping.h>
#include <linux/gameport.h>
#include <linux/moduleparam.h>
#include <linux/mutex.h>
//#include <linux/wakelock.h>
#include <linux/cdev.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pm_runtime.h>
#include <linux/delay.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>
#include <sound/soc-dapm.h>
#include <sound/core.h>
#include <linux/time.h>
#include <linux/fs.h>
#include <asm/uaccess.h>
#include <linux/cpufreq.h>
#include <linux/syscalls.h>
#include <linux/regmap.h>
#include <linux/module.h>
#include <linux/version.h>

#include "sipa_common.h"
#include "sipa_regmap.h"
#include "sipa_tuning_if.h"
#include "sia91xx_common.h"

#ifdef SIPA_TUNING
#include "sipa_socket.h"
#endif

#ifdef PLATFORM_TYPE_MTK
#include "mtk-sp-spk-amp.h"
#endif

#include "sipa_parameter.h"

#define SIPA_NAME							("sipa")
#define SIPA_I2C_NAME						(SIPA_NAME)

#define DISTINGUISH_CHIP_TYPE
//#define ALGO_SWITCH_EN

#define SIPA_CMD_POWER_ON					(1)
#define SIPA_CMD_POWER_OFF					(2)
#define SIPA_CMD_GET_MODE					(3)
#define SIPA_CMD_SET_MODE					(4)
#define SIPA_CMD_GET_REG					(5)
#define SIPA_CMD_SET_REG					(6)

#define SIPA_CMD_WRITE_BYTE					(100)
#define SIPA_CMD_SET_OWI_DELAY				(101)
#define SIPA_CMD_CLR_ERROR					(200)
#define SIPA_CMD_SOCKET_OPEN				(400)
#define SIPA_CMD_SOCKET_CLOSE				(401)
#define SIPA_CMD_VDD_SET_OPEN				(402)
#define SIPA_CMD_VDD_SET_CLOSE				(403)
#define SIPA_CMD_TIMER_TASK_OPEN			(404)
#define SIPA_CMD_TIMER_TASK_CLOSE			(405)
#ifdef ALGO_SWITCH_EN
#define SIPA_CMD_ALGORITHM_OPEN				(406)
#define SIPA_CMD_ALGORITHM_CLOSE			(407)
#define SIPA_CMD_ALGORITHM_STATUS			(408)
#endif

#define SIPA_CMD_TEST_CREATE_FW_FILE		(600)
#define SIPA_CMD_TEST_LOAD_FW				(601)
#define SIPA_CMD_TEST_FW_SHOW				(602)
#define SIPA_CMD_TEST_SET_SPK_CAL			(603)
#define SIPA_CMD_TEST_SHOW_MONITOR			(604)
#define SIPA_CMD_TEST_SET_AUDIO_EFFECTS_EN	(605)
#define SIPA_CMD_TEST_GET_AUDIO_EFFECTS_EN	(606)
#define SIPA_CMD_TEST_DEBUG_PRINT_TO_DSP	(607)
#define SIPA_CMD_TEST_WRITE_FILE			(610)
#define SIPA_CMD_CAL_SPK_EXECUTE			(611)


#define SIPA_CMD_GET_RST_PIN				(612)
#define SIPA_CMD_SET_RST_PIN				(613)

// #define SIPA_CMD_SHOW_ALL_REG			(614)
#define SIPA_CMD_SET_MUTE_MODE				(615)
#define SIPA_CMD_WRITE_SPK_R0           	(616)
#define SIPA_CMD_MANUAL_CONFIG_R0       	(617)
#define SIPA_CMD_GET_RDC_TEMP           	(618)
#define SIPA_CMD_WRITE_SPK_MODE_PARAM   	(619)
#define SIPA_CMD_CLOSE_TEMP_LIMITER			(620)
#define SIPA_CMD_CLOSE_F0_TRACKING			(621)

#define SIPA_CMD_TEST						(777)

#ifdef SIA91XX_TYPE
#define SIA81XX_ENABLE_LEVEL				(0)
#define SIA81XX_DISABLE_LEVEL				(1)
#else
#define SIA81XX_ENABLE_LEVEL				(1)
#define SIA81XX_DISABLE_LEVEL				(0)
#endif
/* 10us > pulse width > 0.75us */
#define MIN_OWI_PULSE_GAP_TIME_US			(1)
#define MAX_OWI_PULSE_GAP_TIME_US			(160)
#define MAX_OWI_RETRY_TIMES					(10)
#define MIN_OWI_MODE						(1)
#define MAX_OWI_MODE						(35)	/* g3r: stock compare: valid modes 1 .. 35 (was 16) */
#define DEFAULT_OWI_MODE					(6)
/* OWI_POLARITY 0 : pulse level == high, 1 : pulse level == low */
#define OWI_POLARITY						(SIA81XX_DISABLE_LEVEL)


//#define OWI_SUPPORT_WRITE_DATA
#ifdef OWI_SUPPORT_WRITE_DATA
#define OWI_DATA_BIG_END
#endif

/* error list */
/* pulse width time out */
#define EPTOUT								(100)
/* pulse electrical level opposite with the polarity */
#define EPOLAR								(101)

#define SIXTH_SIPA_RX_MODULE				(0x1000E900)/* module id */
#define SIXTH_SIPA_RX_ENABLE				(0x1000EA01)/* parameter id */

static ssize_t sipa_cmd_show(struct device *cd,
	struct device_attribute *attr, char *buf);
static ssize_t sipa_cmd_store(struct device *cd,
	struct device_attribute *attr, const char *buf, size_t len);
static DEVICE_ATTR_RW(sipa_cmd);

#ifdef DISTINGUISH_CHIP_TYPE
static ssize_t sipa_device_show(struct device *cd,
	struct device_attribute *attr, char *buf);
static ssize_t sipa_device_store(struct device *cd,
	struct device_attribute *attr, const char *buf, size_t len);

static int check_sipa_status(sipa_dev_t *si_pa);
static DEVICE_ATTR_RW(sipa_device);
#endif

/* stock rodata table (26 names, index == chip type) */
static const char *support_chip_type_name_table[] = {
	"sia8101",
	"sia8109",
	"sia8152",
	"sia8152s",
	"sia8159",
	"sia8159a",
	"sia9195",
	"sia9196",
	"sia9175",
	"sia8001",
	"sia8102",
	"sia9177",
	"sia9255",
	"sia8100x",
	"sia81x9",
	"sia8152x",
	"sia917x",
	"sia8150",
	"sia815T",
	"sia9187",
	"sia5118",
	"sia9197x",
	"sia8132",
	"sia5118s",
	"sia5118x",
	"sia9306"
};

static inline bool sipa_chip_is_digital(unsigned int t)
{
	return SIPA_TYPE_IN(t, 0x19, 0x3b919c0);
}

static sipa_dev_t *g_default_sia_dev;
static sipa_dev_t *g_sipa_dev[SIPA_CHANNEL_NUM];
uint32_t g_dyn_ud_vdd_port;	/* stock .bss+0x28, global */
static DEFINE_MUTEX(sipa_mutex);

static void sipa_resume(
	struct sipa_dev_s *si_pa);
static int sipa_suspend(
	struct sipa_dev_s *si_pa);

/********************************************************************
 * si_pa misc option
 ********************************************************************/
/********************************************************************
 * end - si_pa misc option
 ********************************************************************/

/********************************************************************
 * si_pa GPIO option
 ********************************************************************/
static __inline void gpio_flipping(
	int pin,
	s64 *intervel_ns)
{
	static struct timespec64 cur_time, last_time, temp_time;

	gpio_set_value(pin, !gpio_get_value(pin));

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0))
	ktime_get_real_ts64(&cur_time);
#else
	getnstimeofday64(&cur_time);
#endif
	temp_time = timespec64_sub(cur_time, last_time);
	if (NULL != intervel_ns)
		*intervel_ns = timespec64_to_ns(&temp_time);
	last_time = cur_time;
}

static void sipa_scene_set(sipa_dev_t *si_pa, unsigned int scene);
/* g3p: rewritten from the stock disassembly (types 9/10 = sia8001/sia8102 are chosen by the id pin level) */
static noinline void distinguish_chip_type(sipa_dev_t *si_pa)
{
#ifdef DISTINGUISH_CHIP_TYPE
	unsigned int en_dyn_id;
	int ret;

	if (CHIP_TYPE_UNKNOWN == si_pa->chip_type) {
		pr_info("[ info][%s] %s: there is no si_pa device \n",
			LOG_FLAG, __func__);
		return;
	}

	en_dyn_id = si_pa->en_dyn_id;
	ret = check_sipa_status(si_pa);
	if (1 == en_dyn_id) {
		if (1 == ret)
			si_pa->chip_type = CHIP_TYPE_SIA8001;
		else if (0 == check_sipa_status(si_pa))
			si_pa->chip_type = CHIP_TYPE_SIA8102;
	} else if (0 != ret) {
		pr_info("[ info][%s] %s:  failed to check_sipa_status\n",
			LOG_FLAG, __func__);
		return;
	}

	device_create_file(&si_pa->pdev->dev, &dev_attr_sipa_device);
	pr_info("[ info][%s] %s: sipa device is available, chip_type = %d .\n",
		LOG_FLAG, __func__, si_pa->chip_type);
#endif
}
static __inline int gpio_produce_one_pulse(
	int pin,
	int polarity,
	unsigned int width_us,
	s64 *real_duty_ns,
	s64 *real_idle_ns)
{
	if (polarity != gpio_get_value(pin)) {
		pr_err("[  err][%s] %s: EPOLAR \n", LOG_FLAG, __func__);
		return -EPOLAR;
	}

	gpio_flipping(pin, real_idle_ns);
	udelay(width_us);
	gpio_flipping(pin, real_duty_ns);

	return 0;
}

static __inline int __gpio_produce_one_pulse_cycle(
	int pin,
	int polarity,
	unsigned int duty_time_us,
	unsigned int idle_time_us,
	s64 *real_duty_ns,
	s64 *real_idle_ns)
{
	int ret = gpio_produce_one_pulse(pin, polarity,
		duty_time_us, real_duty_ns, real_idle_ns);
	if (0 == ret) /* if pulse produce suceess */
		udelay(idle_time_us);

	return ret;
}

static __inline int gpio_produce_one_pulse_cycle(
	int pin,
	int polarity,
	unsigned int delay_us,
	s64 *real_duty_ns,
	s64 *real_idle_ns)
{
	return __gpio_produce_one_pulse_cycle(pin, polarity,
		delay_us, delay_us, real_duty_ns, real_idle_ns);
}

static __inline int gpio_produce_pulse_cycles(
	int pin,
	int polarity,
	unsigned int delay_us,
	unsigned int cycles,
	s64 *max_real_delay_ns) {

	s64 real_duty_ns = 0, real_idle_ns = 0, max_pulse_time = 0;
	bool is_fst_pulse = true;
	int ret = 0;

	if (0 == pin) {
		pr_err("[  err][%s] %s: 0 == pin \n", LOG_FLAG, __func__);
		return -EINVAL;
	}

	while (cycles) {
		cycles--;

		ret = gpio_produce_one_pulse_cycle(pin, polarity,
			delay_us, &real_duty_ns, &real_idle_ns);
		if (0 != ret)
			break;

		/* monitor pulse with overtime */
		if (false == is_fst_pulse) {
			max_pulse_time =
				max_pulse_time > real_idle_ns ? max_pulse_time : real_idle_ns;
		}
		max_pulse_time =
			max_pulse_time > real_duty_ns ? max_pulse_time : real_duty_ns;
		is_fst_pulse = false;

	}

	if (NULL != max_real_delay_ns)
		*max_real_delay_ns = max_pulse_time;

	return ret;
}
/********************************************************************
 * end - si_pa GPIO option
 ********************************************************************/


/********************************************************************
 * si_pa owi option
 ********************************************************************/
#ifdef OWI_SUPPORT_WRITE_DATA
static void sia81xx_owi_calc_bit_duty_ns(
	s64 duty_ns,
	s64 idle_ns,
	s64 *duty_time_ns,
	s64 *idle_time_ns,
	const char bit)
{
	if (0 == !!(bit & (char)0x01)) {
		*duty_time_ns = idle_ns;
		*idle_time_ns = duty_ns;
	} else {
		*duty_time_ns = duty_ns;
		*idle_time_ns = idle_ns;
	}
}

static void sia81xx_owi_calc_bit_duty_us(
	u32 duty_us,
	u32 idle_us,
	u32 *duty_time_us,
	u32 *idle_time_us,
	const char bit)
{
	s64 duty_time_ns, idle_time_ns;

	sia81xx_owi_calc_bit_duty_ns((s64)duty_us * NSEC_PER_USEC,
		(s64)idle_us * NSEC_PER_USEC, &duty_time_ns, &idle_time_ns, bit);

	*duty_time_us = duty_time_ns / NSEC_PER_USEC;
	*idle_time_us = idle_time_ns / NSEC_PER_USEC;
}

static int sia81xx_owi_write_one_bit(
	struct sipa_dev_s *si_pa,
	const char bit,
	s64 *real_duty_ns,
	s64 *real_idle_ns)
{
	int ret = 0;
	unsigned int duty_time_us, idle_time_us;

	sia81xx_owi_calc_bit_duty_us(si_pa->owi_delay_us,
		MIN_OWI_PULSE_GAP_TIME_US, &duty_time_us, &idle_time_us, bit);

	ret = __gpio_produce_one_pulse_cycle(si_pa->owi_pin,
		si_pa->owi_polarity, duty_time_us, idle_time_us,
		real_duty_ns, real_idle_ns);

	return ret;
}

static int sia81xx_owi_start_write_byte(
	struct sipa_dev_s *si_pa)
{
	/* before start write, gpio level must be opposite with OWI_POLARITY */
	if (gpio_get_value(si_pa->owi_pin) == si_pa->owi_polarity) {
		pr_err("[  err][%s] %s: EPOLAR !!! \n", LOG_FLAG, __func__);
		return -EPOLAR;
	}

	/* *******************************************************************
	 * for reset flipping timer,
	 * otherwise only need gpio_set_value(dev->owi_pin, OWI_POLARITY);
	 * *******************************************************************/
	gpio_flipping(si_pa->owi_pin, NULL);

	return 0;
}

static void sia81xx_owi_end_write_byte(
	struct sipa_dev_s *si_pa,
	s64 *real_idle_ns)
{
	gpio_flipping(si_pa->owi_pin, real_idle_ns);
}

static s64 sia81xx_owi_calc_deviation_ns(
	struct sipa_dev_s *dev,
	s64 duty_time_ns,
	s64 idle_time_ns,
	const char bit)
{
	s64 deviation = 0;

	dev->err_info.owi_max_gap =
		dev->err_info.owi_max_gap > duty_time_ns ?
		dev->err_info.owi_max_gap : duty_time_ns;

	dev->err_info.owi_max_gap =
		dev->err_info.owi_max_gap > idle_time_ns ?
		dev->err_info.owi_max_gap : idle_time_ns;

	sia81xx_owi_calc_bit_duty_ns(duty_time_ns, idle_time_ns,
		&duty_time_ns, &idle_time_ns, bit);

	if (duty_time_ns < idle_time_ns)
		deviation = idle_time_ns - duty_time_ns;

	return deviation;
}

static int sia81xx_owi_write_one_byte(
	struct sipa_dev_s *si_pa,
	const char data)
{
	int ret = 0, i = 0;
	s64 real_duty_ns = 0, real_idle_ns = 0, last_real_duty_ns = 0;
	s64 max_deviation_ns = 0, cur_deviation_ns = 0;
	char last_bit = 0;
	unsigned long flags;

	spin_lock_irqsave(&si_pa->owi_lock, flags);

	ret = sia81xx_owi_start_write_byte(si_pa);
	if (0 != ret)
		goto owi_end_write_one_byte;

#ifdef OWI_DATA_BIG_END
	for (i = 7; i >= 0; i--) {
		ret = sia81xx_owi_write_one_bit(si_pa, (data >> i),
			&real_duty_ns, &real_idle_ns);
		if (0 != ret)
			goto owi_end_write_one_byte;

		/* when second bit has been sent,
		 * can be get first bit's real idle time */
		if (7 > i) {
			cur_deviation_ns = sia81xx_owi_calc_deviation_ns(si_pa,
				last_real_duty_ns, real_idle_ns, last_bit);
			max_deviation_ns = max_deviation_ns > cur_deviation_ns ?
				max_deviation_ns : cur_deviation_ns;

			/* occur write error */
			if (0 != cur_deviation_ns)
				break;
		}
		last_bit = (data >> i);
		last_real_duty_ns = real_duty_ns;
	}
#else
	for (i = 0; i < 8; i++) {
		ret = sia81xx_owi_write_one_bit(si_pa, (data >> i),
			&real_duty_ns, &real_idle_ns);
		if (0 != ret)
			goto owi_end_write_one_byte;

		/* when second bit has been sent,
		 * can be get first bit's real idle time */
		if (0 < i) {
			cur_deviation_ns = sia81xx_owi_calc_deviation_ns(si_pa,
				last_real_duty_ns, real_idle_ns, last_bit);
			max_deviation_ns = max_deviation_ns > cur_deviation_ns ?
				max_deviation_ns : cur_deviation_ns;

			/* occur write error */
			if (0 != cur_deviation_ns)
				break;
		}
		last_bit = (data >> i);
		last_real_duty_ns = real_duty_ns;
	}
#endif

	/* end write byte, and get last bit real idle time */
	sia81xx_owi_end_write_byte(si_pa, &real_idle_ns);

	/* no write error */
	if (0 == cur_deviation_ns) {
		/* then can be get the last bit's real idle time */
		cur_deviation_ns = sia81xx_owi_calc_deviation_ns(si_pa,
			last_real_duty_ns, real_idle_ns, last_bit);
		max_deviation_ns = max_deviation_ns > cur_deviation_ns ?
			max_deviation_ns : cur_deviation_ns;
	}

owi_end_write_one_byte:

	spin_unlock_irqrestore(&si_pa->owi_lock, flags);

	if (0 != cur_deviation_ns)
		ret = -EPTOUT;

	if (0 != ret) {
		if (-EPOLAR == ret)
			/* record polarity err */
			si_pa->err_info.owi_polarity_err_cnt++;
		else
			/* record wirte err */
			si_pa->err_info.owi_write_err_cnt++;
	}

	/* record history max deviation time */
	si_pa->err_info.owi_max_deviation =
		si_pa->err_info.owi_max_deviation > max_deviation_ns ?
		si_pa->err_info.owi_max_deviation : max_deviation_ns;

	return ret;
}

static int sipa_owi_write_data(
	struct sipa_dev_s *si_pa,
	unsigned int len,
	const char *buf)
{
	int ret = 0, i = 0, retry = 0;

	si_pa->err_info.owi_write_data_cnt++;

	for (i = 0; i < len; i++) {

		retry = 0;

		while (retry < MAX_OWI_RETRY_TIMES) {

			ret = sia81xx_owi_write_one_byte(si_pa, buf[i]);
			if (0 == ret)
				break;

			/* must be ust msleep, do not use mdelay */
			msleep(1);

			retry++;
		}

		/* record max retry time */
		si_pa->err_info.owi_max_retry_cnt =
			si_pa->err_info.owi_max_retry_cnt > retry ?
			si_pa->err_info.owi_max_retry_cnt : retry;

		if (retry >= MAX_OWI_RETRY_TIMES) {
			si_pa->err_info.owi_write_data_err_cnt++;
			return -EPTOUT;
		}
	}

	return ret;
}
#endif

/* g3l: owi code kept for the analog sia81xx chips (stock calls it at run time) */
static void sia81xx_set_owi_polarity(
	struct sipa_dev_s *si_pa)
{
	gpiod_set_raw_value(gpio_to_desc(si_pa->owi_pin), si_pa->owi_polarity);
}
static int __sipa_owi_write_mode(
	struct sipa_dev_s *si_pa,
	unsigned int cycles)
{
	int ret = 0;
	s64 max_flipping_ns = 0, last_flipping_ns = 0;
	unsigned long flags;

	/* g3r: the stock sets the polarity and waits OUTSIDE the lock (usleep_range 5..6 ms instead of udelay(1500)) */
	sia81xx_set_owi_polarity(si_pa);
	usleep_range(5000, 6000);

	spin_lock_irqsave(&si_pa->owi_lock, flags);

	/* last pulse only flipping once, so pulses = cycles - 1 */
	ret = gpio_produce_pulse_cycles(si_pa->owi_pin, si_pa->owi_polarity,
			si_pa->owi_delay_us, cycles - 1, &max_flipping_ns);
	/* last pulse */
	gpio_flipping(si_pa->owi_pin, &last_flipping_ns);

	spin_unlock_irqrestore(&si_pa->owi_lock, flags);

	if (0 != ret) {
		if (-EPOLAR == ret)
			/* record polarity err */
			si_pa->err_info.owi_polarity_err_cnt++;
		else
			/* record wirte err */
			si_pa->err_info.owi_write_err_cnt++;
		return ret;
	}

	/* if cycles == 1, then only flipping gpio, and do not care flipping time */
	if (0 == (cycles - 1))
		return 0;

	/* get max flipping time */
	max_flipping_ns = max_flipping_ns > last_flipping_ns ?
		max_flipping_ns : last_flipping_ns;

	/* record history max flipping time */
	si_pa->err_info.owi_max_gap =
		si_pa->err_info.owi_max_gap > max_flipping_ns ?
		si_pa->err_info.owi_max_gap : max_flipping_ns;

	if ((MAX_OWI_PULSE_GAP_TIME_US * NSEC_PER_USEC) <= max_flipping_ns) {
		/* record wirte err */
		si_pa->err_info.owi_write_err_cnt++;
		return -EPTOUT;
	}

	return 0;
}

static noinline int sipa_owi_write_mode(
	struct sipa_dev_s *si_pa,
	unsigned int mode)
{
	unsigned int retry = 0;

	if ((MAX_OWI_MODE < mode) || (MIN_OWI_MODE > mode))
		return -EINVAL;

	si_pa->err_info.owi_set_mode_cnt++;

	while (retry < MAX_OWI_RETRY_TIMES) {

		if (0 == __sipa_owi_write_mode(si_pa, mode))
			break;

		usleep_range(1000, 2000);	/* g3r: stock (was msleep(1)) */

		retry++;
	}

	/* record max retry time */
	si_pa->err_info.owi_max_retry_cnt =
		si_pa->err_info.owi_max_retry_cnt > retry ?
		si_pa->err_info.owi_max_retry_cnt : retry;

	if (retry >= MAX_OWI_RETRY_TIMES) {
		si_pa->err_info.owi_set_mode_err_cnt++;
		return -EPTOUT;
	}

	return 0;
}

/********************************************************************
* end - si_pa owis option
********************************************************************/


/********************************************************************
 * si_pa chip option
 ********************************************************************/
static bool is_chip_type_supported(unsigned int chip_type)
{
	if (chip_type >= ARRAY_SIZE(support_chip_type_name_table))
		return false;

	return true;
}

/* g3r: from the stock disassembly: the SIA91xx family (mask 0x3b919c0) and chip types 4/5/18 (0x40030) are enabled only through
 * the regmap (the latter additionally need the reset line high), every other type (and any type > 25) is the reset line level */
static __always_inline void sipa_get_rst_value(sipa_dev_t *si_pa);

static noinline bool sipa_is_chip_en(sipa_dev_t *si_pa)
{
	if (0 == si_pa->disable_pin) {
		if (si_pa->chip_type <= 0x19) {
			if (SIPA_TYPE_IN(si_pa->chip_type, 0x19, 0x3b919c0))
				goto by_regmap;
			if (SIPA_TYPE_IN(si_pa->chip_type, 0x19, 0x40030)) {
				if (1 == gpiod_get_raw_value(gpio_to_desc(si_pa->rst_pin)))
					goto by_regmap;
				return false;
			}
		}
		return 1 == gpiod_get_raw_value(gpio_to_desc(si_pa->rst_pin));
	}

by_regmap:
	return sipa_regmap_get_chip_en(si_pa) & 1;
}

int sipa_reg_init(
	struct sipa_dev_s *si_pa)
{
	if (NULL == si_pa->client)
		return 0;

	if (CHIP_TYPE_SIA8101 == si_pa->chip_type
		&& 0 != si_pa->channel_num) {
		sipa_regmap_defaults(g_default_sia_dev->regmap,
			si_pa->chip_type, si_pa->scene, si_pa->channel_num);
	} else {
		sipa_regmap_defaults(si_pa->regmap,
			si_pa->chip_type, si_pa->scene, si_pa->channel_num);
	}

	usleep_range(100, 200);
	if (0 == si_pa->disable_pin)
		sipa_get_rst_value(si_pa);
	if (0 != sipa_regmap_check_chip_id(si_pa->regmap,
			si_pa->channel_num, si_pa->chip_type)) {
		pr_err("[  err][%s] %s: sipa_regmap_check_chip_id error !!! \n",
			LOG_FLAG, __func__);
	}

	return 0;
}

/* g3p: rewritten from the stock disassembly. In the stock sipa_resume is void (no return value), the owi mode is the
 * per-scene table owi_mode[scene] (+0x1f4), the reset line level is 0 for the SIA91xx chip types (mask 0x3b919c0)
 * and 1 for the others, gpiod_*_raw_value + usleep_range replace the template's spinlock + mdelay. */
static void sipa_resume(
	struct sipa_dev_s *si_pa)
{
	int default_sia_is_open = 0;

	if (NULL == si_pa) {
		pr_err("[  err][%s] %s: si_pa is NULL !!! \n", LOG_FLAG, __func__);
		return;
	}

	pr_debug("[debug][%s] %s: si_pa->chip_type = %d, channel:%d, running \n",
		LOG_FLAG, __func__, si_pa->chip_type, si_pa->channel_num);

	if (si_pa->chip_type > 0x19 || sipa_is_chip_en(si_pa))
		return;

	if (0 == si_pa->disable_pin) {
		int level, rst_val;

		if (0 == si_pa->chip_type && 0 != si_pa->channel_num) {
			if (NULL == g_default_sia_dev) {
				pr_err("[  err][%s] %s: g_default_sia_dev == NULL !!! \n",
					LOG_FLAG, __func__);
				goto err_sipa_resume;
			}
			default_sia_is_open = gpiod_get_raw_value(
				gpio_to_desc(g_default_sia_dev->rst_pin));
			if (1 == default_sia_is_open)
				sipa_suspend(g_default_sia_dev);
		}

		if (SIPA_TYPE_IN(si_pa->chip_type, 0xd, 0x2600)) {
			/* power up chip */
			if (0 != sipa_owi_write_mode(si_pa, si_pa->owi_mode[si_pa->scene]))
				goto err_sipa_resume;
		}

		level = SIPA_TYPE_IN(si_pa->chip_type, 0x19, 0x3b919c0) ?
			0 : 1;	/* stock: 0 for the SIA91xx types, 1 for the analog ones */
		gpiod_set_raw_value(gpio_to_desc(si_pa->rst_pin), level);
		usleep_range(1000, 1200);

		if (14 == si_pa->chip_type || 1 == si_pa->chip_type)
			msleep(39);	/* for sia8109 gain rising. */

		rst_val = gpiod_get_raw_value(gpio_to_desc(si_pa->rst_pin));
		pr_debug("[debug][%s] %s: channel = %d, rst_pin_val = %d, owi_mode = %d \n",
			LOG_FLAG, __func__, si_pa->channel_num, rst_val,
			si_pa->owi_mode[si_pa->scene]);
	}

	if (20 == si_pa->chip_type)
		gpiod_set_raw_value(gpio_to_desc(si_pa->boost_pin), 1);

	sipa_reg_init(si_pa);
	if (sipa_regmap_set_chip_on(si_pa))
		si_pa->power_mode = 1;
	sipa_regmap_check_trimming(si_pa);

	if (0 == si_pa->disable_pin && 0 == si_pa->chip_type &&
		1 == default_sia_is_open && 0 != si_pa->channel_num)
		sipa_resume(g_default_sia_dev);

	return;

err_sipa_resume:
	pr_err("[  err][%s] %s: error !!! \n", LOG_FLAG, __func__);
}

/* stock rodata table (index chip_type - 6, 20 entries): reset line level written by suspend for the types of the mask 0xee467 */
static const int sipa_suspend_rst_level[20] = {
	1, 1, 1, 0, 0, 1, 1, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 1
};

static int sipa_suspend(
	struct sipa_dev_s *si_pa)
{
	if (NULL == si_pa) {
		pr_err("[  err][%s] %s: si_pa is NULL !!! \n", LOG_FLAG, __func__);
		return -ENODEV;
	}

	pr_debug("[debug][%s] %s: running channel:%d\n",
		LOG_FLAG, __func__, si_pa->channel_num);

	if (si_pa->chip_type <= 0x19 && sipa_is_chip_en(si_pa)) {
		unsigned int idx = si_pa->chip_type - 6;
		bool digital = idx < 20 && ((0xee467u >> idx) & 1);

		si_pa->power_mode = 0;
		if (!sipa_regmap_set_chip_off(si_pa) && digital) {
			gpiod_set_raw_value(gpio_to_desc(si_pa->rst_pin),
				sipa_suspend_rst_level[idx]);
			usleep_range(5000, 6000);
		}

		if (!digital) {
			if (0 == si_pa->disable_pin) {
				/* power off chip */
				gpiod_set_raw_value(gpio_to_desc(si_pa->rst_pin), 0);
				usleep_range(5000, 6000);
			}
		} else if (20 == si_pa->chip_type) {
			gpiod_set_raw_value(gpio_to_desc(si_pa->boost_pin), 0);
		}
	}

	return 0;
}

/* g3r: the stock has neither sipa_reboot nor sipa_set_mode as functions: both are inlined into sipa_cmd_store
 * (the strings keep the name "sipa_set_mode"); chip mask 0x2600 = owi chips 9, 10, 13; valid mode 1 .. 35 [stock compare] */
static __always_inline int sipa_set_mode(
	struct sipa_dev_s *si_pa,
	unsigned int mode)
{
	int ret = 0;

	if (si_pa->chip_type > 13 || !((1U << si_pa->chip_type) & 0x2600)) {
		pr_info("[ info][%s] %s: needn't set mode \n", LOG_FLAG, __func__);
		return ret;
	}

	if (mode == 0 || mode >= 36) {
		pr_err("[  err][%s] %s: error mode = %u !!! \n",
			LOG_FLAG, __func__, mode);
		return -EINVAL;
	}

	si_pa->owi_mode[si_pa->scene] = mode;

	ret = sipa_suspend(si_pa);
	if (0 == ret) {
		usleep_range(5000, 6000);
		sipa_resume(si_pa);
	}

	return ret;
}


#ifdef ALGO_SWITCH_EN
static int sipa_algo_en_write(
	struct sipa_dev_s *si_pa,
	int32_t enable)
{
	unsigned long cal_handle = 0;

	pr_debug("[debug][%s] %s: tuning port = %d \n",
		LOG_FLAG, __func__, si_pa->dyn_ud_vdd_port);

	if (NULL == tuning_if_opt.open || NULL == tuning_if_opt.write) {
		pr_err("[  err][%s] %s: NULL == tuning_if_opt.open/write \n",
			LOG_FLAG, __func__);
		return -EINVAL;
	}

	cal_handle = tuning_if_opt.open(si_pa->dyn_ud_vdd_port);
	if (0 == cal_handle) {
		pr_err("[  err][%s] %s: NULL == cal_handle \n",
			LOG_FLAG, __func__);
		return -EINVAL;
	}

	return tuning_if_opt.write(cal_handle,
		SIXTH_SIPA_RX_MODULE,
		SIXTH_SIPA_RX_ENABLE,
		sizeof(enable), (uint8_t *)&enable);
}

static int sipa_algo_en_read(
	struct sipa_dev_s *si_pa,
	int32_t *enable)
{
	unsigned long cal_handle = 0;

	pr_debug("[debug][%s] %s: tuning port = %d \n",
		LOG_FLAG, __func__, si_pa->dyn_ud_vdd_port);

	if (NULL == tuning_if_opt.open || NULL == tuning_if_opt.write) {
		pr_err("[  err][%s] %s: NULL == tuning_if_opt.open/write \n",
			LOG_FLAG, __func__);
		return -EINVAL;
	}

	cal_handle = tuning_if_opt.open(si_pa->dyn_ud_vdd_port);
	if (0 == cal_handle) {
		pr_err("[  err][%s] %s: NULL == cal_handle \n",
			LOG_FLAG, __func__);
		return -EINVAL;
	}

	return tuning_if_opt.read(cal_handle,
		SIXTH_SIPA_RX_MODULE,
		SIXTH_SIPA_RX_ENABLE,
		sizeof(*enable), (uint8_t *)enable);
}
#endif
/********************************************************************
 * end - si_pa chip option
 ********************************************************************/

#ifdef DISTINGUISH_CHIP_TYPE
/********************************************************************
 * device attr option
 ********************************************************************/
static ssize_t sipa_device_show(
	struct device *cd,
	struct device_attribute *attr,
	char *buf)
{
	return 0;
}

static ssize_t sipa_device_store(
	struct device *cd,
	struct device_attribute *attr,
	const char *buf,
	size_t len)
{
	return 0;
}
#endif

/* g3p: rewritten from the stock disassembly (three outputs: owi chips [types 9, 10, 13], SIA91xx register dump,
 * analog chips 48-register table). The 76 register addresses are the stock rodata table (+0x47c0). */
static const unsigned char sipa_dump_addr[76] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
	0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b,
	0x2c, 0x2d, 0x2e, 0x2f, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e,
	0x3f, 0x40, 0x41, 0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x59, 0x5a, 0x6e, 0x6f, 0x70, 0x71, 0x72, 0x77
};

#define SIPA_ROW16 "%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x"
#define SIPA_V16(v, o) \
	(v)[(o) + 0], (v)[(o) + 1], (v)[(o) + 2], (v)[(o) + 3], (v)[(o) + 4], (v)[(o) + 5], (v)[(o) + 6], (v)[(o) + 7], \
	(v)[(o) + 8], (v)[(o) + 9], (v)[(o) + 10], (v)[(o) + 11], (v)[(o) + 12], (v)[(o) + 13], (v)[(o) + 14], (v)[(o) + 15]

static ssize_t sipa_cmd_show(
	struct device *cd,
	struct device_attribute *attr,
	char *buf)
{
	sipa_dev_t *si_pa = (sipa_dev_t *)dev_get_drvdata(cd);
	unsigned char chip_id = 0;
	unsigned char vals[0x30];
	unsigned int val = 0;
	int n, i;

	if (NULL == si_pa) {
		pr_err("[  err][%s] %s: si_pa is NULL\n", LOG_FLAG, __func__);
		return -EINVAL;
	}

	if (NULL == buf) {
		pr_err("[  err][%s] %s: buf is NULL\n", LOG_FLAG, __func__);
		return -EINVAL;
	}

	if (SIPA_TYPE_IN(si_pa->chip_type, 0xd, 0x2600)) {
		gpiod_get_raw_value(gpio_to_desc(si_pa->owi_pin));
		snprintf(buf, 1024, "sipa_cmd_show : \nchannel_num = %u\nrst_pin = %d,\n"
			"owi_pin = %d,\nowi_mode = %u, delay = %u,\n",
			si_pa->channel_num, si_pa->rst_pin, si_pa->owi_pin,
			si_pa->owi_mode[si_pa->scene], si_pa->owi_delay_us);
		return strlen(buf);
	}

	if (NULL == si_pa->regmap) {
		pr_err("[  err][%s] %s: regmap is NULL !!! \n", LOG_FLAG, __func__);
		return -1;
	}

	if (SIPA_TYPE_IN(si_pa->chip_type, 0x19, 0x3b919c0)) {
		n = snprintf(buf, 40, "channel_num = %d \n", si_pa->channel_num);

		if (SIPA_TYPE_IN(si_pa->chip_type, 0x18, 0x1b00000) &&
			0 == gpiod_get_raw_value(gpio_to_desc(si_pa->rst_pin))) {
			gpiod_set_raw_value(gpio_to_desc(si_pa->rst_pin), 1);
			usleep_range(10000, 12000);
		}

		for (i = 0; i < ARRAY_SIZE(sipa_dump_addr); i++) {
			unsigned char addr = sipa_dump_addr[i];

			if (0 == sipa_read_reg(si_pa->regmap, addr, &val)) {
				n += snprintf(buf + n, 33, "reg addr = %02x, value = 0x%x \n", addr, val);
				val = 0;
			} else {
				n += snprintf(buf + n, 40, "reg addr = %02x, value = XXXXXX \n", addr);
			}
		}

		if (SIPA_TYPE_IN(si_pa->chip_type, 0x18, 0x1b00000) &&
			!sipa_regmap_get_chip_en(si_pa))
			gpiod_set_raw_value(gpio_to_desc(si_pa->rst_pin), 0);

		return strlen(buf);
	}

	sipa_regmap_read(si_pa->regmap, si_pa->chip_type,
		1 == si_pa->chip_type ? 0x41 : 0x00, 1, &chip_id);

	memset(vals, 0, sizeof(vals));
	sipa_regmap_read(si_pa->regmap, si_pa->chip_type, 0x00, 0x30, vals);

	snprintf(buf, 2048, "sipa_cmd_show : \nchannel_num = %u, chip id = 0x%02x \n"
		"      0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f\n"
		"00:  " SIPA_ROW16 "\n"
		"01:  " SIPA_ROW16 "\n"
		"02:  " SIPA_ROW16 "\n"
		"rst_pin = %d, \nsi_pa_disable_pin = %d,  \n"
		"dynamic_updata_vdd_port = 0x%x \n",
		si_pa->channel_num, chip_id,
		SIPA_V16(vals, 0), SIPA_V16(vals, 16), SIPA_V16(vals, 32),
		si_pa->rst_pin, si_pa->disable_pin, si_pa->g3l_en_irq);

	return strlen(buf);
}

/* g3r: rewritten from the stock disassembly (215 insns): NULL buffer / drvdata checks, "Command format is error" paths,
 * regmap NULL checks, usleep_range for the reset pin, gpiod_*_raw_value, info/err prints of the register commands */
static ssize_t sipa_cmd_store(
	struct device *cd,
	struct device_attribute *attr,
	const char *buf,
	size_t len)
{
	const char *split_symb = ",";
	/* in strsep will be modify "cur" value, nor cur[i] value,
	so this point shoud not be defined with "char * const" */
	char *cur = (char *)buf;
	char *after = NULL;
	sipa_dev_t *si_pa;
	unsigned long cmd;

	if (!buf)
		goto fmt_err;

	si_pa = (sipa_dev_t *)dev_get_drvdata(cd);
	if (!si_pa)
		return -ENOIOCTLCMD;

	cmd = simple_strtoul(strsep(&cur, split_symb), &after, 10);

	switch (cmd) {
	case SIPA_CMD_POWER_ON:
		sipa_resume(si_pa);
		break;
	case SIPA_CMD_POWER_OFF:
		sipa_suspend(si_pa);
		break;
	case SIPA_CMD_GET_MODE:
		pr_debug("[debug][%s] %s: mode = %u \n",
				LOG_FLAG, __func__, si_pa->owi_mode[si_pa->scene]);
		break;
	case SIPA_CMD_SET_MODE:
	{
		unsigned long mode;

		if (!cur)
			goto fmt_err;
		mode = simple_strtoul(strsep(&cur, split_symb), &after, 10);
		sipa_set_mode(si_pa, mode);
		break;
	}
	case SIPA_CMD_GET_REG:
	{
		unsigned char addr;
		unsigned int val = 0;

		if (!cur)
			goto fmt_err;
		if (!si_pa->regmap)
			goto regmap_null;
		addr = (unsigned char)simple_strtoul(strsep(&cur, split_symb), &after, 16);
		if (regmap_read(si_pa->regmap, addr, &val))
			pr_debug("[debug][%s] %s: err regmap_read \n", LOG_FLAG, __func__);
		else
			pr_debug("[debug][%s] %s: addr = 0x%x, val = 0x%x\n",
					LOG_FLAG, __func__, addr, val);
		break;
	}
	case SIPA_CMD_SET_REG:
	{
		unsigned char addr;
		unsigned long val;

		if (!cur)
			goto fmt_err;
		if (!si_pa->regmap)
			goto regmap_null;
		addr = (unsigned char)simple_strtoul(strsep(&cur, split_symb), &after, 16);
		if (!cur)
			goto fmt_err;
		val = simple_strtoul(strsep(&cur, split_symb), &after, 16);
		pr_info("[ info][%s] %s: addr:0x%x, val:0x%x !!\n",
				LOG_FLAG, __func__, addr, (unsigned int)val);
		if (regmap_write(si_pa->regmap, addr, (unsigned int)val))
			pr_err("[  err][%s] %s: err regmap_write \n", LOG_FLAG, __func__);
		break;
	}
	case SIPA_CMD_SET_OWI_DELAY:
	{
		unsigned int temp_us;

		if (!cur)
			goto fmt_err;
		temp_us = (unsigned int)simple_strtoul(strsep(&cur, split_symb), &after, 10);
		if (temp_us < MAX_OWI_PULSE_GAP_TIME_US) /* only for test, pulse width must be < 1ms */
			si_pa->owi_delay_us = temp_us;
		else
			si_pa->owi_delay_us = MAX_OWI_PULSE_GAP_TIME_US;
		break;
	}
	case SIPA_CMD_CLR_ERROR:
		memset(&si_pa->err_info, 0, sizeof(si_pa->err_info));
		break;
	case SIPA_CMD_WRITE_BYTE:
		pr_info("[ info][%s] %s: the option "
			"OWI_SUPPORT_WRITE_DATA unsupport!! \n", LOG_FLAG, __func__);
		break;
	case 612:	/* SIPA_CMD_GET_RST_PIN */
		if (0 == si_pa->disable_pin) {
			unsigned int gpio_value = gpiod_get_raw_value(gpio_to_desc(si_pa->rst_pin));

			pr_debug("[debug][%s] %s: get reset pin value, pin num:%u, value:%u \n",
					LOG_FLAG, __func__, si_pa->rst_pin, gpio_value);
		}
		break;
	case 613:	/* SIPA_CMD_SET_RST_PIN */
		if (!cur)
			goto fmt_err;
		if (0 == si_pa->disable_pin) {
			unsigned char value = (unsigned char)simple_strtoul(
				strsep(&cur, split_symb), &after, 16);

			/* power on/off chip */
			gpiod_set_raw_value(gpio_to_desc(si_pa->rst_pin), value);
			usleep_range(1000, 2000);	/* wait chip power off, the time must be > 1ms */

			pr_debug("[debug][%s] %s: set reset pin %u to %u \n",
					LOG_FLAG, __func__, si_pa->rst_pin, value);
		}
		break;
	default:
		return -ENOIOCTLCMD;
	}

	return len;

regmap_null:
	pr_err("[  err][%s] %s: regmap is NULL !!! \n", LOG_FLAG, __func__);
	return -ENOIOCTLCMD;
fmt_err:
	pr_err("[  err][%s] %s: Command format is error !!! \n", LOG_FLAG, __func__);
	return -ENOIOCTLCMD;
}
/********************************************************************
 * end - device attr option
 ********************************************************************/


/********************************************************************
 * si_pa codec driver
 ********************************************************************/
static int sipa_power_get(
	struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
#if (LINUX_VERSION_CODE > KERNEL_VERSION(4, 16, 28))
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	sipa_dev_t *si_pa = snd_soc_component_get_drvdata(component);
#else
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	sipa_dev_t *si_pa = snd_soc_codec_get_drvdata(codec);
#endif

	ucontrol->value.integer.value[0] =
		(unsigned long)sipa_is_chip_en(si_pa);

	pr_debug("[debug][%s] %s: ucontrol = %ld, channel_num = %d \n",
		LOG_FLAG, __func__, ucontrol->value.integer.value[0], si_pa->channel_num);

	return 0;
}

static int sipa_power_set(
	struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
#if (LINUX_VERSION_CODE > KERNEL_VERSION(4, 16, 28))
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
    sipa_dev_t *si_pa = snd_soc_component_get_drvdata(component);
#else
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	sipa_dev_t *si_pa = snd_soc_codec_get_drvdata(codec);
#endif

	pr_debug("[debug][%s] %s: ucontrol = %ld, rst = %d, channel_num = %d \n",
		LOG_FLAG, __func__, ucontrol->value.integer.value[0],
		si_pa->rst_pin, si_pa->channel_num);

	if (1 == ucontrol->value.integer.value[0]) {
		sipa_resume(si_pa);
	} else {
		sipa_suspend(si_pa);
	}

	return 0;
}

static int sipa_audio_scene_get(
	struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
#if (LINUX_VERSION_CODE > KERNEL_VERSION(4, 16, 28))
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	sipa_dev_t *si_pa = snd_soc_component_get_drvdata(component);
#else
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	sipa_dev_t *si_pa = snd_soc_codec_get_drvdata(codec);
#endif

	ucontrol->value.integer.value[0] = si_pa->scene;

	pr_debug("[debug][%s] %s: ucontrol = %ld, channle = %d \n",
		LOG_FLAG, __func__, ucontrol->value.integer.value[0], si_pa->channel_num);

	return 0;
}

static int sipa_audio_scene_set(
	struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	sipa_dev_t *si_pa = snd_soc_component_get_drvdata(component);

	pr_debug("[debug][%s] %s: ucontrol = %ld, rst = %d, channle = %d \n",
		LOG_FLAG, __func__, ucontrol->value.integer.value[0],
		si_pa->rst_pin, si_pa->channel_num);

	sipa_scene_set(si_pa, ucontrol->value.integer.value[0]);
	return 0;
}

#include "g3l_sipa.inc.h"

#ifdef ALGO_SWITCH_EN
static int sipa_algo_en_get(
	struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
#if (LINUX_VERSION_CODE > KERNEL_VERSION(4, 16, 28))
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	sipa_dev_t *si_pa = snd_soc_component_get_drvdata(component);
#else
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	sipa_dev_t *si_pa = snd_soc_codec_get_drvdata(codec);
#endif
	int32_t algo_en = 0;

	pr_debug("[debug][%s] %s: ucontrol = %ld \n",
		LOG_FLAG, __func__, ucontrol->value.integer.value[0]);

	if (sipa_is_chip_en(si_pa))
		sipa_algo_en_read(si_pa, &algo_en);

	ucontrol->value.integer.value[0] = algo_en;
	return 0;
}

static int sipa_algo_en_set(
	struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
#if (LINUX_VERSION_CODE > KERNEL_VERSION(4, 16, 28))
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	sipa_dev_t *si_pa = snd_soc_component_get_drvdata(component);
#else
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	sipa_dev_t *si_pa = snd_soc_codec_get_drvdata(codec);
#endif

	pr_debug("[debug][%s] %s: ucontrol = %ld\n",
		LOG_FLAG, __func__, ucontrol->value.integer.value[0]);

	if (sipa_is_chip_en(si_pa))
		if (0 >= sipa_algo_en_write(si_pa,
				ucontrol->value.integer.value[0]))
			return -EINVAL;

	return 0;
}
#endif

static const char *const power_function[] = { "Off", "On" };
#ifdef ALGO_SWITCH_EN
static const char *const algo_enable[] = { "Off", "On" };
#endif
static const char *const audio_scene[] = { "Playback", "Voice", "Receiver", "Factory" };

static const struct soc_enum power_enum =
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(power_function), power_function);
#ifdef ALGO_SWITCH_EN
static const struct soc_enum algo_enum =
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(algo_enable), algo_enable);
#endif
static const struct soc_enum audio_scene_enum =
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(audio_scene), audio_scene);

static const struct snd_kcontrol_new sipa_controls[] = {

	SOC_ENUM_EXT("Sipa Power", power_enum,
			sipa_power_get, sipa_power_set),
#ifdef ALGO_SWITCH_EN
	SOC_ENUM_EXT("Sipa Algo", algo_enum,
			sipa_algo_en_get, sipa_algo_en_set),
#endif

	SOC_ENUM_EXT("Sipa Audio Scene", audio_scene_enum,
			sipa_audio_scene_get, sipa_audio_scene_set),
	SOC_SINGLE_EXT("Sipa Pvdd Limit", SND_SOC_NOPM, 0, 5000000, 0,
			sipa_pvdd_limit_get, sipa_pvdd_limit_set),
	SOC_SINGLE_EXT("Sipa Status", SND_SOC_NOPM, 0, 255, 0,
			sipa_status_get, sipa_status_set),
	SOC_SINGLE_EXT("Sipa Type", SND_SOC_NOPM, 0, 100, 0,
			sipa_chip_type_get, sipa_chip_type_set),
	SOC_SINGLE_EXT("Sipa Mute", SND_SOC_NOPM, 0, 1, 0,
			sipa_mute_get, sipa_mute_set),
	SND_SOC_BYTES_TLV("Sipa reg", 4, sipa_reg_get, sipa_reg_set),
	SOC_SINGLE_EXT("sia91xx_spk_sw_gpio_switch", SND_SOC_NOPM, 0, 3, 0,
			sia91xx_spksw_gpio_get, sia91xx_spksw_gpio_put),
};

#ifdef SIA91XX_TYPE
static const struct snd_soc_dai_ops sia91xx_dai_ops = {
	.startup = sia91xx_startup,
	.set_fmt = sia91xx_set_fmt,
	.hw_params = sia91xx_hw_params,
	.mute_stream = sia91xx_mute,
};

static struct snd_soc_dai_driver sia91xx_dai[] = {
	{
		.name = "sia91xx-aif",
		.id = 1,
		.playback = {
			.stream_name = "AIF Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SIA91XX_RATES,
			.formats = SIA91XX_FORMATS,
		},
		.capture = {
			 .stream_name = "AIF Capture",
			 .channels_min = 1,
			 .channels_max = 2,
			 .rates = SIA91XX_RATES,
			 .formats = SIA91XX_FORMATS,
		 },
		 .ops = &sia91xx_dai_ops,
	}
};
#endif

static int sipa_spkr_pa_event(
	struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol,
	int event)
{
#if (LINUX_VERSION_CODE > KERNEL_VERSION(4, 16, 28))
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	sipa_dev_t *si_pa = snd_soc_component_get_drvdata(component);
#else
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);
	sipa_dev_t *si_pa = snd_soc_codec_get_drvdata(codec);
#endif

	pr_debug("[debug][%s] %s: msg = %d \n", LOG_FLAG, __func__, event);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		sipa_resume(si_pa);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		sipa_suspend(si_pa);
		break;
	default:
		pr_err("[  err][%s] %s: msg = %d \n", LOG_FLAG, __func__, event);
		break;
	}

	return 0;
}

static const struct snd_soc_dapm_widget sipa_dapm_widgets[] = {
	SND_SOC_DAPM_INPUT("IN"),

	SND_SOC_DAPM_OUT_DRV_E("SPKR DRV", 0, 0, 0, NULL, 0,
			sipa_spkr_pa_event, SND_SOC_DAPM_POST_PMU |
			SND_SOC_DAPM_PRE_PMD),

	SND_SOC_DAPM_OUTPUT("SPKR")
};

/*static const struct snd_soc_dapm_route sia81xx_audio_map[] = {
	{"SPKR DRV", NULL, "IN"},
	{"SPKR", NULL, "SPKR DRV"}
};*/


#if (LINUX_VERSION_CODE > KERNEL_VERSION(4, 16, 28))
static int sipa_component_probe(
	struct snd_soc_component *component)
{
	sipa_dev_t *si_pa = dev_get_drvdata(component->dev);

	if (!si_pa) {
		pr_err("[  err][%s] %s: si_pa is NULL\n", LOG_FLAG, __func__);
		return -EINVAL;
	}

	pr_info("[debug][%s] %s: running \n", LOG_FLAG, __func__);
#ifdef SIA91XX_TYPE
	/* g3r: stock: only the SIA91xx family (mask 0x3b919c0) */
	if (SIPA_TYPE_IN(si_pa->chip_type, 0x19, 0x3b919c0))
		sia91xx_component_probe(component);
#endif
	return 0;
}

static void sipa_component_remove(
	struct snd_soc_component *component)
{
	pr_info("[debug][%s] %s: running \n", LOG_FLAG, __func__);
	return;
}
#else
static int sipa_codec_probe(
	struct snd_soc_codec *codec)
{
    pr_info("[debug][%s] %s: running \n", LOG_FLAG, __func__);
#ifdef SIA91XX_TYPE
	sia91xx_codec_probe(codec);
#endif
	return 0;
}

static int sipa_codec_remove(
	struct snd_soc_codec *codec)
{
	pr_info("[debug][%s] %s: running \n", LOG_FLAG, __func__);
	return 0;
}

static struct regmap *sipa_codec_get_regmap(struct device *dev)
{
	return NULL;
}
#endif


#if (LINUX_VERSION_CODE > KERNEL_VERSION(4, 16, 28))
static int sipa_component_write(struct snd_soc_component *component,
				 unsigned int reg, unsigned int val) 
{
	return 0;
}

static unsigned int sipa_component_read(struct snd_soc_component *component,
			     unsigned int reg)
{
	return 0;
}

static struct snd_soc_component_driver soc_component_dev_sipa = {
	.probe = sipa_component_probe,
	.remove = sipa_component_remove,
	.controls = sipa_controls,
	.num_controls = ARRAY_SIZE(sipa_controls),
	.dapm_widgets = sipa_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(sipa_dapm_widgets),
	.dapm_routes = NULL, //sia81xx_audio_map,
	.num_dapm_routes = 0,//ARRAY_SIZE(sia81xx_audio_map),
	.write = sipa_component_write,
	.read = sipa_component_read,
};

#elif (LINUX_VERSION_CODE > KERNEL_VERSION(4, 8, 17) && LINUX_VERSION_CODE <= KERNEL_VERSION(4, 16, 28))
static struct snd_soc_codec_driver soc_codec_dev_sipa = {
	.probe = sipa_codec_probe,
	.remove = sipa_codec_remove,
	.get_regmap = sipa_codec_get_regmap,
	.component_driver = {
		.controls = sipa_controls,
		.num_controls = ARRAY_SIZE(sipa_controls),
		.dapm_widgets = sipa_dapm_widgets,
		.num_dapm_widgets = ARRAY_SIZE(sipa_dapm_widgets),
		.dapm_routes = NULL, //sia81xx_audio_map,
		.num_dapm_routes = 0,//ARRAY_SIZE(sia81xx_audio_map),
	},
};
#else
static struct snd_soc_codec_driver soc_codec_dev_sipa = {
	.probe = sipa_codec_probe,
	.remove = sipa_codec_remove,
	.get_regmap = sipa_codec_get_regmap,
	.controls = sipa_controls,
	.num_controls = ARRAY_SIZE(sipa_controls),
	.dapm_widgets = sipa_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(sipa_dapm_widgets),
	.dapm_routes = NULL, //sia81xx_audio_map,
	.num_dapm_routes = 0,//ARRAY_SIZE(sia81xx_audio_map),
};
#endif

/********************************************************************
 * end - sipa codec driver
 ********************************************************************/




/********************************************************************
 * sipa driver common
 ********************************************************************/
/* g3p: the stock has NO list (sia81xx_list, add/del/get_sipa_dev are gone): the registry is the array g_sipa_dev[0..3]
 * (.bss+0x8..0x20) guarded by sipa_mutex; find_sipa_dev takes ONE argument (the device_node of the platform device).
 * kCFI: struct sipa_dev_s *(struct device_node *) = 0xb0c74a14. */
sipa_dev_t *find_sipa_dev(
	struct device_node *of_node)
{
	sipa_dev_t *si_pa = NULL;
	int i;

	if (NULL == of_node) {
		pr_err("[  err][%s] %s: NULL == input parameter \n",
			LOG_FLAG, __func__);
		return NULL;
	}

	mutex_lock(&sipa_mutex);

	for (i = 0; i < SIPA_CHANNEL_NUM; i++) {
		/* make sure that the sipa platform dev had been created */
		if (NULL != g_sipa_dev[i] && NULL != g_sipa_dev[i]->pdev &&
			of_node == g_sipa_dev[i]->pdev->dev.of_node) {
			si_pa = g_sipa_dev[i];
			break;
		}
	}

	mutex_unlock(&sipa_mutex);

	return si_pa;
}

/* stock create_sipa_dev (inlined into sipa_probe): kmalloc 0x2a8, first free slot of g_sipa_dev[], name "sipa-spk.<slot>" */
static __always_inline sipa_dev_t *create_sipa_dev(
	struct platform_device *pdev)
{
	sipa_dev_t *si_pa;
	int i;

	/* don't use devm_kzalloc() */
	si_pa = kzalloc(sizeof(sipa_dev_t), GFP_KERNEL);
	if (NULL == si_pa) {
		pr_err("[  err][%s] %s: dev[%s] cannot create "
			"memory for si_pa\n",
			LOG_FLAG, __func__, pdev->dev.init_name);
		return NULL;
	}

	mutex_lock(&sipa_mutex);
	for (i = 0; i < SIPA_CHANNEL_NUM; i++) {
		if (NULL == g_sipa_dev[i])
			break;
	}
	if (i == SIPA_CHANNEL_NUM) {
		mutex_unlock(&sipa_mutex);
		kfree(si_pa);
		return NULL;
	}
	g_sipa_dev[i] = si_pa;
	mutex_unlock(&sipa_mutex);

	snprintf(si_pa->name, 11, "sipa-spk.%u", i);

	pr_debug("[debug][%s] %s: add si_pa dev : %s, index = %u \n",
		LOG_FLAG, __func__, si_pa->name, i);

	return si_pa;
}

static __always_inline void put_sipa_dev(sipa_dev_t *si_pa)
{
	int i;

	pr_debug("[debug][%s] %s: put dev name : %s, pdev = %p, client = %p \n",
				LOG_FLAG, __func__,
				si_pa->name, si_pa->pdev, si_pa->client);

	if ((NULL != si_pa->pdev) || (NULL != si_pa->client))
		return ;

	for (i = 0; i < SIPA_CHANNEL_NUM; i++) {
		if (g_sipa_dev[i] == si_pa) {
			kfree(si_pa);
			g_sipa_dev[i] = NULL;
			return ;
		}
	}

	pr_err("[  err][%s] %s: dev[%s] cannot find in device array\n",
		LOG_FLAG, __func__, si_pa->name);
}

static __always_inline unsigned int get_chip_type(const char *name)
{
	int i = 0, len = 0;

	if (NULL == name)
		return CHIP_TYPE_UNKNOWN;

	pr_info("[ info][%s] %s: chip : %s \n",
		LOG_FLAG, __func__, name);

	len = strlen(name);
	for (i = 0; i < ARRAY_SIZE(support_chip_type_name_table); i++) {
		if (strlen(support_chip_type_name_table[i]) == len &&
			0 == memcmp(support_chip_type_name_table[i], name, len)) {
			pr_info("[ info][%s] %s: chip_type = %d\n", LOG_FLAG, __func__, i);
			return i;
		}
	}

	return CHIP_TYPE_UNKNOWN;
}

/* g3p: the four compatible-chip tables of the stock (sipa_compat_table at .rodata+0x3408: 14 -> list of 4, 15 -> 4, 16 -> 2, 24 -> 2) */
static const uint32_t sia81x9_list[] = {
	CHIP_TYPE_SIA815T,
	CHIP_TYPE_SIA8159,
	CHIP_TYPE_SIA8159A,
	CHIP_TYPE_SIA8109
};

static const uint32_t sia8152x_list[] = {
	CHIP_TYPE_SIA8152S,	// first chip reg range should cover all other chips
	CHIP_TYPE_SIA8152,
	CHIP_TYPE_SIA8150,
	CHIP_TYPE_SIA8132
};

static const uint32_t sia917x_list[] = {
	CHIP_TYPE_SIA9175,
	CHIP_TYPE_SIA9177
};

static const uint32_t sia5118x_list[] = {
	CHIP_TYPE_SIA5118,
	CHIP_TYPE_SIA5118S
};

/* compatible chips should have same i2c address */
static const struct sipa_chip_compat sipa_compat_table[] = {
	{
		CHIP_TYPE_SIA81X9,
		{sia81x9_list, ARRAY_SIZE(sia81x9_list)}
	},
	{
		CHIP_TYPE_SIA8152X,
		{sia8152x_list, ARRAY_SIZE(sia8152x_list)}
	},
	{
		CHIP_TYPE_SIA917X,
		{sia917x_list, ARRAY_SIZE(sia917x_list)}
	},
	{
		CHIP_TYPE_SIA5118X,
		{sia5118x_list, ARRAY_SIZE(sia5118x_list)}
	}
};

unsigned int get_one_available_chip_type(unsigned int chip_type)
{
	int i = 0;

	for (i = 0; i < ARRAY_SIZE(sipa_compat_table); i++) {
		if (chip_type == sipa_compat_table[i].sub_type) {
			if (0 < sipa_compat_table[i].num)
				//pr_info("[ info][%s] %s: chip_type = %u \n",
				//	LOG_FLAG, __func__, sipa_compat_table[i].chips[0]);
				return sipa_compat_table[i].chips[0];
		}
	}

	return chip_type;
}

#ifdef DISTINGUISH_CHIP_TYPE
/* g3p: rewritten from the stock disassembly: the chip is powered through the reset line (types of the mask 0x1b44002),
 * not through sipa_resume/suspend */
static __always_inline void sipa_get_rst_value(sipa_dev_t *si_pa);

static noinline int check_sipa_status(sipa_dev_t *si_pa)
{
	int ret = 0;

	if (0 == si_pa->disable_pin && SIPA_TYPE_IN(si_pa->chip_type, 0x18, 0x1b44002)) {
		gpiod_set_raw_value(gpio_to_desc(si_pa->rst_pin), 1);
		usleep_range(1000, 1200);
	}

	if (1 == si_pa->en_dyn_id) {
		ret = gpiod_get_raw_value(gpio_to_desc(si_pa->id_pin));
		pr_info("[ info][%s] ch %d id_pin_val = %d\n", __func__, si_pa->channel_num, ret);
	} else {
		if (0 == si_pa->disable_pin)
			sipa_get_rst_value(si_pa);
		ret = sipa_regmap_check_chip_id(
			si_pa->regmap, si_pa->channel_num, si_pa->chip_type);
	}

	if (0 == si_pa->disable_pin && SIPA_TYPE_IN(si_pa->chip_type, 0x18, 0x1b44002)) {
		gpiod_set_raw_value(gpio_to_desc(si_pa->rst_pin), 0);
		usleep_range(1000, 1200);
	}

	return ret;
}
#endif

/* g3p: rewritten from the stock disassembly. Only the compat sub types 14/15/16/24 are handled (stock jump table), the
 * chip is powered through the reset line only for the types of the masks 0x1b44000 (up) / 0x1b44002 (down, tested on the
 * detected type), distinguish_chip_type() runs only for those sub types. */
static __always_inline void sipa_get_rst_value(sipa_dev_t *si_pa)
{
	pr_debug("[debug][%s] %s: reset pin num:%u, value:%u \r\n",
		LOG_FLAG, __func__, si_pa->rst_pin,
		gpiod_get_raw_value(gpio_to_desc(si_pa->rst_pin)));
}

void sipa_compatible_chips_adapt(
	sipa_dev_t *si_pa)
{
	const struct sipa_chip_compat *compat;
	bool found = true;
	int j;

	switch (si_pa->chip_type) {
	case CHIP_TYPE_SIA81X9:
		compat = &sipa_compat_table[0];
		break;
	case CHIP_TYPE_SIA8152X:
		compat = &sipa_compat_table[1];
		break;
	case CHIP_TYPE_SIA917X:
		compat = &sipa_compat_table[2];
		break;
	case CHIP_TYPE_SIA5118X:
		compat = &sipa_compat_table[3];
		break;
	default:
		return;
	}

	if (0 == si_pa->disable_pin) {
		if (SIPA_TYPE_IN(si_pa->chip_type, 0x18, 0x1b44000)) {
			gpiod_set_raw_value(gpio_to_desc(si_pa->rst_pin), 1);
			usleep_range(1000, 1200);
		}
		sipa_get_rst_value(si_pa);
	}

	for (j = 0; j < compat->num; j++) {
		if (NULL != compat->chips
			&& 0 == sipa_regmap_check_chip_id(si_pa->regmap,
			si_pa->channel_num, compat->chips[j])) {
			si_pa->chip_type = compat->chips[j];
			break;
		}
	}
	found = j < compat->num;

	if (0 == si_pa->disable_pin &&
		SIPA_TYPE_IN(si_pa->chip_type, 0x18, 0x1b44002)) {
		gpiod_set_raw_value(gpio_to_desc(si_pa->rst_pin), 0);
		usleep_range(1000, 1200);
	}

	if (!found)
		si_pa->chip_type = CHIP_TYPE_UNKNOWN;

	pr_info("[ info][%s] %s: chip_type = %u \n",
		LOG_FLAG, __func__, si_pa->chip_type);

	distinguish_chip_type(si_pa);
}

/********************************************************************
 * end - sipa driver common
 ********************************************************************/

/********************************************************************
 * i2c bus driver
 ********************************************************************/
/* g3n: rewritten from the stock disassembly. GUESSES: the return value for chip types that are in neither mask
 * (-EINVAL), the meaning of +0x22c (the stock stores extra_cfg.dyn_ud_vdd_port there), the per-scene clamp array
 * (the stock writes the 6 values to si_pa+0x1f4..0x208 and 0 to +0x20c, which in OUR layout would overlap the owi mode
 * and the spinlocks: stored in separate fields, layout mismatch). */
int sipa_pending_actions(sipa_dev_t *si_pa)
{
	SIPA_CHIP_CFG chip_cfg = { 0 };
	SIPA_EXTRA_CFG extra_cfg = { 0 };
	unsigned int type;
	int i;

	if (NULL == si_pa)
		return -EINVAL;

	if (NULL == sipa_param_read_chip_cfg(
		si_pa->channel_num, si_pa->chip_type, &chip_cfg)) {
		pr_err("[  err][%s] %s: read_chip_cfg return NULL \n", LOG_FLAG, __func__);
		return -EFAULT;
	}

	if (0 != sipa_param_read_extra_cfg(si_pa->channel_num, &extra_cfg))
		return -EFAULT;

	si_pa->g3l_en_irq = extra_cfg.dyn_ud_vdd_port;
	g_dyn_ud_vdd_port = extra_cfg.dyn_ud_vdd_port;

	type = si_pa->chip_type;
	if (type > 0x19)
		return -EINVAL;

	if (!((1u << type) & 0x3b919c0)) {
		if (!((1u << type) & 0x2600))
			return -EINVAL;

		si_pa->owi_delay_us = 10;
		si_pa->owi_polarity = 0;
		for (i = 0; i < 6; i++)
			si_pa->owi_mode[i] =
				(chip_cfg.g3m_unk28[i] - 1 < 0x23) ? chip_cfg.g3m_unk28[i] : 6;

		/* power down chip when phone start up */
		sipa_suspend(si_pa);
	}

	pr_info("[ info][%s] %s: sipa %d driver init done \n",
		LOG_FLAG, __func__, si_pa->channel_num);

	return 0;
}

/* g3n: rewritten from the stock disassembly: the stock probe does not read the DT chip type itself, it finds the
 * sipa_dev that the platform probe registered in g_sipa_dev[] through the phandle `si,sipa-dev`.
 * GUESSES: set_smartpa_codec argument (stock passes &client->dev, the stub has a different prototype whose CRC
 * 0x77aedcd7 was not reproduced), the sysfs attribute created at the end (dev_attr_sipa_device), the name of the
 * field at +0x228 (g3l_en_compat). */
int sipa_i2c_probe(
	struct i2c_client *client)
{
	sipa_dev_t *si_pa = NULL;
	struct device_node *np;
	struct snd_soc_dai_driver *dai;
	unsigned int val;
	int addr_offset = 0;
	int ret, i;

	if (NULL == client) {
		pr_err("[  err][%s] %s: client is NULL\n", LOG_FLAG, __func__);
		return -ENODEV;
	}

	pr_info("[ info][%s] %s: i2c addr = 0x%02x \n", LOG_FLAG, __func__, client->addr);

	if (!check_smartpa_type("sia91xx")) {
		pr_warn("[ warn][%s] %s: smartpa type %d already probed, abort", LOG_FLAG, __func__,
			get_smartpa_type());
		return -ENODEV;
	}

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		pr_err("[  err][%s] %s: i2c_check_functionality return -ENODEV \n", LOG_FLAG, __func__);
		return -ENODEV;
	}

	np = of_parse_phandle(client->dev.of_node, "si,sipa-dev", 0);
	if (NULL == np) {
		pr_err("[  err][%s] %s: failed to get si,sipa-dev !!! \n", LOG_FLAG, __func__);
		return -EINVAL;
	}

	mutex_lock(&sipa_mutex);
	for (i = 0; i < SIPA_CHANNEL_NUM; i++) {
		si_pa = g_sipa_dev[i];
		if (si_pa && si_pa->pdev && si_pa->pdev->dev.of_node == np)
			break;
	}
	if (i == SIPA_CHANNEL_NUM) {
		mutex_unlock(&sipa_mutex);
		pr_err("[  err][%s] %s: can't find si_pa\n", LOG_FLAG, __func__);
		return -ENODEV;
	}
	mutex_unlock(&sipa_mutex);

	if (CHIP_TYPE_UNKNOWN == si_pa->chip_type) {
		pr_err("[  err][%s] %s: CHIP_TYPE_UNKNOWN == chip_type !!! \n", LOG_FLAG, __func__);
		return -ENODEV;
	}

	ret = of_property_read_s32(client->dev.of_node, "si,sipa-addr_offset", &addr_offset);
	if (ret < 0) {
		pr_info("[ info][%s] %s: addr_offset default value!!!\n", LOG_FLAG, __func__);
		addr_offset = 0;
	}

	client->addr += addr_offset;
	if (client->addr > 0x7f) {
		pr_err("[ err][%s] %s: client addr error!\n", LOG_FLAG, __func__);
		return -ENODEV;
	}

	si_pa->regmap = sipa_regmap_init(client, si_pa->channel_num,
		get_one_available_chip_type(si_pa->chip_type));
	if (IS_ERR(si_pa->regmap)) {
		pr_err("[  err][%s] %s: regmap_init_i2c error [TF-NOSOUND]!!! \n", LOG_FLAG, __func__);
		[[clang::always_inline]] mievent_report(0x36007bc9, "PA i2c exception", &client->dev);
		return PTR_ERR(si_pa->regmap);
	}

	if (si_pa->chip_type == 15) {	/* stock: chip type 15 only */
		if (regmap_read(si_pa->regmap, 0, &val) < 0) {
			/* retry with the address - 0x30 */
			sipa_regmap_remove(si_pa);
			client->addr -= 0x30;
			si_pa->regmap = sipa_regmap_init(client, si_pa->channel_num,
				get_one_available_chip_type(si_pa->chip_type));
			if (IS_ERR(si_pa->regmap)) {
				pr_err("[  err][%s] %s: regmap_init_i2c error !!! \n", LOG_FLAG, __func__);
				return PTR_ERR(si_pa->regmap);
			}
		}
		regmap_read(si_pa->regmap, 0, &val);
		pr_info("[ info][%s] %s: read chip id = 0x%x !!! \n", LOG_FLAG, __func__, val);
	}

	/* save i2c client */
	si_pa->client = client;

	/* sava driver private data to the dev's driver data */
	dev_set_drvdata(&client->dev, si_pa);

	// for sia8101 stereo
	if (CHIP_TYPE_SIA8101 == si_pa->chip_type && 0 == si_pa->channel_num)
		g_default_sia_dev = si_pa;

	if (si_pa->rst_pin >= 0) {
		ret = devm_gpio_request_one(&client->dev, si_pa->rst_pin, GPIOF_OUT_INIT_LOW, "SIA91XX_RST");
		if (ret) {
			pr_err("[ err][%s] %s: rst gpio request failed!\n", LOG_FLAG, __func__);
			if (ret == -EBUSY)
				return -EPROBE_DEFER;
		}
	}

	if (si_pa->g3l_en_compat)
		sipa_compatible_chips_adapt(si_pa);

	if (si_pa->chip_type > 0x19 || !((1u << si_pa->chip_type) & 0x3b919c0)) {
		pr_err("[  err][%s] %s: CHIP_TYPE_UNKNOWN == chip_type !!! \n", LOG_FLAG, __func__);
		return -ENODEV;
	}

	ret = sia91xx_detect_chip(si_pa);
	if (ret < 0) {
		pr_err("[  err][%s] %s: sia91xx detect fail !!! \n", LOG_FLAG, __func__);
		return ret;
	}

	set_smartpa_type("sia91xx", 8);

	if (si_pa->spksw_gpio >= 0) {
		ret = devm_gpio_request_one(&client->dev, si_pa->spksw_gpio, GPIOF_OUT_INIT_LOW, "sia91xx_spksw");
		if (ret) {
			pr_err("[  err][%s] %s: spksw gpio request failed!\n", LOG_FLAG, __func__);
			return ret;
		}
	}

	dai = devm_kzalloc(&client->dev, sizeof(sia91xx_dai), GFP_KERNEL);
	if (NULL == dai)
		return -ENOMEM;

	memcpy(dai, sia91xx_dai, sizeof(sia91xx_dai));

	sia91xx_append_i2c_address(&client->dev, client, NULL, 0, dai, ARRAY_SIZE(sia91xx_dai));

	ret = devm_snd_soc_register_component(&client->dev,
		&soc_component_dev_sipa, dai, ARRAY_SIZE(sia91xx_dai));
	if (ret < 0) {
		dev_err(&client->dev, "Failed to register sia91xx: %d\n", ret);
		return ret;
	}
	pr_info("[info][%s] snd_soc_register_codec ret = %d !\n", __func__, ret);

	set_smartpa_codec(dev_name(&client->dev), dai[0].name);	/* GUESS: stock passes &client->dev */

	if (!(si_pa->chip_type <= 0xd && ((1u << si_pa->chip_type) & 0x2600)) &&
	    si_pa->chip_type != CHIP_TYPE_UNKNOWN)
		device_create_file(&si_pa->pdev->dev, &dev_attr_sipa_device);	/* GUESS: which attribute */

	sipa_param_load_fw(&client->dev, "sipa.bin");

	pr_info("[ info][%s] %s: finish i2c addr = 0x%02x, name:%s, channel:%d\n",
		LOG_FLAG, __func__, client->addr, si_pa->name, si_pa->channel_num);

	return 0;
}
EXPORT_SYMBOL(sipa_i2c_probe);

void sipa_i2c_remove(
	struct i2c_client *client)
{
	sipa_dev_t *si_pa = NULL;

	/* g3r: from the stock disassembly: NULL checks with their messages, the irq work is cancelled only for the SIA91xx
	 * family with a valid irq pin, no sipa_regmap_remove call */
	if (NULL == client) {
		pr_err("[  err][%s] %s: client is NULL\n", LOG_FLAG, __func__);
		return;
	}

	pr_info("[ info][%s] %s: remove \n", LOG_FLAG, __func__);

	si_pa = (sipa_dev_t *)i2c_get_clientdata(client);
	if (NULL == si_pa) {
		pr_err("[  err][%s] %s: si_pa is NULL \n", LOG_FLAG, __func__);
		return;
	}

	if (SIPA_TYPE_IN(si_pa->chip_type, 0x19, 0x3b919c0) && si_pa->irq_pin >= 0)
		cancel_delayed_work_sync(&si_pa->interrupt_work);

	si_pa->regmap = NULL;

	si_pa->client = NULL;

	put_sipa_dev(si_pa);
}
EXPORT_SYMBOL(sipa_i2c_remove);

#ifndef PLATFORM_TYPE_MTK
static const struct i2c_device_id si_sipa_i2c_id[] = {
	{ SIPA_I2C_NAME, 0 },
	{ }
};

#ifdef CONFIG_OF
static const struct of_device_id si_sipa_i2c_match[] = {
	{.compatible = "si,sia81xx-i2c"},
	{.compatible = "si,sia8101-i2c"},
	{.compatible = "si,sia8109-i2c"},
	{.compatible = "si,sia8152-i2c"},
	{.compatible = "si,sia8152s-i2c"},
	{.compatible = "si,sia8159-i2c"},
	{.compatible = "si,sia81x9-i2c"},
	{.compatible = "si,sia8152x-i2c"},
	{.compatible = "si,sia91xx-i2c"},
	{.compatible = "si,sia8132-i2c"},
	{},
};
#endif

static struct i2c_driver si_sipa_i2c_driver = {
	.driver = {
		.name = SIPA_I2C_NAME,
		.owner = THIS_MODULE,
#ifdef CONFIG_OF
		.of_match_table = si_sipa_i2c_match,
#endif
	},
	.probe		= sipa_i2c_probe,
	.remove		= sipa_i2c_remove,
	.shutdown	= sipa_i2c_shutdown,
	.id_table	= si_sipa_i2c_id,
};
#endif
/********************************************************************
 * end - i2c bus driver
 ********************************************************************/

/********************************************************************
 * sipa dev driver
 ********************************************************************/

/* power manage options */
static int sipa_pm_suspend(
	struct device *dev)
{
#if 0
	sipa_dev_t *si_pa = NULL;

	pr_debug("[debug][%s] %s: running \n", LOG_FLAG, __func__);


	si_pa = (sipa_dev_t *)dev_get_drvdata(dev);

	return sipa_suspend(si_pa);
#else
	pr_debug("[debug][%s] %s: running \n", LOG_FLAG, __func__);

	return 0;
#endif
}

static int sipa_pm_resume(struct device *dev)
{
#if 0
	sipa_dev_t *si_pa = NULL;

	pr_debug("[debug][%s] %s: running \n", LOG_FLAG, __func__);

	si_pa = (sipa_dev_t *)dev_get_drvdata(dev);

	return sipa_resume(si_pa);
#else
	pr_debug("[debug][%s] %s: running \n", LOG_FLAG, __func__);

	return 0;
#endif
}

static const struct dev_pm_ops si_sipa_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(sipa_pm_suspend, sipa_pm_resume)
};

/* g3p: sipa_probe/sipa_remove/sipa_shutdown rewritten from the stock disassembly (G3p). The stock probe is built from the
 * inlined helpers create_sipa_dev (array slot), sipa_property_init (DT, chip type masks), sipa_pinctrl_select; the
 * "si,sipa-dev-name" lookup / list of the template does not exist.
 * Masks: 0x2600 = owi chips (types 9, 10, 13), 0x3b919c0 = SIA91xx family, 0x3a919c0 = family without type 20.
 * QUIRK kept from the stock: chip type 20 is in the irq-request mask (0x3b919c0) but not in the irq-property mask
 * (0x3a919c0), so devm_gpio_request_one() asks for irq_pin == 0 for it. */
static __always_inline int sipa_property_init(
	sipa_dev_t *si_pa,
	struct device_node *np)
{
	const char *chip_type_name = NULL;
	unsigned int chip_type;
	u32 disable_pin = 0, channel_num = 0, en_compat = 0, en_dyn_id = 0, en_irq = 0;
	int ret, rst_pin = 0, owi_pin = 0;

	/* get chip type name */
	ret = of_property_read_string_index(np, "si,si_pa_type", 0, &chip_type_name);
	if (ret < 0) {
		pr_err("[  err][%s] %s: get si,si_pa_type return %d !!! \n",
			LOG_FLAG, __func__, ret);
		return -ENODEV;
	}

	chip_type = get_chip_type(chip_type_name);
	if (CHIP_TYPE_UNKNOWN == chip_type) {
		pr_err("[  err][%s] %s: CHIP_TYPE_UNKNOWN == chip_type !!! \n",
			LOG_FLAG, __func__);
		return -ENODEV;
	}

	ret = of_property_read_u32(np, "si,si_pa_disable_pin", &disable_pin);
	if (ret < 0) {
		pr_err("[  err][%s] %s: get si,si_pa_disable_pin return %d !!! \n",
			LOG_FLAG, __func__, ret);
		return -ENODEV;
	}

	if (of_property_read_u32(np, "channel_num", &channel_num) < 0)
		channel_num = 0;

	if (of_property_read_u32(np, "en_compatible_type_func", &en_compat) < 0)
		en_compat = 0;

	ret = of_property_read_u32(np, "en_dynamic_id", &en_dyn_id);
	if (ret < 0 || 1 != en_dyn_id)
		en_dyn_id = 0;

	si_pa->en_dyn_id = en_dyn_id;
	memset(&si_pa->err_info, 0, sizeof(si_pa->err_info));
	si_pa->channel_num = channel_num;
	si_pa->g3l_en_compat = en_compat;
	si_pa->chip_type = chip_type;
	si_pa->scene = 0;
	si_pa->disable_pin = disable_pin;
	si_pa->status = 0;

	pr_debug("[debug][%s] %s: channel_num = %d, en_compatible_type = %d, scene = %d, "
		"disable_pin = %d, en_dyn_id = %d\n",
		LOG_FLAG, __func__, channel_num, en_compat, 0, disable_pin, en_dyn_id);

	if (0 == si_pa->disable_pin) {
		/* get reset pin's sn */
		rst_pin = of_get_named_gpio(np, "si,si_pa_reset", 0);
		if (rst_pin < 0) {
			pr_err("[  err][%s] %s: rst_pin < 0 !!! \n", LOG_FLAG, __func__);
			return -ENODEV;
		}
		pr_debug("[debug][%s] %s: rst_pin = %d \n", LOG_FLAG, __func__, rst_pin);

		if (SIPA_TYPE_IN(chip_type, 0x19, 0x3a919c0)) {
			int irq_pin;

			ret = of_property_read_u32(np, "en_irq_func", &en_irq);
			if (ret < 0 || 1 != en_irq) {
				irq_pin = -1;
				pr_warn("[ warn] %s: No IRQ GPIO provided !\n", __func__);
			} else {
				irq_pin = of_get_named_gpio(np, "si,si_pa_irq", 0);
				if (irq_pin < 0) {
					pr_err("[  err][%s] %s: irq_pin < 0 !!! \n", LOG_FLAG, __func__);
					return -ENODEV;
				}
			}
			si_pa->irq_pin = irq_pin;
		} else if (20 == chip_type) {
			int boost_pin = of_get_named_gpio(np, "si,si_pa_boost", 0);

			if (boost_pin < 0) {
				pr_err("[  err][%s] %s: boost_pin < 0 !!! \n", LOG_FLAG, __func__);
				return -ENODEV;
			}
			si_pa->boost_pin = boost_pin;
		}

		if (SIPA_TYPE_IN(chip_type, 0x19, 0x3b919c0)) {
			int spksw_gpio = of_get_named_gpio(np, "spksw-gpio", 0);

			if (spksw_gpio < 0)
				pr_err("[  err][%s] %s: spksw_gpio < 0 !!! \n", LOG_FLAG, __func__);
			pr_info("[  info][%s] %s: spksw_gpio = %d \n", LOG_FLAG, __func__, spksw_gpio);
			si_pa->spksw_gpio = spksw_gpio;
		}

		if (SIPA_TYPE_IN(chip_type, 0xd, 0x2600)) {
			owi_pin = of_get_named_gpio(np, "si,si_pa_owi", 0);
			if (owi_pin < 0) {
				pr_err("[  err][%s] %s: owi_pin < 0 !!! \n", LOG_FLAG, __func__);
				return -ENODEV;
			}
			pr_debug("[debug][%s] %s: owi_pin = %d \n", LOG_FLAG, __func__, owi_pin);
		}

		if (1 == si_pa->en_dyn_id) {
			int id_pin = of_get_named_gpio(np, "si,si_pa_id", 0);

			if (id_pin < 0) {
				pr_err("[  err][%s] %s: id_pin < 0 !!! \n", LOG_FLAG, __func__);
				return -ENODEV;
			}
			si_pa->id_pin = id_pin;
			pr_debug("[debug][%s] %s: id_pin = %d \n", LOG_FLAG, __func__, id_pin);
		}
	}

	si_pa->rst_pin = rst_pin;
	si_pa->owi_pin = owi_pin;

	return 0;
}

static __always_inline int sipa_pinctrl_select(
	sipa_dev_t *si_pa,
	struct platform_device *pdev)
{
	struct pinctrl *pinctrl;
	struct pinctrl_state *state;
	int ret;
	bool ok = false;

	if (0 == si_pa->disable_pin) {
		pinctrl = devm_pinctrl_get(&pdev->dev);
		if (NULL == pinctrl) {
			pr_err("[  err][%s] %s: NULL == pinctrl !!! \n", LOG_FLAG, __func__);
			return -ENODEV;
		}

		state = pinctrl_lookup_state(pinctrl, "si_pa_gpio");
		if (NULL == state) {
			pr_err("[  err][%s] %s: NULL == pinctrl_state !!! \n", LOG_FLAG, __func__);
			goto put_pinctrl;
		}

		ret = pinctrl_select_state(pinctrl, state);
		if (0 != ret) {
			pr_err("[  err][%s] %s: error pinctrl_select_state return %d \n",
				LOG_FLAG, __func__, ret);
			goto put_pinctrl;
		}

		if (SIPA_TYPE_IN(si_pa->chip_type, 0x19, 0x3a919c0)) {
			if (si_pa->irq_pin >= 1) {
				state = pinctrl_lookup_state(pinctrl, "si_pa_irq");
				if (NULL == state) {
					pr_err("[  err][%s] %s: NULL == pinctrl_state !!! \n",
						LOG_FLAG, __func__);
					goto put_pinctrl;
				}
				ret = pinctrl_select_state(pinctrl, state);
				if (0 != ret) {
					pr_err("[  err][%s] %s: error pinctrl_select_state return %d \n",
						LOG_FLAG, __func__, ret);
					goto put_pinctrl;
				}
			}
		} else if (20 == si_pa->chip_type) {
			if (si_pa->boost_pin >= 1) {
				state = pinctrl_lookup_state(pinctrl, "si_pa_boost");
				if (NULL == state) {
					pr_err("[  err][%s] %s: si_pa_boost NULL == pinctrl_state !!! \n",
						LOG_FLAG, __func__);
					goto put_pinctrl;
				}
				ret = pinctrl_select_state(pinctrl, state);
				if (0 != ret) {
					pr_err("[  err][%s] %s: error si_pa_boost pinctrl_select_state return %d \n",
						LOG_FLAG, __func__, ret);
					goto put_pinctrl;
				}
				if (si_pa->boost_pin >= 0)
					gpiod_direction_output_raw(gpio_to_desc(si_pa->boost_pin), 0);
			}
		}

		if (1 == si_pa->en_dyn_id) {
			state = pinctrl_lookup_state(pinctrl, "si_pa_id");
			if (NULL == state) {
				pr_err("[  err][%s] %s: NULL == pinctrl_state !!! \n", LOG_FLAG, __func__);
				goto put_pinctrl;
			}
			ret = pinctrl_select_state(pinctrl, state);
			if (0 != ret) {
				pr_err("[  err][%s] %s: error pinctrl_select_state return %d \n",
					LOG_FLAG, __func__, ret);
				goto put_pinctrl;
			}
			gpiod_direction_input(gpio_to_desc(si_pa->id_pin));
		}

		if (SIPA_TYPE_IN(si_pa->chip_type, 0x19, 0x3b919c0) && si_pa->irq_pin >= 0) {
			ret = devm_gpio_request_one(&pdev->dev, si_pa->irq_pin, GPIOF_IN, "SIA91XX_INT");
			if (0 != ret) {
				pr_err("%s: fail to request irq gpio %d", __func__, ret);
				return -ENODEV;
			}
		}

		ok = true;
		if (SIPA_TYPE_IN(si_pa->chip_type, 0xd, 0x2600))
			gpiod_direction_output_raw(gpio_to_desc(si_pa->owi_pin), 0);

put_pinctrl:
		devm_pinctrl_put(pinctrl);
		return ok ? 0 : -ENODEV;
	}

	return 0;
}

static int sipa_probe(struct platform_device *pdev)
{
	int ret = 0;
	sipa_dev_t *si_pa = NULL;
	char work_name[20];

	if (NULL == pdev) {
		pr_err("[  err][%s] %s: pdev is NULL !!! \n", LOG_FLAG, __func__);
		return -ENODEV;
	}

	pr_info("[ info][%s] %s: probe \n", LOG_FLAG, __func__);

	if (!check_smartpa_type("sia91xx")) {
		pr_warn("[ warn][%s] %s: smartpa type %d already probed, abort",
			LOG_FLAG, __func__, get_smartpa_type());
		return -ENODEV;
	}

	si_pa = create_sipa_dev(pdev);
	if (NULL == si_pa) {
		pr_err("[  err][%s] %s: get_si_pa_dev error !!! \n",
			LOG_FLAG, __func__);
		return -ENODEV;
	}

	ret = sipa_property_init(si_pa, pdev->dev.of_node);
	if (0 != ret) {
		pr_err("[  err][%s] %s: sipa_property_init error !!! \n",
			LOG_FLAG, __func__);
		goto put_dev_out;
	}

	ret = sipa_pinctrl_select(si_pa, pdev);
	if (0 != ret) {
		pr_err("[  err][%s] %s: sipa_pinctrl_select error !!! \n",
			LOG_FLAG, __func__);
		goto put_dev_out;
	}

	spin_lock_init(&si_pa->owi_lock);

	/* the SIA91xx family registers its component from the i2c probe */
	if (!SIPA_TYPE_IN(si_pa->chip_type, 0x19, 0x3b919c0)) {
		ret = devm_snd_soc_register_component(&pdev->dev, &soc_component_dev_sipa, NULL, 0);
		if (0 != ret) {
			pr_err("[  err][%s] %s: snd_soc_register_codec fail \n",
				LOG_FLAG, __func__);
			goto put_dev_out;
		}
	}

	/* save platform dev, and sava it to the dev's driver data */
	si_pa->pdev = pdev;
	dev_set_drvdata(&pdev->dev, si_pa);

	if (1 == si_pa->en_dyn_id)
		distinguish_chip_type(si_pa);

	snprintf(work_name, 20, "sipa_%d", si_pa->channel_num);
	si_pa->sia91xx_wq = create_singlethread_workqueue(work_name);
	if (!si_pa->sia91xx_wq) {
		ret = -ENOMEM;
		pr_err("[  err][%s] %s: create singlethread fail \n", LOG_FLAG, __func__);
		goto put_dev_out;
	}

	if (SIPA_TYPE_IN(si_pa->chip_type, 0xd, 0x2600))
		device_create_file(&pdev->dev, &dev_attr_sipa_cmd);

	/* load firmware */
	sipa_param_load_fw(&pdev->dev, "sipa.bin");

	pr_info("[ info][%s] %s: finish, channel:%d\n", LOG_FLAG, __func__, si_pa->channel_num);

	return 0;

put_dev_out:
	put_sipa_dev(si_pa);

	return ret;
}

static int sipa_remove(struct platform_device *pdev)
{
	int ret = 0;
	sipa_dev_t *si_pa = NULL;

	if (NULL == pdev) {
		pr_err("[  err][%s] %s: pdev is NULL !!! \n", LOG_FLAG, __func__);
		return -ENODEV;
	}

	pr_debug("[debug][%s] %s: remove \n", LOG_FLAG, __func__);

	si_pa = (sipa_dev_t *)dev_get_drvdata(&pdev->dev);
	if (NULL == si_pa) {
		pr_err("[  err][%s] %s: si_pa is NULL \n", LOG_FLAG, __func__);
		return 0;
	}

	ret = sipa_suspend(si_pa);
	if (0 != ret) {
		pr_err("[  err][%s] %s: si_pa_dev_remove return : %d \n",
			LOG_FLAG, __func__, ret);
	}

	sipa_param_release();
	cancel_delayed_work_sync(&si_pa->fw_load_work);
	if (si_pa->sia91xx_wq)
		destroy_workqueue(si_pa->sia91xx_wq);

	si_pa->pdev = NULL;

	put_sipa_dev(si_pa);

	return 0;
}

static void sipa_shutdown(struct platform_device *pdev)
{
	sipa_dev_t *si_pa = NULL;

	pr_debug("[debug][%s] %s: running \n", LOG_FLAG, __func__);

	if (NULL == pdev)
		return ;

	si_pa = (sipa_dev_t *)dev_get_drvdata(&pdev->dev);
	if (NULL == si_pa) {
		pr_err("[  err][%s] %s: si_pa is NULL \n", LOG_FLAG, __func__);
		return ;
	}

	if (SIPA_TYPE_IN(si_pa->chip_type, 0xd, 0x2600))
		sipa_suspend(si_pa);
}

#ifdef CONFIG_OF
static const struct of_device_id si_sipa_dt_match[] = {
	{ .compatible = "si,sia81xx" },
	{ .compatible = "si,sia8100" },
	{ .compatible = "si,sia8101" },
	{ .compatible = "si,sia8109" },
	{ .compatible = "si,sia8152" },
	{ .compatible = "si,sia8152s" },
	{ .compatible = "si,sia8159" },
	{ .compatible = "si,sia81x9" },
	{ .compatible = "si,sia8152x" },
	{ .compatible = "si,sia91xx" },
	{ .compatible = "si,sia8132" },
	{ .compatible = "si,sia51xx" },
	{ }
};
MODULE_DEVICE_TABLE(of, si_sipa_dt_match);
#endif

static struct platform_driver si_sipa_dev_driver = {
	.probe  = sipa_probe,
	.remove = sipa_remove,
	.shutdown = sipa_shutdown,
	.driver = {
		.name  = SIPA_NAME,
		.owner = THIS_MODULE,
		.pm = &si_sipa_pm_ops,
#ifdef CONFIG_OF
		.of_match_table = si_sipa_dt_match,
#endif
	},
};

/********************************************************************
 * end - sia81xx dev driver
 ********************************************************************/

static int __init sipa_pa_init(void)
{
	int ret = 0;

	pr_info("[ info][%s] %s: si_pa driver version : %s \n",
		LOG_FLAG, __func__, SIPA_DRIVER_VERSION);

	//init online tuning funtion
#ifdef SIPA_TUNING
	sipa_sock_init();
#endif

	ret = platform_driver_register(&si_sipa_dev_driver);
	if (ret) {
		pr_err("[  err][%s] %s: si_sipa_dev error, ret = %d !!! \n",
			LOG_FLAG, __func__, ret);
		return ret;
	}
#ifndef PLATFORM_TYPE_MTK
	ret = i2c_add_driver(&si_sipa_i2c_driver);
	if (ret) {
		pr_err("[  err][%s] %s: i2c_add_driver error, ret = %d !!! \n",
			LOG_FLAG, __func__, ret);
		return ret;
	}
#endif
	return 0;
}

static void __exit sipa_pa_exit(void)
{
	pr_info("[ info][%s] %s: running \n", LOG_FLAG, __func__);

	//exit online tuning funtion
#ifdef SIPA_TUNING
	sipa_sock_exit();
#endif

#ifndef PLATFORM_TYPE_MTK
	i2c_del_driver(&si_sipa_i2c_driver);
#endif
	platform_driver_unregister(&si_sipa_dev_driver);
}

module_init(sipa_pa_init);
module_exit(sipa_pa_exit);

MODULE_SOFTDEP("pre: aw882xx_dlkm");
MODULE_AUTHOR("yun shi <yun.shi@si-in.com>");
MODULE_DESCRIPTION("SI-IN SIA81xx ASoC driver");
MODULE_LICENSE("GPL");
