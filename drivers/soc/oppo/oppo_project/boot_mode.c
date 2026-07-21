// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/sysfs.h>
#include <linux/kobject.h>
#include <linux/string.h>
#include <soc/oppo/boot_mode.h>

static struct kobject *systeminfo_kobj;

#define MAX_CMD_LENGTH 32

static int ftm_mode = MSM_BOOT_MODE__NORMAL;

static bool __init get_cmdline_value(const char *key, char *value,
				     size_t value_size)
{
	const char *cursor = boot_command_line;
	size_t key_length = strlen(key);
	size_t value_length = 0;

	if (!value_size)
		return false;

	while ((cursor = strstr(cursor, key))) {
		if (cursor == boot_command_line || cursor[-1] == ' ')
			break;
		cursor += key_length;
	}
	if (!cursor)
		return false;

	cursor += key_length;
	while (cursor[value_length] && cursor[value_length] != ' ' &&
	       value_length < value_size - 1)
		value_length++;

	memcpy(value, cursor, value_length);
	value[value_length] = '\0';
	return true;
}

int __init board_mfg_mode_init(void)
{
	char mode[MAX_CMD_LENGTH + 1];

	if (get_cmdline_value("oppo_ftm_mode=", mode, sizeof(mode))) {
		if (!strcmp(mode, "factory2")) {
			ftm_mode = MSM_BOOT_MODE__FACTORY;
			pr_err("kernel ftm OK\r\n");
		} else if (!strcmp(mode, "ftmwifi"))
			ftm_mode = MSM_BOOT_MODE__WLAN;
		else if (!strcmp(mode, "ftmmos"))
			ftm_mode = MSM_BOOT_MODE__MOS;
		else if (!strcmp(mode, "ftmrf"))
			ftm_mode = MSM_BOOT_MODE__RF;
		else if (!strcmp(mode, "ftmrecovery"))
			ftm_mode = MSM_BOOT_MODE__RECOVERY;
		else if (!strcmp(mode, "ftmsilence"))
			ftm_mode = MSM_BOOT_MODE__SILENCE;
		else if (!strcmp(mode, "ftmsau"))
			ftm_mode = MSM_BOOT_MODE__SAU;
		else if (!strcmp(mode, "ftmsafe"))
			ftm_mode = MSM_BOOT_MODE__SAFE;
	}

	pr_err("board_mfg_mode_init, ftm_mode=%d\n", ftm_mode);

	return 0;
}
//__setup("oppo_ftm_mode=", board_mfg_mode_init);

int get_boot_mode(void)
{
	return ftm_mode;
}
EXPORT_SYMBOL(get_boot_mode);

static ssize_t ftmmode_show(struct kobject *kobj, struct kobj_attribute *attr,
			     char *buf)
{
	return sprintf(buf, "%d\n", ftm_mode);
}

static struct kobj_attribute ftmmode_attr = {
	.attr = {"ftmmode", 0644},

	.show = &ftmmode_show,
};

static ssize_t closemodem_store(struct kobject *kobj,
			       struct kobj_attribute *attr,
			       const char *buf, size_t count)
{
	if (count && buf[0] == '1')
		msleep(4000);

	return count;
}

static struct kobj_attribute closemodem_attr = {
	.attr = {"closemodem", 0644},
	.store = closemodem_store,
};

static struct attribute *systeminfo_attrs[] = {
	&ftmmode_attr.attr,
	&closemodem_attr.attr,
	NULL,
};

static struct attribute_group attr_group = {
	.attrs = systeminfo_attrs,
};

/* OPPO 2013-09-03 heiwei add for add interface start reason and boot_mode begin */
char pwron_event[MAX_CMD_LENGTH + 1];
static int __init start_reason_init(void)
{
	if (!get_cmdline_value("androidboot.startupmode=", pwron_event,
			       sizeof(pwron_event)))
		return 0;

	pr_info("%s: parse poweron reason %s\n", __func__, pwron_event);

	return 0;
}
//__setup("androidboot.startupmode=", start_reason_setup);

char boot_mode[MAX_CMD_LENGTH + 1];

/*Fuchun.Liao@Mobile.BSP.CHG 2016-01-14 add for charge*/
bool qpnp_is_power_off_charging(void)
{
	return !strcmp(boot_mode, "charger");
}
EXPORT_SYMBOL(qpnp_is_power_off_charging);

/*PengNan@SW.BSP add for detect charger when reboot 2016-04-22*/
char charger_reboot[MAX_CMD_LENGTH + 1];
bool qpnp_is_charger_reboot(void)
{
	pr_err("%s charger_reboot:%s\n", __func__, charger_reboot);
	return !strcmp(charger_reboot, "1");
}
EXPORT_SYMBOL(qpnp_is_charger_reboot);

static int __init oppo_charger_reboot(void)
{
	if (!get_cmdline_value("oppo_charger_present=", charger_reboot,
			       sizeof(charger_reboot)))
		return 0;

	pr_info("%s: parse charger_reboot %s\n", __func__, charger_reboot);
	return 0;
}


int __init board_boot_mode_init(void)
{
	if (get_cmdline_value("androidboot.mode=", boot_mode,
			      sizeof(boot_mode)))
		pr_err("board_boot_mode_init boot_mode=%s\n", boot_mode);

	return 0;

}

static int __init boot_mode_init(void)
{
	int rc = 0;

	pr_err("%s: parse boot_mode\n", __func__);

	board_boot_mode_init();

	/* OPPO 2013.07.09 hewei add begin for factory mode*/
	board_mfg_mode_init();
	/* OPPO 2013.07.09 hewei add end */

/* OPPO 2013-09-03 heiwei add for add interface start reason and boot_mode begin */
	start_reason_init();
	/*PengNan@SW.BSP add for detect charger when reboot 2016-04-22*/
	oppo_charger_reboot();
/* OPPO 2013-09-03 zhanglong add for add interface start reason and boot_mode end */
	/* OPPO 2013.07.09 hewei add begin for factory mode*/
	systeminfo_kobj = kobject_create_and_add("systeminfo", NULL);
	if (!systeminfo_kobj)
		return -ENOMEM;

	rc = sysfs_create_group(systeminfo_kobj, &attr_group);
	if (rc) {
		kobject_put(systeminfo_kobj);
		systeminfo_kobj = NULL;
	}
	/* OPPO 2013.07.09 hewei add end */

	return rc;
}
//__setup("androidboot.mode=", boot_mode_setup);

arch_initcall(boot_mode_init);

MODULE_LICENSE("GPL v2");
