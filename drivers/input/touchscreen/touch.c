/*
 * Copyright (c)  2008- 2030  Oppo Mobile communication Corp.ltd.
 */
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/serio.h>
#include <linux/gpio.h>
#include <soc/oppo/oppo_project.h>
#include "oppo_touchscreen/Synaptics/S3508/synaptics_s3508.h"
#include "oppo_touchscreen/tp_devices.h"
#include "oppo_touchscreen/touchpanel_common.h"

#define MAX_LIMIT_DATA_LENGTH         100

#define S3508_FW_NAME "tp/16051/16051_FW_S3508_SYNAPTICS.img"
#define S3508_BASELINE_TEST_LIMIT_NAME "tp/16051/16051_Limit_data.img"
#define S3320_FW_NAME "tp/16103/16103_FW_S3320_JDI.img"
#define S3320_BASELINE_TEST_LIMIT_NAME "tp/16103/16103_Limit_data.img"
#define S3508_FW_NAME_16118 "tp/16118/16118_FW_S3508_SYNAPTICS.img"
#define S3508_BASELINE_TEST_LIMIT_NAME_16118 "tp/16118/16118_Limit_data.img"
#define S3508_FW_NAME_17011 "tp/17011/17011_FW_S3508_SYNAPTICS.img"
#define S3508_BASELINE_TEST_LIMIT_NAME_17011 "tp/17011/17011_Limit_data.img"
#define S3508_FW_NAME_17021 "tp/17021/17021_FW_S3508_SYNAPTICS.img"
#define S3508_BASELINE_TEST_LIMIT_NAME_17021 "tp/17021/17021_Limit_data.img"

struct tp_dev_name tp_dev_names[] = {
     {TP_OFILM, "OFILM"},
     {TP_BIEL, "BIEL"},
     {TP_TRULY, "TRULY"},
     {TP_BOE, "BOE"},
     {TP_G2Y, "G2Y"},
     {TP_TPK, "TPK"},
     {TP_JDI, "JDI"},
     {TP_TIANMA, "TIANMA"},
     {TP_SAMSUNG, "SAMSUNG"},
     {TP_DSJM, "DSJM"},
     {TP_BOE_B8, "BOEB8"},
     {TP_INNOLUX, "INNOLUX"},
     {TP_HIMAX_DPT, "DPT"},
     {TP_AUO, "AUO"},
     {TP_DEPUTE, "DEPUTE"},
     {TP_UNKNOWN, "UNKNOWN"},
};

#define GET_TP_DEV_NAME(tp_type) ((tp_dev_names[tp_type].type == (tp_type))?tp_dev_names[tp_type].name:"UNMATCH")

int g_tp_dev_vendor = TP_UNKNOWN;
char *g_tp_chip_name;

int tp_util_get_vendor(struct hw_resource *hw_res, struct panel_info *panel_data)
{
    if (is_project(OPPO_16051)) {
        panel_data->test_limit_name = kzalloc(sizeof(S3508_BASELINE_TEST_LIMIT_NAME), GFP_KERNEL);
        if (panel_data->test_limit_name == NULL) {
            pr_err("[TP]panel_data.test_limit_name kzalloc error\n");
            return -1;
        }
        strcpy(panel_data->test_limit_name, S3508_BASELINE_TEST_LIMIT_NAME);
        strcpy(panel_data->fw_name, S3508_FW_NAME);
        pr_err("[TP]%s: fw_name = %s \n",__func__, panel_data->fw_name);
    } else if (is_project(OPPO_16103)) {
        panel_data->test_limit_name = kzalloc(sizeof(S3320_BASELINE_TEST_LIMIT_NAME), GFP_KERNEL);
        if (panel_data->test_limit_name == NULL) {
            pr_err("[TP]panel_data.test_limit_name kzalloc error\n");
            return -1;
        }
        strcpy(panel_data->test_limit_name, S3320_BASELINE_TEST_LIMIT_NAME);
        strcpy(panel_data->fw_name, S3320_FW_NAME);
        pr_err("[TP]%s: fw_name = %s \n",__func__, panel_data->fw_name);
    } else if (is_project(OPPO_16118)) {
        panel_data->test_limit_name = kzalloc(sizeof(S3508_BASELINE_TEST_LIMIT_NAME_16118), GFP_KERNEL);
        if (panel_data->test_limit_name == NULL) {
            pr_err("[TP]panel_data.test_limit_name kzalloc error\n");
            return -1;
        }
        strcpy(panel_data->test_limit_name, S3508_BASELINE_TEST_LIMIT_NAME_16118);
        strcpy(panel_data->fw_name, S3508_FW_NAME_16118);
        pr_err("[TP]%s: fw_name = %s \n",__func__, panel_data->fw_name);
    } else if (is_project(OPPO_17011)) {
        panel_data->test_limit_name = kzalloc(sizeof(S3508_BASELINE_TEST_LIMIT_NAME_17011), GFP_KERNEL);
        if (panel_data->test_limit_name == NULL) {
            pr_err("[TP]panel_data.test_limit_name kzalloc error\n");
            return -1;
        }
        strcpy(panel_data->test_limit_name, S3508_BASELINE_TEST_LIMIT_NAME_17011);
        strcpy(panel_data->fw_name, S3508_FW_NAME_17011);
        pr_err("[TP]%s: fw_name = %s \n",__func__, panel_data->fw_name);
    } else if (is_project(OPPO_17021)) {
        panel_data->test_limit_name = kzalloc(sizeof(S3508_BASELINE_TEST_LIMIT_NAME_17021), GFP_KERNEL);
        if (panel_data->test_limit_name == NULL) {
            pr_err("[TP]panel_data.test_limit_name kzalloc error\n");
            return -1;
        }
        strcpy(panel_data->test_limit_name, S3508_BASELINE_TEST_LIMIT_NAME_17021);
        strcpy(panel_data->fw_name, S3508_FW_NAME_17021);
        pr_err("[TP]%s: fw_name = %s \n",__func__, panel_data->fw_name);
    }
    strcpy(panel_data->manufacture_info.manufacture, "SAMSUNG");

    return 0;
}
