// SPDX-License-Identifier: GPL-2.0
/**
 * Copyright 2008-2013 OPPO Mobile Comm Corp., Ltd, All rights reserved.
 * FileName:devinfo.c
 * ModuleName:devinfo
 * Author: wangjc
 * Create Date: 2013-10-23
 * Description:add interface to get device information.
 * History:
   <version >  <time>  <author>  <desc>
   1.0		2013-10-23	wangjc	init
   2.0      2015-04-13  hantong modify as platform device  to support diffrent configure in dts
 */
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/gpio.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/consumer.h>
#include <soc/oppo/device_info.h>
#include <soc/oppo/oppo_project.h>

#define DEVINFO_NAME "devinfo"
#define INFO_BUF_LEN 64

static const struct of_device_id devinfo_id[] = {
	{ .compatible = "oppo-devinfo" },
	{},
};
MODULE_DEVICE_TABLE(of, devinfo_id);

struct devinfo_data {
	struct platform_device *devinfo;
	struct pinctrl *pinctrl;
	struct pinctrl_state *hw_sub_gpio_sleep;
	struct pinctrl_state *hw_wlan_gpio_sleep;
	int sub_hw_id1;
	int sub_hw_id2;
	int wlan_hw_id1;
	int wlan_hw_id2;
	int mainboard_res;
	struct proc_dir_entry *wlan_res_entry;
};

struct device_proc_info {
	struct list_head list;
	struct proc_dir_entry *entry;
	struct device *owner;
	char *version;
	char *manufacture;
};

static struct proc_dir_entry *parent;
static LIST_HEAD(device_proc_infos);
static DEFINE_MUTEX(device_proc_lock);

static int dev_proc_show(struct seq_file *s, void *v)
{
	struct device_proc_info *info = s->private;

	if (info)
		seq_printf(s, "Device version:\t\t%s\nDevice manufacture:\t\t%s\n",
			   info->version, info->manufacture);
	return 0;
}

static int dev_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, dev_proc_show, PDE_DATA(inode));
}

