/*
 * Copyright 2016 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include <linux/firmware.h>
#include <drm/drmP.h>
#include "amdgpu.h"
#include "amdgpu_vcn.h"
#include "soc15d.h"
#include "soc15_common.h"

#include "vega10/soc15ip.h"
#include "raven1/VCN/vcn_1_0_offset.h"
#include "raven1/VCN/vcn_1_0_sh_mask.h"
#include "vega10/HDP/hdp_4_0_offset.h"
#include "raven1/MMHUB/mmhub_9_1_offset.h"
#include "raven1/MMHUB/mmhub_9_1_sh_mask.h"

static int vcn_v1_0_start(struct amdgpu_device *adev);
static int vcn_v1_0_stop(struct amdgpu_device *adev);
static void vcn_v1_0_set_dec_ring_funcs(struct amdgpu_device *adev);
static void vcn_v1_0_set_irq_funcs(struct amdgpu_device *adev);

/**
 * vcn_v1_0_early_init - set function pointers
 *
 * @handle: amdgpu_device pointer
 *
 * Set ring and irq function pointers
 */
static int vcn_v1_0_early_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	vcn_v1_0_set_dec_ring_funcs(adev);
	vcn_v1_0_set_irq_funcs(adev);

	return 0;
}

/**
 * vcn_v1_0_sw_init - sw init for VCN block
 *
 * @handle: amdgpu_device pointer
 *
 * Load firmware and sw initialization
 */
static int vcn_v1_0_sw_init(void *handle)
{
	struct amdgpu_ring *ring;
	int r;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	/* VCN TRAP */
	r = amdgpu_irq_add_id(adev, AMDGPU_IH_CLIENTID_VCN, 124, &adev->vcn.irq);
	if (r)
		return r;

	r = amdgpu_vcn_sw_init(adev);
	if (r)
		return r;

	r = amdgpu_vcn_resume(adev);
	if (r)
		return r;

	ring = &adev->vcn.ring_dec;
	sprintf(ring->name, "vcn_dec");
	r = amdgpu_ring_init(adev, ring, 512, &adev->vcn.irq, 0);

	return r;
}

/**
 * vcn_v1_0_sw_fini - sw fini for VCN block
 *
 * @handle: amdgpu_device pointer
 *
 * VCN suspend and free up sw allocation
 */
static int vcn_v1_0_sw_fini(void *handle)
{
	int r;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	r = amdgpu_vcn_suspend(adev);
	if (r)
		return r;

	r = amdgpu_vcn_sw_fini(adev);

	return r;
}

/**
 * vcn_v1_0_hw_init - start and test VCN block
 *
 * @handle: amdgpu_device pointer
 *
 * Initialize the hardware, boot up the VCPU and do some testing
 */
static int vcn_v1_0_hw_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct amdgpu_ring *ring = &adev->vcn.ring_dec;
	int r;

	r = vcn_v1_0_start(adev);
	if (r)
		goto done;

	ring->ready = true;
	r = amdgpu_ring_test_ring(ring);
	if (r) {
		ring->ready = false;
		goto done;
	}

done:
	if (!r)
		DRM_INFO("VCN decode initialized successfully.\n");

	return r;
}

/**
 * vcn_v1_0_hw_fini - stop the hardware block
 *
 * @handle: amdgpu_device pointer
 *
 * Stop the VCN block, mark ring as not ready any more
 */
static int vcn_v1_0_hw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct amdgpu_ring *ring = &adev->vcn.ring_dec;
	int r;

	r = vcn_v1_0_stop(adev);
	if (r)
		return r;

	ring->ready = false;

	return 0;
}

/**
 * vcn_v1_0_suspend - suspend VCN block
 *
 * @handle: amdgpu_device pointer
 *
 * HW fini and suspend VCN block
 */
static int vcn_v1_0_suspend(void *handle)
{
	int r;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	r = vcn_v1_0_hw_fini(adev);
	if (r)
		return r;

	r = amdgpu_vcn_suspend(adev);

	return r;
}

