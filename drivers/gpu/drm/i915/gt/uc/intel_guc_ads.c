// SPDX-License-Identifier: MIT
/*
 * Copyright © 2014-2019 Intel Corporation
 */

#include <linux/bsearch.h>

#include "gt/intel_engine_regs.h"
#include "gt/intel_gt.h"
#include "gt/intel_gt_regs.h"
#include "gt/intel_lrc.h"
#include "gt/shmem_utils.h"
#include "intel_guc_ads.h"
#include "intel_guc_fwif.h"
#include "intel_uc.h"
#include "i915_drv.h"

/*
 * The Additional Data Struct (ADS) has pointers for different buffers used by
 * the GuC. One single gem object contains the ADS struct itself (guc_ads) and
 * all the extra buffers indirectly linked via the ADS struct's entries.
 *
 * Layout of the ADS blob allocated for the GuC:
 *
 *      +---------------------------------------+ <== base
 *      | guc_ads                               |
 *      +---------------------------------------+
 *      | guc_policies                          |
 *      +---------------------------------------+
 *      | guc_gt_system_info                    |
 *      +---------------------------------------+
 *      | guc_engine_usage                      |
 *      +---------------------------------------+ <== static
 *      | guc_mmio_reg[countA] (engine 0.0)     |
 *      | guc_mmio_reg[countB] (engine 0.1)     |
 *      | guc_mmio_reg[countC] (engine 1.0)     |
 *      |   ...                                 |
 *      +---------------------------------------+ <== dynamic
 *      | padding                               |
 *      +---------------------------------------+ <== 4K aligned
 *      | golden contexts                       |
 *      +---------------------------------------+
 *      | padding                               |
 *      +---------------------------------------+ <== 4K aligned
 *      | capture lists                         |
 *      +---------------------------------------+
 *      | padding                               |
 *      +---------------------------------------+ <== 4K aligned
 *      | private data                          |
 *      +---------------------------------------+
 *      | padding                               |
 *      +---------------------------------------+ <== 4K aligned
 */
struct __guc_ads_blob {
	struct guc_ads ads;
	struct guc_policies policies;
	struct guc_gt_system_info system_info;
	struct guc_engine_usage engine_usage;
	/* From here on, location is dynamic! Refer to above diagram. */
	struct guc_mmio_reg regset[];
} __packed;

#define ads_blob_read(guc_, field_)					\
	iosys_map_rd_field(&(guc_)->ads_map, 0, struct __guc_ads_blob, field_)

#define ads_blob_write(guc_, field_, val_)				\
	iosys_map_wr_field(&(guc_)->ads_map, 0, struct __guc_ads_blob,	\
			   field_, val_)

#define info_map_write(map_, field_, val_) \
	iosys_map_wr_field(map_, 0, struct guc_gt_system_info, field_, val_)

#define info_map_read(map_, field_) \
	iosys_map_rd_field(map_, 0, struct guc_gt_system_info, field_)

static u32 guc_ads_regset_size(struct intel_guc *guc)
{
	GEM_BUG_ON(!guc->ads_regset_size);
	return guc->ads_regset_size;
}

static u32 guc_ads_golden_ctxt_size(struct intel_guc *guc)
{
	return PAGE_ALIGN(guc->ads_golden_ctxt_size);
}

static u32 guc_ads_capture_size(struct intel_guc *guc)
{
	/* FIXME: Allocate a proper capture list */
	return PAGE_ALIGN(PAGE_SIZE);
}

static u32 guc_ads_private_data_size(struct intel_guc *guc)
{
	return PAGE_ALIGN(guc->fw.private_data_size);
}

static u32 guc_ads_regset_offset(struct intel_guc *guc)
{
	return offsetof(struct __guc_ads_blob, regset);
}

static u32 guc_ads_golden_ctxt_offset(struct intel_guc *guc)
{
	u32 offset;

	offset = guc_ads_regset_offset(guc) +
		 guc_ads_regset_size(guc);

	return PAGE_ALIGN(offset);
}

