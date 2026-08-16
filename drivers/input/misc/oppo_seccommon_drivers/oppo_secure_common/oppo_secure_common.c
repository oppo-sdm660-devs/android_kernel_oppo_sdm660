// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C), 2008-2017, OPPO Mobile Comm Corp., Ltd */
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/proc_fs.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/uaccess.h>

#include "../include/oppo_secure_common.h"

#define OEM_FUSE_OFF "0"
#define OEM_FUSE_ON "1"
#define UNKNOW_FUSE_VALUE "unkown fuse"
#define FUSE_VALUE_LEN 15

/* SDM660 secure boot fuses */
#define OEM_SEC_BOOT_REG 0x780350
#define OEM_SEC_ENABLE_ANTIROLLBACK_REG 0x78019c
#define OEM_SEC_OVERRIDE_1_REG 0x7860C4
#define OEM_OVERRIDE_1_ENABLED_VALUE 0xffffffff

static struct proc_dir_entry *oppo_secure_common_dir;
static const char *oppo_secure_common_dir_name = "oppo_secure_common";
static struct secure_data *secure_data_ptr;
static char g_fuse_value[FUSE_VALUE_LEN] = UNKNOW_FUSE_VALUE;

static u32 secure_read_reg(phys_addr_t reg)
{
	void __iomem *base;
	u32 val = 0;

	base = ioremap(reg, sizeof(val));
	if (!base)
		return 0;
	val = __raw_readl(base);
	iounmap(base);
	return val;
}

static secure_type_t get_secureType(void)
{
	secure_type_t secureType = SECURE_BOOT_UNKNOWN;
	u32 secure_oem_config1;
	u32 secure_oem_config2;

	if (!secure_data_ptr)
		return SECURE_BOOT_UNKNOWN;

	secure_oem_config1 = secure_read_reg(OEM_SEC_BOOT_REG);
	secure_oem_config2 = secure_read_reg(OEM_SEC_ENABLE_ANTIROLLBACK_REG);

	dev_err(secure_data_ptr->dev, "secure_oem_config1 0x%x\n",
		secure_oem_config1);
	dev_err(secure_data_ptr->dev, "secure_oem_config2 0x%x\n",
		secure_oem_config2);

	if (secure_oem_config1 == 0)
		secureType = SECURE_BOOT_OFF;
	else if (secure_oem_config2 == 0)
		secureType = SECURE_BOOT_ON_STAGE_1;
	else
		secureType = SECURE_BOOT_ON_STAGE_2;

	return secureType;
}

static ssize_t secureType_read_proc(struct file *file, char __user *buf,
				    size_t count, loff_t *off)
{
	char page[256] = { 0 };
	int len;
	secure_type_t secureType = get_secureType();

	len = scnprintf(page, sizeof(page), "%d", secureType);
	return simple_read_from_buffer(buf, count, off, page, len);
}

static ssize_t secureType_write_proc(struct file *filp, const char __user *buf,
				     size_t count, loff_t *offp)
{
	size_t local_count;

	if (count <= 0)
		return 0;

	strscpy(g_fuse_value, UNKNOW_FUSE_VALUE, sizeof(g_fuse_value));
	local_count = min(sizeof(g_fuse_value) - 1, count);
	if (copy_from_user(g_fuse_value, buf, local_count))
		return -EFAULT;
	g_fuse_value[local_count] = '\0';

	if (secure_data_ptr)
		dev_info(secure_data_ptr->dev, "write oem fuse value = %s\n",
			 g_fuse_value);
	return count;
}

static const struct file_operations secureType_proc_fops = {
	.read = secureType_read_proc,
	.write = secureType_write_proc,
};

static ssize_t secureSNBound_read_proc(struct file *file, char __user *buf,
				       size_t count, loff_t *off)
{
	char page[256] = { 0 };
	int len;
	secure_device_sn_bound_state_t secureSNBound_state =
		SECURE_DEVICE_SN_BOUND_UNKNOWN;
	u32 secure_override1_config;

	if (secure_data_ptr) {
		secure_override1_config =
			secure_read_reg(OEM_SEC_OVERRIDE_1_REG);
		dev_info(secure_data_ptr->dev, "secure_override1_config 0x%x\n",
			 secure_override1_config);

		if (get_secureType() == SECURE_BOOT_ON_STAGE_2 &&
		    secure_override1_config != OEM_OVERRIDE_1_ENABLED_VALUE)
			secureSNBound_state = SECURE_DEVICE_SN_BOUND_OFF;
		else
			secureSNBound_state = SECURE_DEVICE_SN_BOUND_ON;
	}

	len = scnprintf(page, sizeof(page), "%d", secureSNBound_state);
	return simple_read_from_buffer(buf, count, off, page, len);
}

static const struct file_operations secureSNBound_proc_fops = {
	.read = secureSNBound_read_proc,
};

static int secure_register_proc_fs(struct secure_data *secure_data)
{
	struct proc_dir_entry *pentry;

	oppo_secure_common_dir = proc_mkdir(oppo_secure_common_dir_name, NULL);
	if (!oppo_secure_common_dir) {
		dev_err(secure_data->dev,
			"can't create oppo_secure_common_dir proc\n");
		return -ENOMEM;
	}

	pentry = proc_create("secureType", 0664, oppo_secure_common_dir,
			     &secureType_proc_fops);
	if (!pentry) {
		dev_err(secure_data->dev, "create secureType proc failed.\n");
		return -ENOMEM;
	}

	pentry = proc_create("secureSNBound", 0444, oppo_secure_common_dir,
			     &secureSNBound_proc_fops);
	if (!pentry) {
		dev_err(secure_data->dev,
			"create secureSNBound proc failed.\n");
		return -ENOMEM;
	}

	return 0;
}

static int oppo_secure_common_probe(struct platform_device *secure_dev)
{
	struct device *dev = &secure_dev->dev;
	struct secure_data *secure_data;
	int ret;

	secure_data = devm_kzalloc(dev, sizeof(*secure_data), GFP_KERNEL);
	if (!secure_data)
		return -ENOMEM;

	secure_data->dev = dev;
	secure_data_ptr = secure_data;

	ret = secure_register_proc_fs(secure_data);
	if (ret) {
		dev_err(dev, "secure_data probe failed ret = %d\n", ret);
		proc_remove(oppo_secure_common_dir);
		oppo_secure_common_dir = NULL;
		secure_data_ptr = NULL;
		return ret;
	}

	return 0;
}

static int oppo_secure_common_remove(struct platform_device *secure_dev)
{
	proc_remove(oppo_secure_common_dir);
	oppo_secure_common_dir = NULL;
	secure_data_ptr = NULL;
	return 0;
}

static const struct of_device_id oppo_secure_common_match_table[] = {
	{
		.compatible = "oppo,secure_common",
	},
	{}
};

static struct platform_driver oppo_secure_common_driver = {
	.probe = oppo_secure_common_probe,
	.remove = oppo_secure_common_remove,
	.driver = {
		.name = "oppo_secure_common",
		.of_match_table = oppo_secure_common_match_table,
	},
};

static int __init oppo_secure_common_init(void)
{
	return platform_driver_register(&oppo_secure_common_driver);
}
fs_initcall(oppo_secure_common_init);

static void __exit oppo_secure_common_exit(void)
{
	platform_driver_unregister(&oppo_secure_common_driver);
}
module_exit(oppo_secure_common_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("OPPO secure common driver");
