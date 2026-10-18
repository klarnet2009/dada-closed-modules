// SPDX-License-Identifier: GPL-2.0
/*
 * G1w (17.10.2026): the four documented stubs of G1p..G1t (get_cluster_type, xr_set_next_freq, is_render_sibling, set_vip_flag) are
 * replaced by real code (migt.c / mi_game_load.c). Nothing is stubbed in the Kleaf build any more; the file is kept only so that
 * BUILD.bazel stays unchanged. Callees that are still NOT written (see STATUS.md) would need new stubs here, under MI_GAME_KLEAF_STUBS.
 */
#ifdef MI_GAME_KLEAF_STUBS
#include "mi_game.h"
#endif