static u32 guc_ads_capture_offset(struct intel_guc *guc)
{
	u32 offset;

	offset = guc_ads_golden_ctxt_offset(guc) +
		 guc_ads_golden_ctxt_size(guc);

	return PAGE_ALIGN(offset);
}

static u32 guc_ads_private_data_offset(struct intel_guc *guc)
{
	u32 offset;

	offset = guc_ads_capture_offset(guc) +
		 guc_ads_capture_size(guc);

	return PAGE_ALIGN(offset);
}

static u32 guc_ads_blob_size(struct intel_guc *guc)
{
	return guc_ads_private_data_offset(guc) +
	       guc_ads_private_data_size(guc);
}

static void guc_policies_init(struct intel_guc *guc)
{
	struct intel_gt *gt = guc_to_gt(guc);
	struct drm_i915_private *i915 = gt->i915;
	u32 global_flags = 0;

	ads_blob_write(guc, policies.dpc_promote_time,
		       GLOBAL_POLICY_DEFAULT_DPC_PROMOTE_TIME_US);
	ads_blob_write(guc, policies.max_num_work_items,
		       GLOBAL_POLICY_MAX_NUM_WI);

	if (i915->params.reset < 2)
		global_flags |= GLOBAL_POLICY_DISABLE_ENGINE_RESET;

	ads_blob_write(guc, policies.global_flags, global_flags);
	ads_blob_write(guc, policies.is_valid, 1);
}

void intel_guc_ads_print_policy_info(struct intel_guc *guc,
				     struct drm_printer *dp)
{
	if (unlikely(iosys_map_is_null(&guc->ads_map)))
		return;

	drm_printf(dp, "Global scheduling policies:\n");
	drm_printf(dp, "  DPC promote time   = %u\n",
		   ads_blob_read(guc, policies.dpc_promote_time));
	drm_printf(dp, "  Max num work items = %u\n",
		   ads_blob_read(guc, policies.max_num_work_items));
	drm_printf(dp, "  Flags              = %u\n",
		   ads_blob_read(guc, policies.global_flags));
}

static int guc_action_policies_update(struct intel_guc *guc, u32 policy_offset)
{
	u32 action[] = {
		INTEL_GUC_ACTION_GLOBAL_SCHED_POLICY_CHANGE,
		policy_offset
	};

	return intel_guc_send_busy_loop(guc, action, ARRAY_SIZE(action), 0, true);
}

int intel_guc_global_policies_update(struct intel_guc *guc)
{
	struct intel_gt *gt = guc_to_gt(guc);
	u32 scheduler_policies;
	intel_wakeref_t wakeref;
	int ret;

	if (iosys_map_is_null(&guc->ads_map))
		return -EOPNOTSUPP;

	scheduler_policies = ads_blob_read(guc, ads.scheduler_policies);
	GEM_BUG_ON(!scheduler_policies);

	guc_policies_init(guc);

	if (!intel_guc_is_ready(guc))
		return 0;

	with_intel_runtime_pm(&gt->i915->runtime_pm, wakeref)
		ret = guc_action_policies_update(guc, scheduler_policies);

	return ret;
}

static void guc_mapping_table_init(struct intel_gt *gt,
				   struct iosys_map *info_map)
{
	unsigned int i, j;
	struct intel_engine_cs *engine;
	enum intel_engine_id id;

	/* Table must be set to invalid values for entries not used */
	for (i = 0; i < GUC_MAX_ENGINE_CLASSES; ++i)
		for (j = 0; j < GUC_MAX_INSTANCES_PER_CLASS; ++j)
			info_map_write(info_map, mapping_table[i][j],
				       GUC_MAX_INSTANCES_PER_CLASS);

	for_each_engine(engine, gt, id) {
		u8 guc_class = engine_class_to_guc_class(engine->class);

		info_map_write(info_map, mapping_table[guc_class][ilog2(engine->logical_mask)],
			       engine->instance);
	}
}

