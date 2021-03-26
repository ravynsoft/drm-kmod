/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2020,2021 Intel Corporation
 */

#ifndef __INTEL_STEP_H__
#define __INTEL_STEP_H__

#include <linux/types.h>

struct i915_rev_steppings {
	u8 gt_stepping;
	u8 disp_stepping;
};

#define TGL_UY_REVID_STEP_TBL_SIZE	4
#define TGL_REVID_STEP_TBL_SIZE		2
#define ADLS_REVID_STEP_TBL_SIZE	13

extern const struct i915_rev_steppings kbl_revids[];
extern const struct i915_rev_steppings tgl_uy_revid_step_tbl[TGL_UY_REVID_STEP_TBL_SIZE];
extern const struct i915_rev_steppings tgl_revid_step_tbl[TGL_REVID_STEP_TBL_SIZE];
extern const struct i915_rev_steppings adls_revid_step_tbl[ADLS_REVID_STEP_TBL_SIZE];

/*
 * Symbolic steppings that do not match the hardware. These are valid both as gt
 * and display steppings as symbolic names.
 */
enum intel_step {
	STEP_NONE = 0,
	STEP_A0,
	STEP_A2,
	STEP_B0,
	STEP_B1,
	STEP_C0,
	STEP_D0,
};

#endif /* __INTEL_STEP_H__ */