/**
 * vcn_v1_0_resume - resume VCN block
 *
 * @handle: amdgpu_device pointer
 *
 * Resume firmware and hw init VCN block
 */
static int vcn_v1_0_resume(void *handle)
{
	int r;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	r = amdgpu_vcn_resume(adev);
	if (r)
		return r;

	r = vcn_v1_0_hw_init(adev);

	return r;
}

/**
 * vcn_v1_0_mc_resume - memory controller programming
 *
 * @adev: amdgpu_device pointer
 *
 * Let the VCN memory controller know it's offsets
 */
static void vcn_v1_0_mc_resume(struct amdgpu_device *adev)
{
	uint64_t offset;
	uint32_t size;

	/* programm memory controller bits 0-27 */
	WREG32(SOC15_REG_OFFSET(UVD, 0, mmUVD_LMI_VCPU_CACHE_64BIT_BAR_LOW),
			lower_32_bits(adev->vcn.gpu_addr));
	WREG32(SOC15_REG_OFFSET(UVD, 0, mmUVD_LMI_VCPU_CACHE_64BIT_BAR_HIGH),
			upper_32_bits(adev->vcn.gpu_addr));

	/* Current FW has no signed header, but will be added later on */
	/* offset = AMDGPU_VCN_FIRMWARE_OFFSET; */
	offset = 0;
	size = AMDGPU_GPU_PAGE_ALIGN(adev->vcn.fw->size + 4);
	WREG32(SOC15_REG_OFFSET(UVD, 0, mmUVD_VCPU_CACHE_OFFSET0), offset >> 3);
	WREG32(SOC15_REG_OFFSET(UVD, 0, mmUVD_VCPU_CACHE_SIZE0), size);

	offset += size;
	size = AMDGPU_VCN_HEAP_SIZE;
	WREG32(SOC15_REG_OFFSET(UVD, 0, mmUVD_VCPU_CACHE_OFFSET1), offset >> 3);
	WREG32(SOC15_REG_OFFSET(UVD, 0, mmUVD_VCPU_CACHE_SIZE1), size);

	offset += size;
	size = AMDGPU_VCN_STACK_SIZE + (AMDGPU_VCN_SESSION_SIZE * 40);
	WREG32(SOC15_REG_OFFSET(UVD, 0, mmUVD_VCPU_CACHE_OFFSET2), offset >> 3);
	WREG32(SOC15_REG_OFFSET(UVD, 0, mmUVD_VCPU_CACHE_SIZE2), size);

	WREG32(SOC15_REG_OFFSET(UVD, 0, mmUVD_UDEC_ADDR_CONFIG),
			adev->gfx.config.gb_addr_config);
	WREG32(SOC15_REG_OFFSET(UVD, 0, mmUVD_UDEC_DB_ADDR_CONFIG),
			adev->gfx.config.gb_addr_config);
	WREG32(SOC15_REG_OFFSET(UVD, 0, mmUVD_UDEC_DBW_ADDR_CONFIG),
			adev->gfx.config.gb_addr_config);
}

/**
 * vcn_v1_0_start - start VCN block
 *
 * @adev: amdgpu_device pointer
 *
 * Setup and start the VCN block
 */