/*
 * The save/restore register list must be pre-calculated to a temporary
 * buffer before it can be copied inside the ADS.
 */
struct temp_regset {
	/*
	 * ptr to the section of the storage for the engine currently being
	 * worked on
	 */
	struct guc_mmio_reg *registers;
	/* ptr to the base of the allocated storage for all engines */
	struct guc_mmio_reg *storage;
	u32 storage_used;
	u32 storage_max;
};

static int guc_mmio_reg_cmp(const void *a, const void *b)
{
	const struct guc_mmio_reg *ra = a;
	const struct guc_mmio_reg *rb = b;

	return (int)ra->offset - (int)rb->offset;
}

static struct guc_mmio_reg * __must_check
__mmio_reg_add(struct temp_regset *regset, struct guc_mmio_reg *reg)
{
	u32 pos = regset->storage_used;
	struct guc_mmio_reg *slot;

	if (pos >= regset->storage_max) {
		size_t size = ALIGN((pos + 1) * sizeof(*slot), PAGE_SIZE);
		struct guc_mmio_reg *r = krealloc(regset->storage,
						  size, GFP_KERNEL);
		if (!r) {
			WARN_ONCE(1, "Incomplete regset list: can't add register (%d)\n",
				  -ENOMEM);
			return ERR_PTR(-ENOMEM);
		}

		regset->registers = r + (regset->registers - regset->storage);
		regset->storage = r;
		regset->storage_max = size / sizeof(*slot);
	}

	slot = &regset->storage[pos];
	regset->storage_used++;
	*slot = *reg;

	return slot;
}

static long __must_check guc_mmio_reg_add(struct temp_regset *regset,
					  u32 offset, u32 flags)
{
	u32 count = regset->storage_used - (regset->registers - regset->storage);
	struct guc_mmio_reg reg = {
		.offset = offset,
		.flags = flags,
	};
	struct guc_mmio_reg *slot;

	/*
	 * The mmio list is built using separate lists within the driver.
	 * It's possible that at some point we may attempt to add the same
	 * register more than once. Do not consider this an error; silently
	 * move on if the register is already in the list.
	 */
	if (bsearch(&reg, regset->registers, count,
		    sizeof(reg), guc_mmio_reg_cmp))
		return 0;

	slot = __mmio_reg_add(regset, &reg);
	if (IS_ERR(slot))
		return PTR_ERR(slot);

	while (slot-- > regset->registers) {
		GEM_BUG_ON(slot[0].offset == slot[1].offset);
		if (slot[1].offset > slot[0].offset)
			break;

		swap(slot[1], slot[0]);
	}

	return 0;
}

#define GUC_MMIO_REG_ADD(regset, reg, masked) \
	guc_mmio_reg_add(regset, \
			 i915_mmio_reg_offset((reg)), \
			 (masked) ? GUC_REGSET_MASKED : 0)

static int guc_mmio_regset_init(struct temp_regset *regset,
				struct intel_engine_cs *engine)
{
	const u32 base = engine->mmio_base;
	struct i915_wa_list *wal = &engine->wa_list;
	struct i915_wa *wa;
	unsigned int i;
	int ret = 0;

	/*
	 * Each engine's registers point to a new start relative to
	 * storage
	 */
	regset->registers = regset->storage + regset->storage_used;

	ret |= GUC_MMIO_REG_ADD(regset, RING_MODE_GEN7(base), true);
	ret |= GUC_MMIO_REG_ADD(regset, RING_HWS_PGA(base), false);
	ret |= GUC_MMIO_REG_ADD(regset, RING_IMR(base), false);

	for (i = 0, wa = wal->list; i < wal->count; i++, wa++)
		ret |= GUC_MMIO_REG_ADD(regset, wa->reg, wa->masked_reg);

