# g4r 61: wlan_mlme_miracast_opt_timer_handler is a LOCAL symbol in the stock (compare_ko_v2 function-binding): static.
@@@ xiaomi_ext/xiaomi_ext.c
<<<<
void wlan_mlme_miracast_opt_timer_handler(void *arg);
====
static void wlan_mlme_miracast_opt_timer_handler(void *arg);
>>>>
<<<<
}

void wlan_mlme_miracast_opt_timer_handler(void *arg)
{
====
}

static void wlan_mlme_miracast_opt_timer_handler(void *arg)
{
>>>>
