/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2020-2021 Intel Corporation
 */

#ifndef __INTEL_FB_H__
#define __INTEL_FB_H__

#include <linux/types.h>

struct drm_framebuffer;

struct drm_i915_private;

struct i915_ggtt_view;

struct intel_plane_state;

bool is_ccs_plane(const struct drm_framebuffer *fb, int plane);
bool is_gen12_ccs_plane(const struct drm_framebuffer *fb, int plane);
bool is_gen12_ccs_cc_plane(const struct drm_framebuffer *fb, int plane);
bool is_aux_plane(const struct drm_framebuffer *fb, int plane);
bool is_semiplanar_uv_plane(const struct drm_framebuffer *fb, int color_plane);

bool is_surface_linear(const struct drm_framebuffer *fb, int color_plane);

int main_to_ccs_plane(const struct drm_framebuffer *fb, int main_plane);
int skl_ccs_to_main_plane(const struct drm_framebuffer *fb, int ccs_plane);
int skl_main_to_aux_plane(const struct drm_framebuffer *fb, int main_plane);

unsigned int intel_tile_size(const struct drm_i915_private *dev_priv);
unsigned int intel_tile_height(const struct drm_framebuffer *fb, int color_plane);
unsigned int intel_tile_row_size(const struct drm_framebuffer *fb, int color_plane);

unsigned int intel_cursor_alignment(const struct drm_i915_private *dev_priv);

void intel_fb_plane_get_subsampling(int *hsub, int *vsub,
				    const struct drm_framebuffer *fb,
				    int color_plane);

u32 intel_plane_adjust_aligned_offset(int *x, int *y,
				      const struct intel_plane_state *state,
				      int color_plane,
				      u32 old_offset, u32 new_offset);
u32 intel_plane_compute_aligned_offset(int *x, int *y,
				       const struct intel_plane_state *state,
				       int color_plane);

int intel_fb_pitch(const struct drm_framebuffer *fb, int color_plane, unsigned int rotation);

int intel_fill_fb_info(struct drm_i915_private *dev_priv, struct drm_framebuffer *fb);
void intel_fill_fb_ggtt_view(struct i915_ggtt_view *view, const struct drm_framebuffer *fb,
			     unsigned int rotation);
int intel_plane_compute_gtt(struct intel_plane_state *plane_state);

#endif /* __INTEL_FB_H__ */
