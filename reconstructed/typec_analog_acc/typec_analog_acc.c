// SPDX-License-Identifier: GPL-2.0
/* Reconstructed from the stock typec_analog_acc.ko (Xiaomi 15 "dada") by reverse engineering; not original source. */
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/notifier.h>
#include <linux/usb/typec.h>
#include <linux/usb/ucsi_glink.h>
#include <sound/soc.h>
#include <sound/jack.h>

#define TYPEC_ANALOG_JACK_MASK 0x13

struct typec_analog_acc_priv {
	int acc;
	struct notifier_block nb;
	struct snd_soc_jack jack;
	char pad[0xd0 - 0x20 - sizeof(struct snd_soc_jack)];
};

static struct typec_analog_acc_priv *acc_priv;

static void typec_analog_acc_jack_report(struct typec_analog_acc_priv *priv, int status)
{
	pr_info("%s: enter, jack->status: %d, status: %d, mask: %d\n", __func__,
		priv->jack.status, status, TYPEC_ANALOG_JACK_MASK);
	snd_soc_jack_report(&priv->jack, status, TYPEC_ANALOG_JACK_MASK);
}

static int typec_analog_acc_event_changed(struct notifier_block *nb, unsigned long event, void *data)
{
	struct typec_analog_acc_priv *priv;
	struct ucsi_glink_constat_info *info = data;
	int acc, status;

	pr_info("%s: enter\n", __func__);
	priv = container_of(nb, struct typec_analog_acc_priv, nb);
	if (!priv)
		pr_err("%s: priv is NULL\n", __func__);
	acc = info->acc;
	pr_info("%s: acc = %d", __func__, acc);
	if (acc == TYPEC_ACCESSORY_AUDIO) {
		if (priv->acc == TYPEC_ACCESSORY_AUDIO)
			goto out;
		status = TYPEC_ANALOG_JACK_MASK;
	} else if (acc == TYPEC_ACCESSORY_NONE) {
		if (priv->acc == TYPEC_ACCESSORY_NONE)
			goto out;
		status = 0;
	} else {
		goto out;
	}
	typec_analog_acc_jack_report(priv, status);
	priv->acc = acc;
out:
	pr_info("%s: exit\n", __func__);
	return 0;
}

int typec_analog_acc_init(struct snd_soc_component *component)
{
	int ret;

	pr_info("%s: enter\n", __func__);
	acc_priv = kzalloc(sizeof(*acc_priv), GFP_KERNEL);
	if (!acc_priv)
		pr_err("%s: acc_priv is NULL\n", __func__);
	acc_priv->nb.notifier_call = typec_analog_acc_event_changed;
	acc_priv->nb.priority = 0;
	ret = register_ucsi_glink_notifier(&acc_priv->nb);
	if (ret) {
		pr_err("%s: ucsi glink notifier registration failed: %d\n", __func__, ret);
		return ret;
	}
	ret = snd_soc_card_jack_new(component->card, "Typec_analog Jack", TYPEC_ANALOG_JACK_MASK, &acc_priv->jack);
	if (ret) {
		pr_err("%s: Failed to create new jack Typec_analog Jack: %d\n", __func__, ret);
		return ret;
	}
	pr_info("%s: exit\n", __func__);
	return 0;
}

EXPORT_SYMBOL(typec_analog_acc_init);

static void __exit typec_analog_acc_exit(void)
{
	kfree(acc_priv);
}
module_exit(typec_analog_acc_exit);

static int __init typec_analog_acc_module_init(void)
{
	return 0;
}
module_init(typec_analog_acc_module_init);

MODULE_DESCRIPTION("type analog accessory module");
MODULE_LICENSE("GPL v2");
