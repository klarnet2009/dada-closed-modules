// SPDX-License-Identifier: GPL-2.0
/* matt logging helpers (stock: mp_main.c/mp_dump.c area). Reconstructed from the OS4 stock disassembly. DO NOT LOAD before review. */
#include "matt.h"
#include <linux/rtc.h>
#include <linux/time.h>

const u64 conversion_bits_scaled[5] = { 1000000, 1000, 1, 0, 0 };
const char *const label_bit[5] __attribute__((section(".data"))) = { "bps", "Kbps", "Mbps", "Gbps", "Tbps" };
static const char *const g_verbose_str[11] = { "", "F", "E", "W", "I", "IH", "IM", "IL", "D", "T", "" };
struct category_name_info g_matt_category_name[MATT_MODULE_ID_MAX] = {
	[0] = { "MAIN" },
	[8] = { "ANY" },
};

/* ThinLTO-promoted string constants (shared with another TU in the stock): GLOBAL HIDDEN objects */
#define LLVMS ".llvm.17829671557986578456"
extern const char matt_str_old_data[] __asm__(".str" LLVMS) __attribute__((visibility("hidden")));
extern const char matt_str_overwin[] __asm__(".str.1" LLVMS) __attribute__((visibility("hidden")));
extern const char matt_str_invalid[] __asm__(".str.2" LLVMS) __attribute__((visibility("hidden")));
const char matt_str_old_data[] = "DROP: OLD_DATA";
const char matt_str_overwin[] = "DROP: OVERWINDOW";
const char matt_str_invalid[] = "INVALID";

char *matt_drop_reason_to_str(enum matt_drop_reason type)
{
	switch (type) {
	case MATT_SKB_DROP_REASON_OLD_DATA:
		return (char *)matt_str_old_data;
	case MATT_SKB_DROP_REASON_OVERWINDOW:
		return (char *)matt_str_overwin;
	default:
		return (char *)matt_str_invalid;
	}
}

char *matt_types_to_str(u8 type)
{
	const char *const types_str[] = {
		"[ Connect Request ]", "[ Connect Respond ]", "[ Reset ]", "[ PACK ]", "[ NACK ]", "[ Ping ]", "[ Pong ]",
	};

	if (type <= 6)
		return (char *)types_str[(s8)type];
	return "INVALID";
}

void adaptive_bit_unit_to_str(char *s, int len, long long bps)
{
	long long integer, frac;
	int div;	/* stock keeps the divisor in an int: 1e12 truncates to -727379968 (tera bug, trace text only) */
	int unit;

	if (bps <= 999) {
		snprintf(s, len, "%lld %s", bps, label_bit[UNIT_CONV]);
		return;
	}
	if (bps >= 1000000000000LL) {
		unit = TERA_CONV;
		div = (int)1000000000000LL;
	} else if (bps >= 1000000000LL) {
		unit = GIGA_CONV;
		div = 1000000000LL;
	} else if (bps >= 1000000LL) {
		unit = MEGA_CONV;
		div = 1000000LL;
	} else {
		unit = KILO_CONV;
		div = 1000LL;
	}
	integer = bps / div;
	frac = (bps % div) * 100 / div;
	if (!frac)
		snprintf(s, len, "%lld %s", integer, label_bit[unit]);
	else if (!integer)
		snprintf(s, len, "0.%02lld %s", frac, label_bit[unit]);
	else
		snprintf(s, len, "%lld.%02lld %s", integer, frac, label_bit[unit]);
}

noinline void matt_get_time_of_the_day_in_hr_min_sec_usec(char *tbuf, int len)
{
	struct timespec64 ts = {};
	struct rtc_time tm = {};
	u32 local_time;

	ktime_get_real_ts64(&ts);
	local_time = (u32)(ts.tv_sec - (sys_tz.tz_minuteswest * 60));
	rtc_time64_to_tm(local_time, &tm);
	scnprintf(tbuf, len, "[%02d:%02d:%02d.%06lu]", tm.tm_hour, tm.tm_min, tm.tm_sec, ts.tv_nsec / 1000);
}

uint64_t matt_get_time_of_the_day_us(void)
{
	struct timespec64 ts = {};
	struct rtc_time tm = {};
	u32 local_time;

	ktime_get_real_ts64(&ts);
	local_time = (u32)(ts.tv_sec - (sys_tz.tz_minuteswest * 60));
	rtc_time64_to_tm(local_time, &tm);
	return (tm.tm_hour * 3600000000ULL) + (tm.tm_min * 60000000ULL) + (tm.tm_sec * 1000000ULL) + (u64)ts.tv_nsec / 1000;
}

