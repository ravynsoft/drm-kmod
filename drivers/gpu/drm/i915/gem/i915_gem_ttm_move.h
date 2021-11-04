/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021 Intel Corporation
 */
#ifndef _I915_GEM_TTM_MOVE_H_
#define _I915_GEM_TTM_MOVE_H_

#include <linux/types.h>

struct ttm_buffer_object;
struct ttm_operation_ctx;
struct ttm_place;
struct ttm_resource;
struct ttm_tt;

struct drm_i915_gem_object;
struct i915_refct_sgt;

int i915_ttm_move_notify(struct ttm_buffer_object *bo);

/* Internal I915 TTM declarations and definitions below. */

void __i915_ttm_move(struct ttm_buffer_object *bo, bool clear,
		     struct ttm_resource *dst_mem,
		     struct ttm_tt *dst_ttm,
		     struct i915_refct_sgt *dst_rsgt,
		     bool allow_accel);

int i915_ttm_move(struct ttm_buffer_object *bo, bool evict,
		  struct ttm_operation_ctx *ctx,
		  struct ttm_resource *dst_mem,
		  struct ttm_place *hop);

void i915_ttm_adjust_domains_after_move(struct drm_i915_gem_object *obj);

void i915_ttm_adjust_gem_after_move(struct drm_i915_gem_object *obj);

#endif