	/* Be extra paranoid and include all whitelist registers. */
	for (i = 0; i < RING_MAX_NONPRIV_SLOTS; i++)
		ret |= GUC_MMIO_REG_ADD(regset,
					RING_FORCE_TO_NONPRIV(base, i),
					false);

	/* add in local MOCS registers */
	for (i = 0; i < GEN9_LNCFCMOCS_REG_COUNT; i++)
		ret |= GUC_MMIO_REG_ADD(regset, GEN9_LNCFCMOCS(i), false);

	return ret ? -1 : 0;
}

static long guc_mmio_reg_state_create(struct intel_guc *guc)
{
	struct intel_gt *gt = guc_to_gt(guc);
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	struct temp_regset temp_set = {};
	long total = 0;
	long ret;

	for_each_engine(engine, gt, id) {
		u32 used = temp_set.storage_used;

		ret = guc_mmio_regset_init(&temp_set, engine);
		if (ret < 0)
			goto fail_regset_init;

		guc->ads_regset_count[id] = temp_set.storage_used - used;
		total += guc->ads_regset_count[id];
	}

	guc->ads_regset = temp_set.storage;

	drm_dbg(&guc_to_gt(guc)->i915->drm, "Used %zu KB for temporary ADS regset\n",
		(temp_set.storage_max * sizeof(struct guc_mmio_reg)) >> 10);

	return total * sizeof(struct guc_mmio_reg);

fail_regset_init:
	kfree(temp_set.storage);
	return ret;
}

static void guc_mmio_reg_state_init(struct intel_guc *guc,
				    struct __guc_ads_blob *blob)
{
	struct intel_gt *gt = guc_to_gt(guc);
	struct intel_engine_cs *engine;
	struct guc_mmio_reg *ads_registers;
	enum intel_engine_id id;
	u32 addr_ggtt, offset;

	offset = guc_ads_regset_offset(guc);
	addr_ggtt = intel_guc_ggtt_offset(guc, guc->ads_vma) + offset;
	ads_registers = (struct guc_mmio_reg *)(((u8 *)blob) + offset);

	memcpy(ads_registers, guc->ads_regset, guc->ads_regset_size);

	for_each_engine(engine, gt, id) {
		u32 count = guc->ads_regset_count[id];
		struct guc_mmio_reg_set *ads_reg_set;
		u8 guc_class;

		/* Class index is checked in class converter */
		GEM_BUG_ON(engine->instance >= GUC_MAX_INSTANCES_PER_CLASS);

		guc_class = engine_class_to_guc_class(engine->class);
		ads_reg_set = &blob->ads.reg_state_list[guc_class][engine->instance];

		if (!count) {
			ads_reg_set->address = 0;
			ads_reg_set->count = 0;
			continue;
		}

		ads_reg_set->address = addr_ggtt;
		ads_reg_set->count = count;

		addr_ggtt += count * sizeof(struct guc_mmio_reg);
	}
}

static void fill_engine_enable_masks(struct intel_gt *gt,
				     struct iosys_map *info_map)
{
	info_map_write(info_map, engine_enabled_masks[GUC_RENDER_CLASS], 1);
	info_map_write(info_map, engine_enabled_masks[GUC_BLITTER_CLASS], 1);
	info_map_write(info_map, engine_enabled_masks[GUC_VIDEO_CLASS], VDBOX_MASK(gt));
	info_map_write(info_map, engine_enabled_masks[GUC_VIDEOENHANCE_CLASS], VEBOX_MASK(gt));
}

