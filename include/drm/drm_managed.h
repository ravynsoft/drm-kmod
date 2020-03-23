// SPDX-License-Identifier: GPL-2.0

#ifndef _DRM_MANAGED_H_
#define _DRM_MANAGED_H_

#include <linux/gfp.h>
#include <linux/types.h>

struct drm_device;

typedef void (*drmres_release_t)(struct drm_device *dev, void *res);

#define drmm_add_action(dev, action, data) \
	__drmm_add_action(dev, action, data, #action)

int __must_check __drmm_add_action(struct drm_device *dev,
				   drmres_release_t action,
				   void *data, const char *name);

void drmm_add_final_kfree(struct drm_device *dev, void *parent);

void *drmm_kmalloc(struct drm_device *dev, size_t size, gfp_t gfp) __malloc;
static inline void *drmm_kzalloc(struct drm_device *dev, size_t size, gfp_t gfp)
{
	return drmm_kmalloc(dev, size, gfp | __GFP_ZERO);
}
char *drmm_kstrdup(struct drm_device *dev, const char *s, gfp_t gfp);

void drmm_kfree(struct drm_device *dev, void *data);

#endif