static int vcn_v1_0_start(struct amdgpu_device *adev)
{
	struct amdgpu_ring *ring = &adev->vcn.ring_dec;
	uint32_t rb_bufsz, tmp;
	uint32_t lmi_swap_cntl;
	int i, j, r;

	/* disable byte swapping */
	lmi_swap_cntl = 0;

	vcn_v1_0_mc_resume(adev);

	/* disable clock gating */
	WREG32_P(SOC15_REG_OFFSET(UVD, 0, mmUVD_CGC_CTRL), 0,
			~UVD_CGC_CTRL__DYN_CLOCK_MODE_MASK);

	/* disable interupt */
	WREG32_P(SOC15_REG_OFFSET(UVD, 0, mmUVD_MASTINT_EN), 0,
			~UVD_MASTINT_EN__VCPU_EN_MASK);

	/* stall UMC and register bus before resetting VCPU */
	WREG32_P(SOC15_REG_OFFSET(UVD, 0, mmUVD_LMI_CTRL2),
			UVD_LMI_CTRL2__STALL_ARB_UMC_MASK,
			~UVD_LMI_CTRL2__STALL_ARB_UMC_MASK);
	mdelay(1);

	/* put LMI, VCPU, RBC etc... into reset */
	WREG32(SOC15_REG_OFFSET(UVD, 0, mmUVD_SOFT_RESET),
		UVD_SOFT_RESET__LMI_SOFT_RESET_MASK |
		UVD_SOFT_RESET__VCPU_SOFT_RESET_MASK |
		UVD_SOFT_RESET__LBSI_SOFT_RESET_MASK |
		UVD_SOFT_RESET__RBC_SOFT_RESET_MASK |
		UVD_SOFT_RESET__CSM_SOFT_RESET_MASK |
		UVD_SOFT_RESET__CXW_SOFT_RESET_MASK |
		UVD_SOFT_RESET__TAP_SOFT_RESET_MASK |
		UVD_SOFT_RESET__LMI_UMC_SOFT_RESET_MASK);
	mdelay(5);

	/* initialize VCN memory controller */
	WREG32(SOC15_REG_OFFSET(UVD, 0, mmUVD_LMI_CTRL),
		(0x40 << UVD_LMI_CTRL__WRITE_CLEAN_TIMER__SHIFT) |
		UVD_LMI_CTRL__WRITE_CLEAN_TIMER_EN_MASK |
		UVD_LMI_CTRL__DATA_COHERENCY_EN_MASK |
		UVD_LMI_CTRL__VCPU_DATA_COHERENCY_EN_MASK |
		UVD_LMI_CTRL__REQ_MODE_MASK |
		0x00100000L);

#ifdef __BIG_ENDIAN
	/* swap (8 in 32) RB and IB */
	lmi_swap_cntl = 0xa;
#endif
	WREG32(SOC15_REG_OFFSET(UVD, 0, mmUVD_LMI_SWAP_CNTL), lmi_swap_cntl);

	WREG32(SOC15_REG_OFFSET(UVD, 0, mmUVD_MPC_SET_MUXA0), 0x40c2040);
	WREG32(SOC15_REG_OFFSET(UVD, 0, mmUVD_MPC_SET_MUXA1), 0x0);
	WREG32(SOC15_REG_OFFSET(UVD, 0, mmUVD_MPC_SET_MUXB0), 0x40c2040);
	WREG32(SOC15_REG_OFFSET(UVD, 0, mmUVD_MPC_SET_MUXB1), 0x0);
	WREG32(SOC15_REG_OFFSET(UVD, 0, mmUVD_MPC_SET_ALU), 0);
	WREG32(SOC15_REG_OFFSET(UVD, 0, mmUVD_MPC_SET_MUX), 0x88);

	/* take all subblocks out of reset, except VCPU */
	WREG32(SOC15_REG_OFFSET(UVD, 0, mmUVD_SOFT_RESET),
			UVD_SOFT_RESET__VCPU_SOFT_RESET_MASK);
	mdelay(5);

	/* enable VCPU clock */
	WREG32(SOC15_REG_OFFSET(UVD, 0, mmUVD_VCPU_CNTL),
			UVD_VCPU_CNTL__CLK_EN_MASK);

	/* enable UMC */
	WREG32_P(SOC15_REG_OFFSET(UVD, 0, mmUVD_LMI_CTRL2), 0,
			~UVD_LMI_CTRL2__STALL_ARB_UMC_MASK);

	/* boot up the VCPU */
	WREG32(SOC15_REG_OFFSET(UVD, 0, mmUVD_SOFT_RESET), 0);
	mdelay(10);

	for (i = 0; i < 10; ++i) {
		uint32_t status;

		for (j = 0; j < 100; ++j) {
			status = RREG32(SOC15_REG_OFFSET(UVD, 0, mmUVD_STATUS));
			if (status & 2)
				break;
			mdelay(10);
		}
		r = 0;
		if (status & 2)
			break;

		DRM_ERROR("VCN decode not responding, trying to reset the VCPU!!!\n");
		WREG32_P(SOC15_REG_OFFSET(UVD, 0, mmUVD_SOFT_RESET),
				UVD_SOFT_RESET__VCPU_SOFT_RESET_MASK,
				~UVD_SOFT_RESET__VCPU_SOFT_RESET_MASK);
		mdelay(10);
		WREG32_P(SOC15_REG_OFFSET(UVD, 0, mmUVD_SOFT_RESET), 0,
				~UVD_SOFT_RESET__VCPU_SOFT_RESET_MASK);
		mdelay(10);
		r = -1;
	}

	if (r) {
		DRM_ERROR("VCN decode not responding, giving up!!!\n");
		return r;
	}
	/* enable master interrupt */
	WREG32_P(SOC15_REG_OFFSET(UVD, 0, mmUVD_MASTINT_EN),
		(UVD_MASTINT_EN__VCPU_EN_MASK|UVD_MASTINT_EN__SYS_EN_MASK),
		~(UVD_MASTINT_EN__VCPU_EN_MASK|UVD_MASTINT_EN__SYS_EN_MASK));

	/* clear the bit 4 of VCN_STATUS */
	WREG32_P(SOC15_REG_OFFSET(UVD, 0, mmUVD_STATUS), 0,
			~(2 << UVD_STATUS__VCPU_REPORT__SHIFT));

	/* force RBC into idle state */
	rb_bufsz = order_base_2(ring->ring_size);
	tmp = REG_SET_FIELD(0, UVD_RBC_RB_CNTL, RB_BUFSZ, rb_bufsz);
	tmp = REG_SET_FIELD(tmp, UVD_RBC_RB_CNTL, RB_BLKSZ, 1);
	tmp = REG_SET_FIELD(tmp, UVD_RBC_RB_CNTL, RB_NO_FETCH, 1);
	tmp = REG_SET_FIELD(tmp, UVD_RBC_RB_CNTL, RB_WPTR_POLL_EN, 0);
	tmp = REG_SET_FIELD(tmp, UVD_RBC_RB_CNTL, RB_NO_UPDATE, 1);
	tmp = REG_SET_FIELD(tmp, UVD_RBC_RB_CNTL, RB_RPTR_WR_EN, 1);
	WREG32(SOC15_REG_OFFSET(UVD, 0, mmUVD_RBC_RB_CNTL), tmp);

	/* set the write pointer delay */
	WREG32(SOC15_REG_OFFSET(UVD, 0, mmUVD_RBC_RB_WPTR_CNTL), 0);

	/* set the wb address */
	WREG32(SOC15_REG_OFFSET(UVD, 0, mmUVD_RBC_RB_RPTR_ADDR),
			(upper_32_bits(ring->gpu_addr) >> 2));

	/* programm the RB_BASE for ring buffer */
	WREG32(SOC15_REG_OFFSET(UVD, 0, mmUVD_LMI_RBC_RB_64BIT_BAR_LOW),
			lower_32_bits(ring->gpu_addr));
	WREG32(SOC15_REG_OFFSET(UVD, 0, mmUVD_LMI_RBC_RB_64BIT_BAR_HIGH),
			upper_32_bits(ring->gpu_addr));

	/* Initialize the ring buffer's read and write pointers */
	WREG32(SOC15_REG_OFFSET(UVD, 0, mmUVD_RBC_RB_RPTR), 0);

	ring->wptr = RREG32(SOC15_REG_OFFSET(UVD, 0, mmUVD_RBC_RB_RPTR));
	WREG32(SOC15_REG_OFFSET(UVD, 0, mmUVD_RBC_RB_WPTR),
			lower_32_bits(ring->wptr));

	WREG32_P(SOC15_REG_OFFSET(UVD, 0, mmUVD_RBC_RB_CNTL), 0,
			~UVD_RBC_RB_CNTL__RB_NO_FETCH_MASK);

	return 0;
}