#define LR_HW_CONTEXT_SIZE (80 * sizeof(u32))
#define LRC_SKIP_SIZE (LRC_PPHWSP_SZ * PAGE_SIZE + LR_HW_CONTEXT_SIZE)
static int guc_prep_golden_context(struct intel_guc *guc)
{
	struct intel_gt *gt = guc_to_gt(guc);
	u32 addr_ggtt, offset;
	u32 total_size = 0, alloc_size, real_size;
	u8 engine_class, guc_class;
	struct guc_gt_system_info local_info;
	struct iosys_map info_map;

	/*
	 * Reserve the memory for the golden contexts and point GuC at it but
	 * leave it empty for now. The context data will be filled in later
	 * once there is something available to put there.
	 *
	 * Note that the HWSP and ring context are not included.
	 *
	 * Note also that the storage must be pinned in the GGTT, so that the
	 * address won't change after GuC has been told where to find it. The
	 * GuC will also validate that the LRC base + size fall within the
	 * allowed GGTT range.
	 */
	if (!iosys_map_is_null(&guc->ads_map)) {
		offset = guc_ads_golden_ctxt_offset(guc);
		addr_ggtt = intel_guc_ggtt_offset(guc, guc->ads_vma) + offset;
		info_map = IOSYS_MAP_INIT_OFFSET(&guc->ads_map,
						 offsetof(struct __guc_ads_blob, system_info));
	} else {
		memset(&local_info, 0, sizeof(local_info));
		iosys_map_set_vaddr(&info_map, &local_info);
		fill_engine_enable_masks(gt, &info_map);
	}

	for (engine_class = 0; engine_class <= MAX_ENGINE_CLASS; ++engine_class) {
		if (engine_class == OTHER_CLASS)
			continue;

		guc_class = engine_class_to_guc_class(engine_class);

		if (!info_map_read(&info_map, engine_enabled_masks[guc_class]))
			continue;

		real_size = intel_engine_context_size(gt, engine_class);
		alloc_size = PAGE_ALIGN(real_size);
		total_size += alloc_size;

		if (iosys_map_is_null(&guc->ads_map))
			continue;

		/*
		 * This interface is slightly confusing. We need to pass the
		 * base address of the full golden context and the size of just
		 * the engine state, which is the section of the context image
		 * that starts after the execlists context. This is required to
		 * allow the GuC to restore just the engine state when a
		 * watchdog reset occurs.
		 * We calculate the engine state size by removing the size of
		 * what comes before it in the context image (which is identical
		 * on all engines).
		 */
		ads_blob_write(guc, ads.eng_state_size[guc_class],
			       real_size - LRC_SKIP_SIZE);
		ads_blob_write(guc, ads.golden_context_lrca[guc_class],
			       addr_ggtt);

		addr_ggtt += alloc_size;
	}

	/* Make sure current size matches what we calculated previously */
	if (guc->ads_golden_ctxt_size)
		GEM_BUG_ON(guc->ads_golden_ctxt_size != total_size);

	return total_size;
}

static struct intel_engine_cs *find_engine_state(struct intel_gt *gt, u8 engine_class)
{
	struct intel_engine_cs *engine;
	enum intel_engine_id id;

	for_each_engine(engine, gt, id) {
		if (engine->class != engine_class)
			continue;

		if (!engine->default_state)
			continue;

		return engine;
	}

	return NULL;
}

