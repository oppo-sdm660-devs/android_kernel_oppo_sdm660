// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C), 2008-2017, OPPO Mobile Comm Corp., Ltd */
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/gpio.h>
#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/proc_fs.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <soc/oppo/oppo_project.h>

#include "../include/oppo_fp_common.h"

#define CHIP_PRIMAX "primax"
#define CHIP_CT "CT"
#define CHIP_OFILM "ofilm"
#define CHIP_QTECH "Qtech"
#define CHIP_TRULY "truly"
#define CHIP_GOODIX "G"
#define CHIP_FPC "F"
#define CHIP_UNKNOWN "unknown"

#define ENGINEER_MENU_FPC1022 "-1,-1"
#define ENGINEER_MENU_FPC1023 "1,-1"
#define ENGINEER_MENU_FPC1140 "-1,-1"
#define ENGINEER_MENU_FPC1260 "1,-1"
#define ENGINEER_MENU_FPC1270 "-1,-1"
#define ENGINEER_MENU_GOODIX_3268 "-1,-1"
#define ENGINEER_MENU_GOODIX_5288 "-1,-1"
#define ENGINEER_MENU_GOODIX_5298 "-1,-1"
#define ENGINEER_MENU_DEFAULT "-1,-1"

static const char *fp_id_name = "fp_id";
static char fp_manu[FP_ID_MAX_LENGTH] = CHIP_UNKNOWN;
static char g_engineermode_menu_config[ENGINEER_MENU_SELECT_MAXLENTH] =
	ENGINEER_MENU_DEFAULT;

static struct fp_data *fp_data_ptr;
static struct proc_dir_entry *fp_id_dir;

struct fp_module_config_info {
	const struct fp_module_gpio_config_info *list;
	int count;
};

static const struct fp_module_gpio_config_info fp_16051_config[] = {
	{ { 1, 1, 1 }, FP_FPC_1140, CHIP_OFILM, ENGINEER_MENU_FPC1140 },
	{ { 0, 0, 1 }, FP_FPC_1140, CHIP_PRIMAX, ENGINEER_MENU_FPC1140 },
	{ { 0, 1, 0 }, FP_FPC_1140, CHIP_TRULY, ENGINEER_MENU_FPC1140 },
	{ { 1, 0, 1 }, FP_FPC_1260, CHIP_OFILM, ENGINEER_MENU_FPC1260 },
	{ { 0, 0, 0 }, FP_FPC_1260, CHIP_PRIMAX, ENGINEER_MENU_FPC1260 },
	{ { 1, 1, 0 }, FP_FPC_1260, CHIP_TRULY, ENGINEER_MENU_FPC1260 },
	{ { 1, 0, 0 }, FP_FPC_1260, CHIP_QTECH, ENGINEER_MENU_FPC1260 },
	{ { 0, 0, -1 }, FP_FPC_1140, CHIP_PRIMAX, ENGINEER_MENU_FPC1140 },
	{ { 0, 1, -1 }, FP_FPC_1140, CHIP_TRULY, ENGINEER_MENU_FPC1140 },
	{ { 1, 0, -1 }, FP_FPC_1140, CHIP_QTECH, ENGINEER_MENU_FPC1140 },
	{ { 1, 1, -1 }, FP_FPC_1140, CHIP_OFILM, ENGINEER_MENU_FPC1140 },
};

/* R11 fingerprint modules. */
static const struct fp_module_gpio_config_info fp_common_config[] = {
	{ { 0, 0, 1 }, FP_FPC_1023_GLASS, CHIP_FPC, ENGINEER_MENU_FPC1023 },
	{ { 0, 1, 1 }, FP_FPC_1022, CHIP_FPC, ENGINEER_MENU_FPC1022 },
	{ { 0, 1, 0 }, FP_FPC_1023, CHIP_FPC, ENGINEER_MENU_FPC1023 },
	{ { 1, 0, 0 }, FP_FPC_1023, CHIP_FPC, ENGINEER_MENU_FPC1023 },
	{ { 1, 0, 1 }, FP_GOODIX_3268, CHIP_GOODIX, ENGINEER_MENU_GOODIX_3268 },
	{ { 1, 1, 0 }, FP_GOODIX_5288, CHIP_GOODIX, ENGINEER_MENU_GOODIX_5288 },
	{ { 1, 1, 1 }, FP_FPC_1022, CHIP_FPC, ENGINEER_MENU_FPC1022 },
};

/* R11s fingerprint modules. */
static const struct fp_module_gpio_config_info fp_r11s_config[] = {
	{ { 0, 0, 0 }, FP_FPC_1270, CHIP_FPC, ENGINEER_MENU_FPC1270 },
	{ { 0, 1, 0 }, FP_FPC_1023, CHIP_FPC, ENGINEER_MENU_FPC1023 },
	{ { 0, 1, 1 }, FP_FPC_1022, CHIP_FPC, ENGINEER_MENU_FPC1022 },
	{ { 1, 0, 0 }, FP_FPC_1023, CHIP_FPC, ENGINEER_MENU_FPC1023 },
	{ { 1, 0, 1 }, FP_GOODIX_3268, CHIP_GOODIX, ENGINEER_MENU_GOODIX_3268 },
	{ { 1, 1, 0 }, FP_GOODIX_5288, CHIP_GOODIX, ENGINEER_MENU_GOODIX_5288 },
	{ { 1, 1, 1 }, FP_FPC_1022, CHIP_FPC, ENGINEER_MENU_FPC1022 },
};

