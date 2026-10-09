/*
 * drivers/staging/android/ion/compat_ion.c
 *
 * Copyright (C) 2013 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/compat.h>
#include <linux/fs.h>
#include <linux/uaccess.h>

#include "ion.h"
#include "compat_ion.h"
#include "ion_legacy.h"

/* See drivers/staging/android/uapi/ion.h for the definition of these structs */
struct compat_ion_old_allocation_data {
	compat_size_t len;
	compat_size_t align;
	compat_uint_t heap_id_mask;
	compat_uint_t flags;
	compat_int_t handle;
};

struct compat_ion_custom_data {
	compat_uint_t cmd;
	compat_ulong_t arg;
};

struct compat_ion_flush_data {
	compat_int_t handle;
	compat_int_t fd;
	compat_uptr_t vaddr;
	compat_uint_t offset;
	compat_uint_t length;
};

struct compat_ion_prefetch_data {
	compat_int_t heap_id;
	compat_ulong_t len;
	compat_uptr_t regions;
	compat_uint_t nr_regions;
};

#define COMPAT_ION_IOC_ALLOC	_IOWR(ION_IOC_MAGIC, 0, \
				      struct compat_ion_old_allocation_data)
#define COMPAT_ION_IOC_CUSTOM	_IOWR(ION_IOC_MAGIC, 6, \
				      struct compat_ion_custom_data)
#define COMPAT_ION_IOC_CLEAN_CACHES _IOWR(ION_IOC_MSM_MAGIC, 0, \
					  struct compat_ion_flush_data)
#define COMPAT_ION_IOC_INV_CACHES _IOWR(ION_IOC_MSM_MAGIC, 1, \
					struct compat_ion_flush_data)
#define COMPAT_ION_IOC_CLEAN_INV_CACHES _IOWR(ION_IOC_MSM_MAGIC, 2, \
					      struct compat_ion_flush_data)
#define COMPAT_ION_IOC_PREFETCH	_IOWR(ION_IOC_MSM_MAGIC, 3, \
				      struct compat_ion_prefetch_data)
#define COMPAT_ION_IOC_DRAIN	_IOWR(ION_IOC_MSM_MAGIC, 4, \
				      struct compat_ion_prefetch_data)

static int compat_ion_legacy_cache_ioctl(struct file *filp, unsigned int cmd,
					 unsigned long arg)
{
	struct compat_ion_flush_data flush;
	unsigned int native_cmd;

	switch (cmd) {
	case COMPAT_ION_IOC_CLEAN_CACHES:
		native_cmd = ION_IOC_CLEAN_CACHES;
		break;
	case COMPAT_ION_IOC_INV_CACHES:
		native_cmd = ION_IOC_INV_CACHES;
		break;
	case COMPAT_ION_IOC_CLEAN_INV_CACHES:
		native_cmd = ION_IOC_CLEAN_INV_CACHES;
		break;
	default:
		return -ENOIOCTLCMD;
	}

	if (copy_from_user(&flush, compat_ptr(arg), sizeof(flush)))
		return -EFAULT;

	return ion_legacy_cache_ioctl(filp, flush.handle, flush.fd,
				      flush.offset, flush.length, native_cmd);
}

static long compat_ion_legacy_prefetch_ioctl(unsigned int cmd,
					     unsigned long arg)
{
	struct compat_ion_prefetch_data data32;
	struct ion_legacy_prefetch_data data;
	unsigned int native_cmd;

	switch (cmd) {
	case COMPAT_ION_IOC_PREFETCH:
		native_cmd = ION_IOC_PREFETCH;
		break;
	case COMPAT_ION_IOC_DRAIN:
		native_cmd = ION_IOC_DRAIN;
		break;
	default:
		return -ENOIOCTLCMD;
	}

	if (copy_from_user(&data32, compat_ptr(arg), sizeof(data32)))
		return -EFAULT;

	data.heap_id = data32.heap_id;
	data.len = data32.len;
	data.regions = compat_ptr(data32.regions);
	data.nr_regions = data32.nr_regions;

	return ion_legacy_prefetch_ioctl(native_cmd, &data, true);
}

static long compat_ion_legacy_custom_ioctl(struct file *filp,
					   unsigned long arg)
{
	struct compat_ion_custom_data custom;

	if (copy_from_user(&custom, compat_ptr(arg), sizeof(custom)))
		return -EFAULT;

	switch (custom.cmd) {
	case COMPAT_ION_IOC_CLEAN_CACHES:
	case COMPAT_ION_IOC_INV_CACHES:
	case COMPAT_ION_IOC_CLEAN_INV_CACHES:
		return compat_ion_legacy_cache_ioctl(filp, custom.cmd, custom.arg);
	case COMPAT_ION_IOC_PREFETCH:
	case COMPAT_ION_IOC_DRAIN:
		return compat_ion_legacy_prefetch_ioctl(custom.cmd, custom.arg);
	default:
		return -ENOIOCTLCMD;
	}
}

long compat_ion_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	if (!filp->f_op->unlocked_ioctl)
		return -ENOTTY;

	switch (cmd) {
	case COMPAT_ION_IOC_ALLOC:
	{
		struct compat_ion_old_allocation_data __user *data32;
		struct compat_ion_old_allocation_data data;

		data32 = compat_ptr(arg);
		if (copy_from_user(&data, data32, sizeof(data)))
			return -EFAULT;

		return ion_legacy_alloc_ioctl(filp, data.len, data.align,
					      data.heap_id_mask, data.flags,
					      &data32->handle);
	}
	case COMPAT_ION_IOC_CUSTOM:
		return compat_ion_legacy_custom_ioctl(filp, arg);
	case COMPAT_ION_IOC_CLEAN_CACHES:
	case COMPAT_ION_IOC_INV_CACHES:
	case COMPAT_ION_IOC_CLEAN_INV_CACHES:
		return compat_ion_legacy_cache_ioctl(filp, cmd, arg);
	case COMPAT_ION_IOC_PREFETCH:
	case COMPAT_ION_IOC_DRAIN:
		return compat_ion_legacy_prefetch_ioctl(cmd, arg);
	case ION_IOC_FREE:
	case ION_IOC_SHARE:
	case ION_IOC_MAP:
	case ION_IOC_IMPORT:
	case ION_IOC_SYNC:
	case ION_IOC_ALLOC:
	case ION_IOC_HEAP_QUERY:
	case ION_IOC_PREFETCH:
	case ION_IOC_DRAIN:
		return filp->f_op->unlocked_ioctl(filp, cmd,
						(unsigned long)compat_ptr(arg));
	default:
		return -ENOIOCTLCMD;
	}
}