static void guc_init_golden_context(struct intel_guc *guc)
{
	struct intel_engine_cs *engine;
	struct intel_gt *gt = guc_to_gt(guc);
	unsigned long offset;
	u32 addr_ggtt, total_size = 0, alloc_size, real_size;
	u8 engine_class, guc_class;

	if (!intel_uc_uses_guc_submission(&gt->uc))
		return;

	GEM_BUG_ON(iosys_map_is_null(&guc->ads_map));

	/*
	 * Go back and fill in the golden context data now that it is
	 * available.
	 */
	offset = guc_ads_golden_ctxt_offset(guc);
	addr_ggtt = intel_guc_ggtt_offset(guc, guc->ads_vma) + offset;

	for (engine_class = 0; engine_class <= MAX_ENGINE_CLASS; ++engine_class) {
		if (engine_class == OTHER_CLASS)
			continue;

		guc_class = engine_class_to_guc_class(engine_class);
		if (!ads_blob_read(guc, system_info.engine_enabled_masks[guc_class]))
			continue;

		real_size = intel_engine_context_size(gt, engine_class);
		alloc_size = PAGE_ALIGN(real_size);
		total_size += alloc_size;

		engine = find_engine_state(gt, engine_class);
		if (!engine) {
			drm_err(&gt->i915->drm, "No engine state recorded for class %d!\n",
				engine_class);
			ads_blob_write(guc, ads.eng_state_size[guc_class], 0);
			ads_blob_write(guc, ads.golden_context_lrca[guc_class], 0);
			continue;
		}

		GEM_BUG_ON(ads_blob_read(guc, ads.eng_state_size[guc_class]) !=
			   real_size - LRC_SKIP_SIZE);
		GEM_BUG_ON(ads_blob_read(guc, ads.golden_context_lrca[guc_class]) != addr_ggtt);

		addr_ggtt += alloc_size;

		shmem_read_to_iosys_map(engine->default_state, 0, &guc->ads_map,
					offset, real_size);
		offset += alloc_size;
	}

	GEM_BUG_ON(guc->ads_golden_ctxt_size != total_size);
}

static void guc_capture_list_init(struct intel_guc *guc, struct __guc_ads_blob *blob)
{
	int i, j;
	u32 addr_ggtt, offset;

	offset = guc_ads_capture_offset(guc);
	addr_ggtt = intel_guc_ggtt_offset(guc, guc->ads_vma) + offset;

	/* FIXME: Populate a proper capture list */

	for (i = 0; i < GUC_CAPTURE_LIST_INDEX_MAX; i++) {
		for (j = 0; j < GUC_MAX_ENGINE_CLASSES; j++) {
			blob->ads.capture_instance[i][j] = addr_ggtt;
			blob->ads.capture_class[i][j] = addr_ggtt;
		}

		blob->ads.capture_global[i] = addr_ggtt;
	}
}

static void __guc_ads_init(struct intel_guc *guc)
{
	struct intel_gt *gt = guc_to_gt(guc);
	struct drm_i915_private *i915 = gt->i915;
	struct __guc_ads_blob *blob = guc->ads_blob;
	struct iosys_map info_map = IOSYS_MAP_INIT_OFFSET(&guc->ads_map,
			offsetof(struct __guc_ads_blob, system_info));
	u32 base;

	/* GuC scheduling policies */
	guc_policies_init(guc);

	/* System info */
	fill_engine_enable_masks(gt, &info_map);

	blob->system_info.generic_gt_sysinfo[GUC_GENERIC_GT_SYSINFO_SLICE_ENABLED] =
		hweight8(gt->info.sseu.slice_mask);
	blob->system_info.generic_gt_sysinfo[GUC_GENERIC_GT_SYSINFO_VDBOX_SFC_SUPPORT_MASK] =
		gt->info.vdbox_sfc_access;

	if (GRAPHICS_VER(i915) >= 12 && !IS_DGFX(i915)) {
		u32 distdbreg = intel_uncore_read(gt->uncore,
						  GEN12_DIST_DBS_POPULATED);
		blob->system_info.generic_gt_sysinfo[GUC_GENERIC_GT_SYSINFO_DOORBELL_COUNT_PER_SQIDI] =
			((distdbreg >> GEN12_DOORBELLS_PER_SQIDI_SHIFT) &
			 GEN12_DOORBELLS_PER_SQIDI) + 1;
	}

	/* Golden contexts for re-initialising after a watchdog reset */
	guc_prep_golden_context(guc);

	guc_mapping_table_init(guc_to_gt(guc), &info_map);

	base = intel_guc_ggtt_offset(guc, guc->ads_vma);

	/* Capture list for hang debug */
	guc_capture_list_init(guc, blob);

	/* ADS */
	blob->ads.scheduler_policies = base + ptr_offset(blob, policies);
	blob->ads.gt_system_info = base + ptr_offset(blob, system_info);

	/* MMIO save/restore list */
	guc_mmio_reg_state_init(guc, blob);

	/* Private Data */
	blob->ads.private_data = base + guc_ads_private_data_offset(guc);

	i915_gem_object_flush_map(guc->ads_vma->obj);
}