/**
 * vcn_v1_0_stop - stop VCN block
 *
 * @adev: amdgpu_device pointer
 *
 * stop the VCN block
 */
static int vcn_v1_0_stop(struct amdgpu_device *adev)
{
	/* force RBC into idle state */
	WREG32(SOC15_REG_OFFSET(UVD, 0, mmUVD_RBC_RB_CNTL), 0x11010101);

	/* Stall UMC and register bus before resetting VCPU */
	WREG32_P(SOC15_REG_OFFSET(UVD, 0, mmUVD_LMI_CTRL2),
			UVD_LMI_CTRL2__STALL_ARB_UMC_MASK,
			~UVD_LMI_CTRL2__STALL_ARB_UMC_MASK);
	mdelay(1);

	/* put VCPU into reset */
	WREG32(SOC15_REG_OFFSET(UVD, 0, mmUVD_SOFT_RESET),
			UVD_SOFT_RESET__VCPU_SOFT_RESET_MASK);
	mdelay(5);

	/* disable VCPU clock */
	WREG32(SOC15_REG_OFFSET(UVD, 0, mmUVD_VCPU_CNTL), 0x0);

	/* Unstall UMC and register bus */
	WREG32_P(SOC15_REG_OFFSET(UVD, 0, mmUVD_LMI_CTRL2), 0,
			~UVD_LMI_CTRL2__STALL_ARB_UMC_MASK);

	return 0;
}

