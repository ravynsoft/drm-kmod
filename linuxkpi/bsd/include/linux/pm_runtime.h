/* Public domain. */

#ifndef _LINUX_PM_RUNTIME_H
#define _LINUX_PM_RUNTIME_H

#include <linux/device.h>
#include <linux/pm.h>

#define pm_runtime_mark_last_busy(x) (void)(x)
#define pm_runtime_use_autosuspend(x) (void)(x)
#define pm_runtime_dont_use_autosuspend(x) (void)(x)
#define pm_runtime_put_autosuspend(x) (void)(x)
#define pm_runtime_set_autosuspend_delay(x, y) (void)(x); (void)(y)
#define pm_runtime_set_active(x) (void)(x)
#define pm_runtime_allow(x) (void)(x)
#define pm_runtime_put_noidle(x) (void)(x)
#define pm_runtime_forbid(x) (void)(x)
#define pm_runtime_get_noresume(x) (void)(x)
#define pm_runtime_put(x) (void)(x)
#define pm_runtime_enable(x) (void)(x)
#define pm_runtime_disable(x) (void)(x)
#define pm_runtime_autosuspend(x) (void)(x)
#define pm_runtime_resume(x) (void)(x)

static inline int
pm_runtime_get_sync(struct device *dev)
{
	return 0;
}

static inline int
pm_runtime_get_if_in_use(struct device *dev)
{
	return 1;
}

static inline int
pm_runtime_get_if_active(struct device *dev, bool x)
{
	return -EINVAL;
}

#endif
