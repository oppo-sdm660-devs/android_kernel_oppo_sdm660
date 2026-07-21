/************************************************************************************
** File: - android\kernel\arch\arm\mach-msm\include\mach\oppo_boot.h
** Copyright (C), 2008-2012, OPPO Mobile Comm Corp., Ltd
**
** Description:
**     change define of boot_mode here for other place to use it
** Version: 1.0
** --------------------------- Revision History: --------------------------------
** 	<author>	<data>			<desc>
** tong.han@BasicDrv.TP&LCD 11/01/2014 add this file
************************************************************************************/
#ifndef _OPPO_BOOT_H
#define _OPPO_BOOT_H

#include <linux/types.h>

enum{
	MSM_BOOT_MODE__NORMAL,
	MSM_BOOT_MODE__FASTBOOT,
	MSM_BOOT_MODE__RECOVERY,
	MSM_BOOT_MODE__FACTORY,
	MSM_BOOT_MODE__RF,
	MSM_BOOT_MODE__WLAN,
	MSM_BOOT_MODE__MOS,
	MSM_BOOT_MODE__CHARGE,
	MSM_BOOT_MODE__SILENCE,
	MSM_BOOT_MODE__SAU,
        MSM_BOOT_MODE__SAFE = 999,
};

#ifdef CONFIG_OPPO_COMMON_SOFT
extern int get_boot_mode(void);
//Fuchun.Liao@Mobile.BSP.CHG 2016-01-14 add for charge
extern bool qpnp_is_power_off_charging(void);
//PengNan@SW.BSP add for detect charger when reboot 2016-04-22
extern bool qpnp_is_charger_reboot(void);
#else
static inline int get_boot_mode(void)
{
	return MSM_BOOT_MODE__NORMAL;
}

static inline bool qpnp_is_power_off_charging(void)
{
	return false;
}

static inline bool qpnp_is_charger_reboot(void)
{
	return false;
}
#endif
#endif