static int vcn_v1_0_set_clockgating_state(void *handle,
					  enum amd_clockgating_state state)
{
	/* needed for driver unload*/
	return 0;
}

/**
 * vcn_v1_0_dec_ring_get_rptr - get read pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Returns the current hardware read pointer
 */
static uint64_t vcn_v1_0_dec_ring_get_rptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	return RREG32(SOC15_REG_OFFSET(UVD, 0, mmUVD_RBC_RB_RPTR));
}

/**
 * vcn_v1_0_dec_ring_get_wptr - get write pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Returns the current hardware write pointer
 */
static uint64_t vcn_v1_0_dec_ring_get_wptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	return RREG32(SOC15_REG_OFFSET(UVD, 0, mmUVD_RBC_RB_WPTR));
}

/**
 * vcn_v1_0_dec_ring_set_wptr - set write pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Commits the write pointer to the hardware
 */
static void vcn_v1_0_dec_ring_set_wptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	WREG32(SOC15_REG_OFFSET(UVD, 0, mmUVD_RBC_RB_WPTR), lower_32_bits(ring->wptr));
}

/**
 * vcn_v1_0_dec_ring_insert_start - insert a start command
 *
 * @ring: amdgpu_ring pointer
 *
 * Write a start command to the ring.
 */
static void vcn_v1_0_dec_ring_insert_start(struct amdgpu_ring *ring)
{
	amdgpu_ring_write(ring,
		PACKET0(SOC15_REG_OFFSET(UVD, 0, mmUVD_GPCOM_VCPU_DATA0), 0));
	amdgpu_ring_write(ring, 0);
	amdgpu_ring_write(ring,
		PACKET0(SOC15_REG_OFFSET(UVD, 0, mmUVD_GPCOM_VCPU_CMD), 0));
	amdgpu_ring_write(ring, VCN_CMD_PACKET_START << 1);
}

/**
 * vcn_v1_0_dec_ring_emit_fence - emit an fence & trap command
 *
 * @ring: amdgpu_ring pointer
 * @fence: fence to emit
 *
 * Write a fence and a trap command to the ring.
 */