static const struct fp_module_config_info fp_16051_info = {
	.list = fp_16051_config,
	.count = ARRAY_SIZE(fp_16051_config),
};
static const struct fp_module_config_info fp_common_info = {
	.list = fp_common_config,
	.count = ARRAY_SIZE(fp_common_config),
};
static const struct fp_module_config_info fp_r11s_info = {
	.list = fp_r11s_config,
	.count = ARRAY_SIZE(fp_r11s_config),
};

static const struct fp_module_config_info *fp_config_for_project(void)
{
	unsigned int project = get_project();

	switch (project) {
	case OPPO_16051:
		return &fp_16051_info;
	case OPPO_16103:
	case OPPO_16118:
		return &fp_common_info;
	case OPPO_17011:
	case OPPO_17021:
		return &fp_r11s_info;
	default:
		return &fp_common_info;
	}
}

static int fp_request_named_gpio(struct fp_data *fp_data, const char *label,
				 int *gpio)
{
	struct device *dev = fp_data->dev;
	struct device_node *np = dev->of_node;
	int ret;

	ret = of_get_named_gpio(np, label, 0);
	if (ret < 0) {
		dev_err(dev, "failed to get '%s'\n", label);
		return FP_ERROR_GPIO;
	}

	*gpio = ret;
	ret = devm_gpio_request(dev, *gpio, label);
	if (ret) {
		dev_err(dev, "failed to request gpio %d\n", *gpio);
		return FP_ERROR_GPIO;
	}

	dev_err(dev, "%s - gpio: %d\n", label, *gpio);
	return FP_OK;
}

static int fp_gpio_parse_dts(struct fp_data *fp_data)
{
	int ret = FP_OK;

	if (!fp_data)
		return FP_ERROR_GENERAL;

	ret = fp_request_named_gpio(fp_data, "oppo,fp-id1", &fp_data->gpio_id1);
	if (ret)
		return ret;

	ret = fp_request_named_gpio(fp_data, "oppo,fp-id2", &fp_data->gpio_id2);
	if (ret)
		return ret;

	ret = fp_request_named_gpio(fp_data, "oppo,fp-id3", &fp_data->gpio_id3);
	if (ret)
		return ret;

	return FP_OK;
}

static void fp_set_manu(fp_vendor_t type, const char *suffix, const char *menu)
{
	strscpy(fp_manu, CHIP_FPC, sizeof(fp_manu));
	switch (type) {
	case FP_GOODIX_3268:
	case FP_GOODIX_5288:
	case FP_GOODIX_5298:
		strscpy(fp_manu, CHIP_GOODIX, sizeof(fp_manu));
		break;
	default:
		break;
	}
	strlcat(fp_manu, suffix, sizeof(fp_manu));
	strscpy(g_engineermode_menu_config, menu,
		sizeof(g_engineermode_menu_config));
}

static fp_vendor_t fp_get_matched_chip_module(struct device *dev, int fp_id1,
					      int fp_id2, int fp_id3)
{
	const struct fp_module_config_info *cfg = fp_config_for_project();
	int i;

	for (i = 0; i < cfg->count; i++) {
		const struct fp_module_gpio_config_info *m = &cfg->list[i];

		if ((m->gpio_id_config_list[0] == fp_id1) &&
		    (m->gpio_id_config_list[1] == fp_id2) &&
		    (m->gpio_id_config_list[2] == fp_id3)) {
			switch (m->fp_vendor_chip) {
			case FP_FPC_1022:
				fp_set_manu(FP_FPC_1022, "_1022",
					    ENGINEER_MENU_FPC1022);
				return FP_FPC_1022;
			case FP_FPC_1023:
				fp_set_manu(FP_FPC_1023, "_1023",
					    ENGINEER_MENU_FPC1023);
				return FP_FPC_1023;
			case FP_FPC_1023_GLASS:
				fp_set_manu(FP_FPC_1023_GLASS, "_1023_GLASS",
					    ENGINEER_MENU_FPC1023);
				return FP_FPC_1023_GLASS;
			case FP_FPC_1140:
				fp_set_manu(FP_FPC_1140, "_1140",
					    ENGINEER_MENU_FPC1140);
				return FP_FPC_1140;
			case FP_FPC_1260:
				fp_set_manu(FP_FPC_1260, "_1260",
					    ENGINEER_MENU_FPC1260);
				return FP_FPC_1260;
			case FP_FPC_1270:
				fp_set_manu(FP_FPC_1270, "_1270",
					    ENGINEER_MENU_FPC1270);
				return FP_FPC_1270;
			case FP_GOODIX_3268:
				fp_set_manu(FP_GOODIX_3268, "_3268",
					    ENGINEER_MENU_GOODIX_3268);
				return FP_GOODIX_3268;
			case FP_GOODIX_5288:
				fp_set_manu(FP_GOODIX_5288, "_5288",
					    ENGINEER_MENU_GOODIX_5288);
				return FP_GOODIX_5288;
			default:
				dev_err(dev,
					"gpio ids matched but no matched vendor chip!\n");
				return FP_UNKNOWN;
			}
		}
	}

	strscpy(fp_manu, CHIP_UNKNOWN, sizeof(fp_manu));
	strscpy(g_engineermode_menu_config, ENGINEER_MENU_DEFAULT,
		sizeof(g_engineermode_menu_config));
	return FP_UNKNOWN;
}