void matt_trace_msg_cmn(unsigned int idx, MATT_MODULE_ID category, MATT_TRACE_LEVEL verbose, const char *str_format, va_list val)
{
	char buf[512];
	unsigned int len;

	if (!sysctl_matt_debug || !((sysctl_matt_debug >> verbose) & 1))
		return;
	memset(buf, 0, sizeof(buf));
	{
		char tbuf[18];

		memset(tbuf, 0, sizeof(tbuf));
		if (category >= MATT_MODULE_ID_MAX) {
			vscnprintf(buf, sizeof(buf), str_format, val);
			pr_info("%s: Invalid category: %d, log: %s\n", __func__, category, buf);
			return;
		}
		if (verbose >= MATT_TRACE_LEVEL_MAX) {
			vscnprintf(buf, sizeof(buf), str_format, val);
			pr_info("%s: Invalid verbose level %d, log: %s\n", __func__, verbose, buf);
			return;
		}
		matt_get_time_of_the_day_in_hr_min_sec_usec(tbuf, sizeof(tbuf));
		len = scnprintf(buf, sizeof(buf), "%s %s: [%d:%s:%s] ", tbuf, KBUILD_MODNAME, in_interrupt() ? 0 : current->pid,
				g_verbose_str[verbose], g_matt_category_name[category].category_name_str);
	}
	vscnprintf(&buf[len], (unsigned int)(512 - len), str_format, val);
	pr_err("%s\n", buf);
}

void matt_trace_msg(MATT_MODULE_ID module, MATT_TRACE_LEVEL level, const char *str_format GKPAD(fcrcr2lXe0hxAQ), ...)
{
	va_list ap;

	va_start(ap, str_format);
	matt_trace_msg_cmn(module, module, level, str_format, ap);
	va_end(ap);
}
EXPORT_SYMBOL(matt_trace_msg);

void matt_hex_dump(const char *fn, const char *prefix_str, int prefix_type, int rowsize, int groupsize,
		   const void *buf, size_t len, bool ascii)
{
	const u8 *ptr = buf;
	int i, linelen, remaining = len;

	if (rowsize != 16 && rowsize != 32)
		rowsize = 16;
	if (!len)
		return;
	{
	unsigned char linebuf[131] = {};

	for (i = 0; i < len; i += rowsize) {
		linelen = min(remaining, rowsize);
		remaining -= rowsize;
		hex_dump_to_buffer(ptr + i, linelen, rowsize, groupsize, linebuf, sizeof(linebuf), ascii);
		switch (prefix_type) {
		case DUMP_PREFIX_ADDRESS:
#line 55
			matt_trace_msg(0, 9, "%s: %d: %s%s%p: %s", __func__, __LINE__, fn, prefix_str, ptr + i, linebuf);
			break;
		case DUMP_PREFIX_OFFSET:
#line 58
			matt_trace_msg(0, 9, "%s: %d: %s%s%.8x: %s", __func__, __LINE__, fn, prefix_str, i, linebuf);
			break;
		default:
#line 61
			matt_trace_msg(0, 9, "%s: %d: %s%s%s", __func__, __LINE__, fn, prefix_str, linebuf);
			break;
		}
	}
	}
}

void matt_dump_creq(struct matt_creq *creq)
{
#line 203
	matt_trace_msg(0, 9, "%s: %d: [%s] matt hdr: num:%d offset:%d flags:%d type:%d bytes:%d", __func__, __LINE__, __func__,
		       ntohl(creq->hdr.num), ntohl(creq->hdr.offset), creq->hdr.flags, creq->hdr.type, ntohs(creq->hdr.len));
#line 211
	matt_trace_msg(0, 9, "%s: %d: [%s] creq hdr: version:%d mss:%d ts:%d snd_buf:%d rcv_buf:%d", __func__, __LINE__, __func__,
		       ntohs(creq->version), ntohs(creq->mss), ntohl(creq->ts), ntohl(creq->snd_buf), ntohl(creq->rcv_buf));
}

void matt_dump_crsp(struct matt_crsp *crsp)
{
#line 222
	matt_trace_msg(0, 9, "%s: %d: [%s] matt hdr: num:%d offset:%d flags:%d type:%d bytes:%d", __func__, __LINE__, __func__,
		       ntohl(crsp->hdr.num), ntohl(crsp->hdr.offset), crsp->hdr.flags, crsp->hdr.type, ntohs(crsp->hdr.len));
#line 231
	matt_trace_msg(0, 9, "%s: %d: [%s] crsp hdr: status:%d version:%d mss:%d ts:%d snd_buf:%d rcv_buf:%d", __func__, __LINE__, __func__,
		       ntohl(crsp->status), ntohs(crsp->version), ntohs(crsp->mss), ntohl(crsp->ts), ntohl(crsp->snd_buf), ntohl(crsp->rcv_buf));
}

void matt_dump_skb(const uint8_t *fn, struct sk_buff *skb)
{
	struct matthdr *mh = (struct matthdr *)skb->data;
	struct matt_skb_cb *cb = MATT_SKB_CB(skb);

#line 174
	MATT_TRACE(0, 9, "[%s] matt num:%u offset:%u reserved:%u flags:%d type:%d len:%d", fn, ntohl(mh->num),
		   ntohl(mh->offset), ntohl(mh->reserved), mh->flags, mh->type, ntohs(mh->len));
#line 189
	MATT_TRACE(0, 9, "[%s] matt cb seq(%u,%u) gso_segs:%u gso_size:%u flags:%d type:%d skb len:%u hlen:%u dlen:%u gso:%u non_linear:%u cloned:%u destructor:%p",
		   fn, cb->offset_start, cb->offset_end, cb->gso_segs, cb->gso_size, cb->flags, cb->types, skb->len,
		   skb_headlen(skb), skb->data_len, skb_is_gso(skb), skb_is_nonlinear(skb), skb_cloned(skb), skb->destructor);
}