static void vcn_v1_0_dec_ring_emit_fence(struct amdgpu_ring *ring, u64 addr, u64 seq,
				     unsigned flags)
{
	WARN_ON(flags & AMDGPU_FENCE_FLAG_64BIT);

	amdgpu_ring_write(ring,
		PACKET0(SOC15_REG_OFFSET(UVD, 0, mmUVD_CONTEXT_ID), 0));
	amdgpu_ring_write(ring, seq);
	amdgpu_ring_write(ring,
		PACKET0(SOC15_REG_OFFSET(UVD, 0, mmUVD_GPCOM_VCPU_DATA0), 0));
	amdgpu_ring_write(ring, addr & 0xffffffff);
	amdgpu_ring_write(ring,
		PACKET0(SOC15_REG_OFFSET(UVD, 0, mmUVD_GPCOM_VCPU_DATA1), 0));
	amdgpu_ring_write(ring, upper_32_bits(addr) & 0xff);
	amdgpu_ring_write(ring,
		PACKET0(SOC15_REG_OFFSET(UVD, 0, mmUVD_GPCOM_VCPU_CMD), 0));
	amdgpu_ring_write(ring, VCN_CMD_FENCE << 1);

	amdgpu_ring_write(ring,
		PACKET0(SOC15_REG_OFFSET(UVD, 0, mmUVD_GPCOM_VCPU_DATA0), 0));
	amdgpu_ring_write(ring, 0);
	amdgpu_ring_write(ring,
		PACKET0(SOC15_REG_OFFSET(UVD, 0, mmUVD_GPCOM_VCPU_DATA1), 0));
	amdgpu_ring_write(ring, 0);
	amdgpu_ring_write(ring,
		PACKET0(SOC15_REG_OFFSET(UVD, 0, mmUVD_GPCOM_VCPU_CMD), 0));
	amdgpu_ring_write(ring, VCN_CMD_TRAP << 1);
}

/**
 * vcn_v1_0_dec_ring_hdp_invalidate - emit an hdp invalidate
 *
 * @ring: amdgpu_ring pointer
 *
 * Emits an hdp invalidate.
 */
static void vcn_v1_0_dec_ring_emit_hdp_invalidate(struct amdgpu_ring *ring)
{
	amdgpu_ring_write(ring, PACKET0(SOC15_REG_OFFSET(HDP, 0, mmHDP_DEBUG0), 0));
	amdgpu_ring_write(ring, 1);
}

/**
 * vcn_v1_0_dec_ring_emit_ib - execute indirect buffer
 *
 * @ring: amdgpu_ring pointer
 * @ib: indirect buffer to execute
 *
 * Write ring commands to execute the indirect buffer
 */
static void vcn_v1_0_dec_ring_emit_ib(struct amdgpu_ring *ring,
				  struct amdgpu_ib *ib,
				  unsigned vm_id, bool ctx_switch)
{
	amdgpu_ring_write(ring,
		PACKET0(SOC15_REG_OFFSET(UVD, 0, mmUVD_LMI_RBC_IB_VMID), 0));
	amdgpu_ring_write(ring, vm_id);

	amdgpu_ring_write(ring,
		PACKET0(SOC15_REG_OFFSET(UVD, 0, mmUVD_LMI_RBC_IB_64BIT_BAR_LOW), 0));
	amdgpu_ring_write(ring, lower_32_bits(ib->gpu_addr));
	amdgpu_ring_write(ring,
		PACKET0(SOC15_REG_OFFSET(UVD, 0, mmUVD_LMI_RBC_IB_64BIT_BAR_HIGH), 0));
	amdgpu_ring_write(ring, upper_32_bits(ib->gpu_addr));
	amdgpu_ring_write(ring,
		PACKET0(SOC15_REG_OFFSET(UVD, 0, mmUVD_RBC_IB_SIZE), 0));
	amdgpu_ring_write(ring, ib->length_dw);
}

