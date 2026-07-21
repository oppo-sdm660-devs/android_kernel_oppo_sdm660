/**
 * Copyright 2008-2013 OPPO Mobile Comm Corp., Ltd, All rights reserved.
 * FileName:devinfo.h
 * ModuleName:devinfo
 * Author: wangjc
 * Create Date: 2013-10-23
 * Description:add interface to get device information.
 * History:
   <version >  <time>  <author>  <desc>
   1.0		2013-10-23	wangjc	init
*/

#ifndef _DEVICE_INFO_H
#define _DEVICE_INFO_H

#include <linux/errno.h>

enum {
	MAINBOARD_RESOURCE0 = 0,
	MAINBOARD_RESOURCE1 = 1,
	MAINBOARD_RESOURCE2 = 2,
};

struct manufacture_info {
	char *version;
	char *manufacture;
};

#ifdef CONFIG_OPPO_DEVICE_INFO
int register_device_proc(char *name, char *version, char *manufacture);
#else
static inline int register_device_proc(char *name, char *version,
				       char *manufacture)
{
	return -ENODEV;
}
#endif

#endif /* _DEVICE_INFO_H */