static const struct file_operations dev_node_fops = {
	.owner		= THIS_MODULE,
	.open		= dev_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int register_device_proc_data(const char *name, const char *version,
				     const char *manufacture,
				     struct device *owner)
{
	struct proc_dir_entry *d_entry;
	struct device_proc_info *info;
	int ret = 0;

	if (!name || !version || !manufacture)
		return -EINVAL;

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->version = kstrdup(version, GFP_KERNEL);
	info->manufacture = kstrdup(manufacture, GFP_KERNEL);
	info->owner = owner;
	if (!info->version || !info->manufacture) {
		ret = -ENOMEM;
		goto free_info;
	}

	mutex_lock(&device_proc_lock);
	if (!parent) {
		parent = proc_mkdir("devinfo", NULL);
		if (!parent) {
			pr_err("can't create devinfo proc\n");
			ret = -ENOMEM;
			goto unlock;
		}
	}

	d_entry = proc_create_data(name, 0444, parent, &dev_node_fops, info);
	if (!d_entry) {
		pr_err("create %s proc failed.\n", name);
		if (list_empty(&device_proc_infos)) {
			proc_remove(parent);
			parent = NULL;
		}
		ret = -ENOMEM;
		goto unlock;
	}
	info->entry = d_entry;
	list_add_tail(&info->list, &device_proc_infos);
	mutex_unlock(&device_proc_lock);
	return 0;

unlock:
	mutex_unlock(&device_proc_lock);
free_info:
	kfree(info->manufacture);
	kfree(info->version);
	kfree(info);
	return ret;
}

int register_device_proc(char *name, char *version, char *manufacture)
{
	return register_device_proc_data(name, version, manufacture, NULL);
}
EXPORT_SYMBOL(register_device_proc);

static int register_managed_device_proc(struct device *owner, const char *name,
					const char *version,
					const char *manufacture)
{
	return register_device_proc_data(name, version, manufacture, owner);
}

static void unregister_managed_device_proc_entries(struct device *owner)
{
	struct device_proc_info *info;
	struct device_proc_info *next;

	mutex_lock(&device_proc_lock);
	list_for_each_entry_safe(info, next, &device_proc_infos, list) {
		if (info->owner != owner)
			continue;
		proc_remove(info->entry);
		list_del(&info->list);
		kfree(info->manufacture);
		kfree(info->version);
		kfree(info);
	}
	if (list_empty(&device_proc_infos)) {
		proc_remove(parent);
		parent = NULL;
	}
	mutex_unlock(&device_proc_lock);
}

static int request_input_gpio(struct devinfo_data *devinfo_data,
			      const char *property, const char *label,
			      int *gpio)
{
	struct device *dev = &devinfo_data->devinfo->dev;

	*gpio = of_get_named_gpio(dev->of_node, property, 0);
	if (*gpio == -ENOENT)
		return 0;
	if (*gpio < 0)
		return *gpio;

	return devm_gpio_request_one(dev, *gpio, GPIOF_IN, label);
}

static int sub_mainboard_verify(struct devinfo_data *devinfo_data)
{
	int ret;
	int id1 = -1;
	int id2 = -1;
	bool register_speaker = false;
	char temp_manufacture_sub[INFO_BUF_LEN] = {0};
	char temp_speaker_manufacture_sub[INFO_BUF_LEN] = {0};
	struct manufacture_info mainboard_info;
	struct manufacture_info speaker_mainboard_info;

	if (!devinfo_data) {
		pr_err("devinfo_data is NULL\n");
		return -EINVAL;
	}

	ret = request_input_gpio(devinfo_data, "Hw,sub_hwid_1", "SUB_HW_ID1",
				 &devinfo_data->sub_hw_id1);
	if (ret)
		return ret;
	ret = request_input_gpio(devinfo_data, "Hw,sub_hwid_2", "SUB_HW_ID2",
				 &devinfo_data->sub_hw_id2);
	if (ret)
		return ret;

	devinfo_data->hw_sub_gpio_sleep = pinctrl_lookup_state(
			devinfo_data->pinctrl, "hw_sub_gpio_sleep");
	if (IS_ERR(devinfo_data->hw_sub_gpio_sleep))
		return PTR_ERR(devinfo_data->hw_sub_gpio_sleep);

	if (gpio_is_valid(devinfo_data->sub_hw_id1))
		id1 = gpio_get_value(devinfo_data->sub_hw_id1);
	if (gpio_is_valid(devinfo_data->sub_hw_id2))
		id2 = gpio_get_value(devinfo_data->sub_hw_id2);

	mainboard_info.manufacture = temp_manufacture_sub;
	mainboard_info.version = "Qcom";
	speaker_mainboard_info.manufacture = temp_speaker_manufacture_sub;
	speaker_mainboard_info.version = "Qcom";
	switch (get_project()) {
	case OPPO_16103:
	case OPPO_16118:
	{
		pr_err("id1 = %d,id2 = %d\n", id1, id2);
		if (get_PCB_Version() == HW_VERSION__10)
			snprintf(mainboard_info.manufacture, INFO_BUF_LEN, "%d-audio-china", get_project());
		else {
			if (id1 == 1 && (id2 == 1 || id2 < 0))
				snprintf(mainboard_info.manufacture, INFO_BUF_LEN, "%d-audio-china", get_project());
			else if (id1 == 0 && (id2 == 0 || id2 < 0))
				snprintf(mainboard_info.manufacture, INFO_BUF_LEN, "%d-audio-oversea", get_project());
			else
				mainboard_info.manufacture = "sub-UNSPECIFIED";
		}
		break;
	}
	case OPPO_16051:
	{
		pr_err("id1 = %d\n", id1);
		if (id1 == 0)
			snprintf(mainboard_info.manufacture, INFO_BUF_LEN, "%d-audio-china", get_project());
		else if (id1 == 1)
			snprintf(mainboard_info.manufacture, INFO_BUF_LEN, "%d-audio-oversea", get_project());
		else
			mainboard_info.manufacture = "sub-UNSPECIFIED";
		break;
	}
	/*Haitao.Zhou@BSP.Bootloader.Device, 2017/06/26, Add for 17011/17021*/
	case OPPO_17011:
	case OPPO_17021:
	{
		pr_err("id1 = %d,id2 = %d\n", id1, id2);
		if (id1 == 1)
			snprintf(mainboard_info.manufacture, INFO_BUF_LEN, "%d-audio-china", get_project());
		else if (id1 == 0)
			snprintf(mainboard_info.manufacture, INFO_BUF_LEN, "%d-audio-oversea", get_project());
		else
			mainboard_info.manufacture = "sub-UNSPECIFIED";

		if (id2 == 1)
			snprintf(speaker_mainboard_info.manufacture, INFO_BUF_LEN, "%d-speaker-china", get_project());
		else if (id2 == 0)
			snprintf(speaker_mainboard_info.manufacture, INFO_BUF_LEN, "%d-speaker-oversea", get_project());
		else
			speaker_mainboard_info.manufacture = "sub-UNSPECIFIED";
		register_speaker = true;
		break;
	}
	default:
	{
		snprintf(mainboard_info.manufacture, INFO_BUF_LEN, "%d-%d", get_project(), get_Operator_Version());
		break;
	}
	}

	ret = pinctrl_select_state(devinfo_data->pinctrl,
				   devinfo_data->hw_sub_gpio_sleep);
	if (ret)
		return ret;

	if (register_speaker) {
		ret = register_managed_device_proc(
				&devinfo_data->devinfo->dev, "speaker_mainboard",
				speaker_mainboard_info.version,
				speaker_mainboard_info.manufacture);
		if (ret)
			return ret;
	}

	return register_managed_device_proc(&devinfo_data->devinfo->dev,
					    "audio_mainboard",
					    mainboard_info.version,
					    mainboard_info.manufacture);
}

/*rendong.shi@BSP.boot,2016/03/24,add for mainboard resource*/
static int wlan_resource_verify(struct devinfo_data *devinfo_data)
{
	int ret;
	int id1 = -1;
	int id2 = -1;
	char temp_manufacture_wlan[INFO_BUF_LEN] = {0};
	struct manufacture_info mainboard_info;

	if (!devinfo_data) {
		pr_err("devinfo_data is NULL\n");
		return -EINVAL;
	}

	ret = request_input_gpio(devinfo_data, "Hw,wlan_hwid_1", "WLAN_HW_ID1",
				 &devinfo_data->wlan_hw_id1);
	if (ret)
		return ret;
	ret = request_input_gpio(devinfo_data, "Hw,wlan_hwid_2", "WLAN_HW_ID2",
				 &devinfo_data->wlan_hw_id2);
	if (ret)
		return ret;

	if (gpio_is_valid(devinfo_data->wlan_hw_id1))
		id1 = gpio_get_value(devinfo_data->wlan_hw_id1);
	if (gpio_is_valid(devinfo_data->wlan_hw_id2))
		id2 = gpio_get_value(devinfo_data->wlan_hw_id2);

	mainboard_info.manufacture = temp_manufacture_wlan;
	mainboard_info.version = "Qcom";
	devinfo_data->mainboard_res = MAINBOARD_RESOURCE0;
	switch (get_project()) {
	case OPPO_16103:
	case OPPO_16118:
	{
		pr_err("wlan id1 = %d,id2 = %d\n", id1, id2);
		if (id1 == 1 && id2 == 1) {
			devinfo_data->mainboard_res = MAINBOARD_RESOURCE1;
			snprintf(mainboard_info.manufacture, INFO_BUF_LEN, "%d-wlan-first", get_project());
		} else if (id1 == 1 && id2 == 0) {
			devinfo_data->mainboard_res = MAINBOARD_RESOURCE2;
			snprintf(mainboard_info.manufacture, INFO_BUF_LEN, "%d-wlan-second", get_project());
		} else {
			devinfo_data->mainboard_res = MAINBOARD_RESOURCE0;
			mainboard_info.manufacture = "wlan-UNSPECIFIED";
		}
		break;
	}
	default:
	{
		snprintf(mainboard_info.manufacture, INFO_BUF_LEN, "%d-%d", get_project(), get_Operator_Version());
		break;
	}
	}

	if (gpio_is_valid(devinfo_data->wlan_hw_id1) ||
	    gpio_is_valid(devinfo_data->wlan_hw_id2)) {
		devinfo_data->hw_wlan_gpio_sleep = pinctrl_lookup_state(
				devinfo_data->pinctrl, "hw_wlan_gpio_sleep");
		if (IS_ERR(devinfo_data->hw_wlan_gpio_sleep))
			return PTR_ERR(devinfo_data->hw_wlan_gpio_sleep);
		ret = pinctrl_select_state(devinfo_data->pinctrl,
					   devinfo_data->hw_wlan_gpio_sleep);
		if (ret)
			return ret;
	}

	return register_managed_device_proc(&devinfo_data->devinfo->dev,
					    "wlan_resource",
					    mainboard_info.version,
					    mainboard_info.manufacture);
}

static int mainboard_resource_show(struct seq_file *s, void *v)
{
	struct devinfo_data *devinfo_data = s->private;

	seq_printf(s, "%d", devinfo_data->mainboard_res);
	return 0;
}

static int mainboard_resource_open(struct inode *inode, struct file *file)
{
	return single_open(file, mainboard_resource_show, PDE_DATA(inode));
}

static const struct file_operations mainboard_res_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= mainboard_resource_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};


