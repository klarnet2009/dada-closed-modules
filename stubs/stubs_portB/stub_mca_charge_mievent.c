// SPDX-License-Identifier: GPL-2.0
/* Stub provider of mca_charge_mievent (only the export CRCs matter; the include set is chosen so that genksyms reproduces the stock CRCs). */
#include <linux/module.h>
#include <mca/common/mca_charge_mievent.h>

void mca_charge_mievent_report(int event_index, void *data, int size) { }
EXPORT_SYMBOL_GPL(mca_charge_mievent_report);
void mca_charge_mievent_set_state(enum charge_mievent_state_ele state, int value) { }
EXPORT_SYMBOL_GPL(mca_charge_mievent_set_state);