static void vcn_v1_0_dec_vm_reg_write(struct amdgpu_ring *ring,
				uint32_t data0, uint32_t data1)
{
	amdgpu_ring_write(ring,
		PACKET0(SOC15_REG_OFFSET(UVD, 0, mmUVD_GPCOM_VCPU_DATA0), 0));
	amdgpu_ring_write(ring, data0);
	amdgpu_ring_write(ring,
		PACKET0(SOC15_REG_OFFSET(UVD, 0, mmUVD_GPCOM_VCPU_DATA1), 0));
	amdgpu_ring_write(ring, data1);
	amdgpu_ring_write(ring,
		PACKET0(SOC15_REG_OFFSET(UVD, 0, mmUVD_GPCOM_VCPU_CMD), 0));
	amdgpu_ring_write(ring, VCN_CMD_WRITE_REG << 1);
}

static void vcn_v1_0_dec_vm_reg_wait(struct amdgpu_ring *ring,
				uint32_t data0, uint32_t data1, uint32_t mask)
{
	amdgpu_ring_write(ring,
		PACKET0(SOC15_REG_OFFSET(UVD, 0, mmUVD_GPCOM_VCPU_DATA0), 0));
	amdgpu_ring_write(ring, data0);
	amdgpu_ring_write(ring,
		PACKET0(SOC15_REG_OFFSET(UVD, 0, mmUVD_GPCOM_VCPU_DATA1), 0));
	amdgpu_ring_write(ring, data1);
	amdgpu_ring_write(ring,
		PACKET0(SOC15_REG_OFFSET(UVD, 0, mmUVD_GP_SCRATCH8), 0));
	amdgpu_ring_write(ring, mask);
	amdgpu_ring_write(ring,
		PACKET0(SOC15_REG_OFFSET(UVD, 0, mmUVD_GPCOM_VCPU_CMD), 0));
	amdgpu_ring_write(ring, VCN_CMD_REG_READ_COND_WAIT << 1);
}

static void vcn_v1_0_dec_ring_emit_vm_flush(struct amdgpu_ring *ring,
					unsigned vm_id, uint64_t pd_addr)
{
	struct amdgpu_vmhub *hub = &ring->adev->vmhub[ring->funcs->vmhub];
	uint32_t req = ring->adev->gart.gart_funcs->get_invalidate_req(vm_id);
	uint32_t data0, data1, mask;
	unsigned eng = ring->vm_inv_eng;

	pd_addr = pd_addr | 0x1; /* valid bit */
	/* now only use physical base address of PDE and valid */
	BUG_ON(pd_addr & 0xFFFF00000000003EULL);

	data0 = (hub->ctx0_ptb_addr_hi32 + vm_id * 2) << 2;
	data1 = upper_32_bits(pd_addr);
	vcn_v1_0_dec_vm_reg_write(ring, data0, data1);

	data0 = (hub->ctx0_ptb_addr_lo32 + vm_id * 2) << 2;
	data1 = lower_32_bits(pd_addr);
	vcn_v1_0_dec_vm_reg_write(ring, data0, data1);

	data0 = (hub->ctx0_ptb_addr_lo32 + vm_id * 2) << 2;
	data1 = lower_32_bits(pd_addr);
	mask = 0xffffffff;
	vcn_v1_0_dec_vm_reg_wait(ring, data0, data1, mask);

	/* flush TLB */
	data0 = (hub->vm_inv_eng0_req + eng) << 2;
	data1 = req;
	vcn_v1_0_dec_vm_reg_write(ring, data0, data1);

	/* wait for flush */
	data0 = (hub->vm_inv_eng0_ack + eng) << 2;
	data1 = 1 << vm_id;
	mask =  1 << vm_id;
	vcn_v1_0_dec_vm_reg_wait(ring, data0, data1, mask);
}

static int vcn_v1_0_set_interrupt_state(struct amdgpu_device *adev,
					struct amdgpu_irq_src *source,
					unsigned type,
					enum amdgpu_interrupt_state state)
{
	return 0;
}

static int vcn_v1_0_process_interrupt(struct amdgpu_device *adev,
				      struct amdgpu_irq_src *source,
				      struct amdgpu_iv_entry *entry)
{
	DRM_DEBUG("IH: VCN TRAP\n");

	amdgpu_fence_process(&adev->vcn.ring_dec);

	return 0;
}