/**
 * intel_guc_ads_create() - allocates and initializes GuC ADS.
 * @guc: intel_guc struct
 *
 * GuC needs memory block (Additional Data Struct), where it will store
 * some data. Allocate and initialize such memory block for GuC use.
 */
int intel_guc_ads_create(struct intel_guc *guc)
{
	u32 size;
	int ret;

	GEM_BUG_ON(guc->ads_vma);

	/*
	 * Create reg state size dynamically on system memory to be copied to
	 * the final ads blob on gt init/reset
	 */
	ret = guc_mmio_reg_state_create(guc);
	if (ret < 0)
		return ret;
	guc->ads_regset_size = ret;

	/* Likewise the golden contexts: */
	ret = guc_prep_golden_context(guc);
	if (ret < 0)
		return ret;
	guc->ads_golden_ctxt_size = ret;

	/* Now the total size can be determined: */
	size = guc_ads_blob_size(guc);

	ret = intel_guc_allocate_and_map_vma(guc, size, &guc->ads_vma,
					     (void **)&guc->ads_blob);
	if (ret)
		return ret;

	if (i915_gem_object_is_lmem(guc->ads_vma->obj))
		iosys_map_set_vaddr_iomem(&guc->ads_map, (void __iomem *)guc->ads_blob);
	else
		iosys_map_set_vaddr(&guc->ads_map, guc->ads_blob);

	__guc_ads_init(guc);

	return 0;
}

void intel_guc_ads_init_late(struct intel_guc *guc)
{
	/*
	 * The golden context setup requires the saved engine state from
	 * __engines_record_defaults(). However, that requires engines to be
	 * operational which means the ADS must already have been configured.
	 * Fortunately, the golden context state is not needed until a hang
	 * occurs, so it can be filled in during this late init phase.
	 */
	guc_init_golden_context(guc);
}

void intel_guc_ads_destroy(struct intel_guc *guc)
{
	i915_vma_unpin_and_release(&guc->ads_vma, I915_VMA_RELEASE_MAP);
	guc->ads_blob = NULL;
	iosys_map_clear(&guc->ads_map);
	kfree(guc->ads_regset);
}

static void guc_ads_private_data_reset(struct intel_guc *guc)
{
	u32 size;

	size = guc_ads_private_data_size(guc);
	if (!size)
		return;

	iosys_map_memset(&guc->ads_map, guc_ads_private_data_offset(guc),
			 0, size);
}

/**
 * intel_guc_ads_reset() - prepares GuC Additional Data Struct for reuse
 * @guc: intel_guc struct
 *
 * GuC stores some data in ADS, which might be stale after a reset.
 * Reinitialize whole ADS in case any part of it was corrupted during
 * previous GuC run.
 */
void intel_guc_ads_reset(struct intel_guc *guc)
{
	if (!guc->ads_vma)
		return;

	__guc_ads_init(guc);

	guc_ads_private_data_reset(guc);
}

u32 intel_guc_engine_usage_offset(struct intel_guc *guc)
{
	return intel_guc_ggtt_offset(guc, guc->ads_vma) +
		offsetof(struct __guc_ads_blob, engine_usage);
}

struct iosys_map intel_guc_engine_usage_record_map(struct intel_engine_cs *engine)
{
	struct intel_guc *guc = &engine->gt->uc.guc;
	u8 guc_class = engine_class_to_guc_class(engine->class);
	size_t offset = offsetof(struct __guc_ads_blob,
				 engine_usage.engines[guc_class][ilog2(engine->logical_mask)]);

	return IOSYS_MAP_INIT_OFFSET(&guc->ads_map, offset);
}