static ssize_t fp_id_node_read(struct file *file, char __user *buf,
			       size_t count, loff_t *pos)
{
	return simple_read_from_buffer(buf, count, pos, fp_manu,
				       strlen(fp_manu));
}

static ssize_t fp_id_node_write(struct file *file, const char __user *buf,
				size_t count, loff_t *pos)
{
	size_t local_count;

	if (count <= 0)
		return 0;

	strscpy(fp_manu, CHIP_UNKNOWN, sizeof(fp_manu));
	local_count = min(sizeof(fp_manu) - 1, count);
	if (copy_from_user(fp_manu, buf, local_count))
		return -EFAULT;
	fp_manu[local_count] = '\0';

	if (fp_data_ptr)
		dev_info(fp_data_ptr->dev, "write fp manu = %s\n", fp_manu);
	return count;
}

static const struct file_operations fp_id_node_ctrl = {
	.read = fp_id_node_read,
	.write = fp_id_node_write,
};

static int fp_register_proc_fs(struct fp_data *fp_data)
{
	fp_data->fp_id1 = gpio_get_value(fp_data->gpio_id1);
	fp_data->fp_id2 = gpio_get_value(fp_data->gpio_id2);
	fp_data->fp_id3 = gpio_get_value(fp_data->gpio_id3);

	dev_err(fp_data->dev,
		"fp_register_proc_fs: fp_id1=%d fp_id2=%d fp_id3=%d\n",
		fp_data->fp_id1, fp_data->fp_id2, fp_data->fp_id3);

	fp_data->fpsensor_type =
		fp_get_matched_chip_module(fp_data->dev, fp_data->fp_id1,
					   fp_data->fp_id2, fp_data->fp_id3);

	fp_id_dir = proc_create(fp_id_name, 0666, NULL, &fp_id_node_ctrl);
	if (!fp_id_dir)
		return FP_ERROR_GENERAL;

	return FP_OK;
}

fp_vendor_t get_fpsensor_type(void)
{
	if (!fp_data_ptr) {
		pr_err("%s no device\n", __func__);
		return FP_UNKNOWN;
	}

	return fp_data_ptr->fpsensor_type;
}
EXPORT_SYMBOL(get_fpsensor_type);

static int oppo_fp_common_probe(struct platform_device *fp_dev)
{
	struct device *dev = &fp_dev->dev;
	struct fp_data *fp_data;
	int ret;

	ret = oppo_project_info_init();
	if (ret)
		return ret;

	fp_data = devm_kzalloc(dev, sizeof(*fp_data), GFP_KERNEL);
	if (!fp_data)
		return -ENOMEM;

	fp_data->dev = dev;

	ret = fp_gpio_parse_dts(fp_data);
	if (ret) {
		dev_err(dev, "fp_gpio_parse_dts failed ret=%d\n", ret);
		return ret;
	}

	ret = fp_register_proc_fs(fp_data);
	if (ret) {
		dev_err(dev, "fp_register_proc_fs failed ret=%d\n", ret);
		return ret;
	}

	fp_data_ptr = fp_data;
	platform_set_drvdata(fp_dev, fp_data);
	dev_info(dev, "oppo fp common probed, sensor=%d\n",
		 fp_data->fpsensor_type);
	return 0;
}

static int oppo_fp_common_remove(struct platform_device *fp_dev)
{
	proc_remove(fp_id_dir);
	fp_id_dir = NULL;
	fp_data_ptr = NULL;
	return 0;
}

static const struct of_device_id oppo_fp_common_match_table[] = {
	{
		.compatible = "oppo,fp_common",
	},
	{}
};

static struct platform_driver oppo_fp_common_driver = {
	.probe = oppo_fp_common_probe,
	.remove = oppo_fp_common_remove,
	.driver = {
		.name = "oppo_fp_common",
		.of_match_table = oppo_fp_common_match_table,
	},
};

static int __init oppo_fp_common_init(void)
{
	return platform_driver_register(&oppo_fp_common_driver);
}
subsys_initcall(oppo_fp_common_init);

static void __exit oppo_fp_common_exit(void)
{
	platform_driver_unregister(&oppo_fp_common_driver);
}
module_exit(oppo_fp_common_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("OPPO fingerprint common driver");