static const struct amd_ip_funcs vcn_v1_0_ip_funcs = {
	.name = "vcn_v1_0",
	.early_init = vcn_v1_0_early_init,
	.late_init = NULL,
	.sw_init = vcn_v1_0_sw_init,
	.sw_fini = vcn_v1_0_sw_fini,
	.hw_init = vcn_v1_0_hw_init,
	.hw_fini = vcn_v1_0_hw_fini,
	.suspend = vcn_v1_0_suspend,
	.resume = vcn_v1_0_resume,
	.is_idle = NULL /* vcn_v1_0_is_idle */,
	.wait_for_idle = NULL /* vcn_v1_0_wait_for_idle */,
	.check_soft_reset = NULL /* vcn_v1_0_check_soft_reset */,
	.pre_soft_reset = NULL /* vcn_v1_0_pre_soft_reset */,
	.soft_reset = NULL /* vcn_v1_0_soft_reset */,
	.post_soft_reset = NULL /* vcn_v1_0_post_soft_reset */,
	.set_clockgating_state = vcn_v1_0_set_clockgating_state,
	.set_powergating_state = NULL /* vcn_v1_0_set_powergating_state */,
};

static const struct amdgpu_ring_funcs vcn_v1_0_dec_ring_vm_funcs = {
	.type = AMDGPU_RING_TYPE_VCN_DEC,
	.align_mask = 0xf,
	.nop = PACKET0(SOC15_REG_OFFSET(UVD, 0, mmUVD_NO_OP), 0),
	.support_64bit_ptrs = false,
	.get_rptr = vcn_v1_0_dec_ring_get_rptr,
	.get_wptr = vcn_v1_0_dec_ring_get_wptr,
	.set_wptr = vcn_v1_0_dec_ring_set_wptr,
	.emit_frame_size =
		2 + /* vcn_v1_0_dec_ring_emit_hdp_invalidate */
		34 * AMDGPU_MAX_VMHUBS + /* vcn_v1_0_dec_ring_emit_vm_flush */
		14 + 14 + /* vcn_v1_0_dec_ring_emit_fence x2 vm fence */
		4,
	.emit_ib_size = 8, /* vcn_v1_0_dec_ring_emit_ib */
	.emit_ib = vcn_v1_0_dec_ring_emit_ib,
	.emit_fence = vcn_v1_0_dec_ring_emit_fence,
	.emit_vm_flush = vcn_v1_0_dec_ring_emit_vm_flush,
	.emit_hdp_invalidate = vcn_v1_0_dec_ring_emit_hdp_invalidate,
	.test_ring = amdgpu_vcn_dec_ring_test_ring,
	.test_ib = amdgpu_vcn_dec_ring_test_ib,
	.insert_nop = amdgpu_ring_insert_nop,
	.insert_start = vcn_v1_0_dec_ring_insert_start,
	.pad_ib = amdgpu_ring_generic_pad_ib,
	.begin_use = amdgpu_vcn_ring_begin_use,
	.end_use = amdgpu_vcn_ring_end_use,
};

static void vcn_v1_0_set_dec_ring_funcs(struct amdgpu_device *adev)
{
	adev->vcn.ring_dec.funcs = &vcn_v1_0_dec_ring_vm_funcs;
	DRM_INFO("VCN decode is enabled in VM mode\n");
}

static const struct amdgpu_irq_src_funcs vcn_v1_0_irq_funcs = {
	.set = vcn_v1_0_set_interrupt_state,
	.process = vcn_v1_0_process_interrupt,
};

static void vcn_v1_0_set_irq_funcs(struct amdgpu_device *adev)
{
	adev->vcn.irq.num_types = 1;
	adev->vcn.irq.funcs = &vcn_v1_0_irq_funcs;
}

const struct amdgpu_ip_block_version vcn_v1_0_ip_block =
{
		.type = AMD_IP_BLOCK_TYPE_VCN,
		.major = 1,
		.minor = 0,
		.rev = 0,
		.funcs = &vcn_v1_0_ip_funcs,
};