static int devinfo_probe(struct platform_device *pdev)
{
	struct devinfo_data *devinfo_data;
	int ret;

	ret = oppo_project_info_init();
	if (ret)
		return ret;

	devinfo_data = devm_kzalloc(&pdev->dev, sizeof(*devinfo_data),
				    GFP_KERNEL);
	if (!devinfo_data)
		return -ENOMEM;

	devinfo_data->devinfo = pdev;
	devinfo_data->pinctrl = devm_pinctrl_get(&pdev->dev);
	if (IS_ERR(devinfo_data->pinctrl))
		return PTR_ERR(devinfo_data->pinctrl);
	platform_set_drvdata(pdev, devinfo_data);

	pr_info("this is project %d\n", get_project());

	ret = sub_mainboard_verify(devinfo_data);
	if (ret)
		goto remove_proc;
	ret = wlan_resource_verify(devinfo_data);
	if (ret)
		goto remove_proc;

	devinfo_data->wlan_res_entry = proc_create_data(
			"wlan_res", 0444, NULL, &mainboard_res_proc_fops,
			devinfo_data);
	if (!devinfo_data->wlan_res_entry) {
		pr_err("create wlan_res proc failed.\n");
		ret = -ENOMEM;
		goto remove_proc;
	}

	return 0;

remove_proc:
	proc_remove(devinfo_data->wlan_res_entry);
	devinfo_data->wlan_res_entry = NULL;
	unregister_managed_device_proc_entries(&pdev->dev);
	platform_set_drvdata(pdev, NULL);
	return ret;
}

static int devinfo_remove(struct platform_device *pdev)
{
	struct devinfo_data *devinfo_data = platform_get_drvdata(pdev);

	if (devinfo_data) {
		proc_remove(devinfo_data->wlan_res_entry);
		devinfo_data->wlan_res_entry = NULL;
	}
	unregister_managed_device_proc_entries(&pdev->dev);
	platform_set_drvdata(pdev, NULL);
	return 0;
}

static struct platform_driver devinfo_platform_driver = {
	.probe = devinfo_probe,
	.remove = devinfo_remove,
	.driver = {
		.name = DEVINFO_NAME,
		.of_match_table = devinfo_id,
	},
};

module_platform_driver(devinfo_platform_driver);

MODULE_DESCRIPTION("OPPO device info");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Wangjc <wjc@oppo.com>");
