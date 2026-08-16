// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2007-2012 Google, Inc. */
#ifndef _LINUX_WAKELOCK_H
#define _LINUX_WAKELOCK_H

#include <linux/ktime.h>
#include <linux/device.h>
#include <linux/pm_wakeup.h>
#include <linux/slab.h>

enum { WAKE_LOCK_SUSPEND, WAKE_LOCK_TYPE_COUNT };

struct wake_lock {
	struct wakeup_source *ws;
};

static inline void wake_lock_init(struct wake_lock *lock, int type,
				  const char *name)
{
	lock->ws = wakeup_source_create(name);
	if (lock->ws)
		wakeup_source_add(lock->ws);
}

static inline void wake_lock_destroy(struct wake_lock *lock)
{
	if (lock->ws) {
		wakeup_source_remove(lock->ws);
		wakeup_source_destroy(lock->ws);
		lock->ws = NULL;
	}
}

static inline void wake_lock(struct wake_lock *lock)
{
	if (lock->ws)
		__pm_stay_awake(lock->ws);
}

static inline void wake_lock_timeout(struct wake_lock *lock, long timeout)
{
	if (lock->ws)
		__pm_wakeup_event(lock->ws, jiffies_to_msecs(timeout));
}

static inline void wake_unlock(struct wake_lock *lock)
{
	if (lock->ws)
		__pm_relax(lock->ws);
}

static inline int wake_lock_active(struct wake_lock *lock)
{
	return lock->ws ? lock->ws->active : 0;
}

#endif /* _LINUX_WAKELOCK_H */
