// SPDX-License-Identifier: GPL-2.0
/* matt congestion control (stock: mp_cc.c; BBR derived from net/ipv4/tcp_bbr.c, minmax from lib/win_minmax.c). Reconstructed from the OS4 stock disassembly. DO NOT LOAD before review. */
#include "matt.h"

static u32 minmax_reset_m(struct minmax *m, u32 t, u32 meas)
{
	struct minmax_sample val = { .t = t, .v = meas };

	m->s[2] = m->s[1] = m->s[0] = val;
	return m->s[0].v;
}

static u32 minmax_subwin_update_m(struct minmax *m, u32 win, const struct minmax_sample *val)
{
	u32 dt = val->t - m->s[0].t;

	if (unlikely(dt > win)) {
		m->s[0] = m->s[1];
		m->s[1] = m->s[2];
		m->s[2] = *val;
		if (unlikely(val->t - m->s[0].t > win)) {
			m->s[0] = m->s[1];
			m->s[1] = *val;
			if (unlikely(val->t - m->s[0].t > win))
				m->s[0] = *val;
		}
	} else if (unlikely(m->s[1].t == m->s[0].t) && dt > win / 4) {
		m->s[2] = m->s[1] = *val;
	} else if (unlikely(m->s[2].t == m->s[1].t) && dt > win / 2) {
		m->s[2] = *val;
	}
	return m->s[0].v;
}

u32 matt_minmax_running_max(struct minmax *m, u32 win, u32 t, u32 meas)
{
	struct minmax_sample val = { .t = t, .v = meas };

	if (unlikely(val.v >= m->s[0].v) || unlikely(val.t - m->s[2].t > win))
		return minmax_reset_m(m, t, meas);
	if (unlikely(val.v >= m->s[1].v))
		m->s[2] = m->s[1] = val;
	else if (unlikely(val.v >= m->s[2].v))
		m->s[2] = val;
	return minmax_subwin_update_m(m, win, &val);
}

u32 matt_minmax_running_min(struct minmax *m, u32 win, u32 t, u32 meas)
{
	struct minmax_sample val = { .t = t, .v = meas };

	if (unlikely(val.v <= m->s[0].v) || unlikely(val.t - m->s[2].t > win))
		return minmax_reset_m(m, t, meas);
	if (unlikely(val.v <= m->s[1].v))
		m->s[2] = m->s[1] = val;
	else if (unlikely(val.v <= m->s[2].v))
		m->s[2] = val;
	return minmax_subwin_update_m(m, win, &val);
}
