/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2018 Intel Corporation
 */

#include "../i915_selftest.h"
#include "igt_flush_test.h"
#include "i915_random.h"

#include "mock_context.h"

struct spinner {
	struct drm_i915_private *i915;
	struct drm_i915_gem_object *hws;
	struct drm_i915_gem_object *obj;
	u32 *batch;
	void *seqno;
};

static int spinner_init(struct spinner *spin, struct drm_i915_private *i915)
{
	unsigned int mode;
	void *vaddr;
	int err;

	GEM_BUG_ON(INTEL_GEN(i915) < 8);

	memset(spin, 0, sizeof(*spin));
	spin->i915 = i915;

	spin->hws = i915_gem_object_create_internal(i915, PAGE_SIZE);
	if (IS_ERR(spin->hws)) {
		err = PTR_ERR(spin->hws);
		goto err;
	}

	spin->obj = i915_gem_object_create_internal(i915, PAGE_SIZE);
	if (IS_ERR(spin->obj)) {
		err = PTR_ERR(spin->obj);
		goto err_hws;
	}

	i915_gem_object_set_cache_level(spin->hws, I915_CACHE_LLC);
	vaddr = i915_gem_object_pin_map(spin->hws, I915_MAP_WB);
	if (IS_ERR(vaddr)) {
		err = PTR_ERR(vaddr);
		goto err_obj;
	}
	spin->seqno = memset(vaddr, 0xff, PAGE_SIZE);

	mode = HAS_LLC(i915) ? I915_MAP_WB : I915_MAP_WC;
	vaddr = i915_gem_object_pin_map(spin->obj, mode);
	if (IS_ERR(vaddr)) {
		err = PTR_ERR(vaddr);
		goto err_unpin_hws;
	}
	spin->batch = vaddr;

	return 0;

err_unpin_hws:
	i915_gem_object_unpin_map(spin->hws);
err_obj:
	i915_gem_object_put(spin->obj);
err_hws:
	i915_gem_object_put(spin->hws);
err:
	return err;
}

static unsigned int seqno_offset(u64 fence)
{
	return offset_in_page(sizeof(u32) * fence);
}

static u64 hws_address(const struct i915_vma *hws,
		       const struct i915_request *rq)
{
	return hws->node.start + seqno_offset(rq->fence.context);
}

static int emit_recurse_batch(struct spinner *spin,
			      struct i915_request *rq,
			      u32 arbitration_command)
{
	struct i915_address_space *vm = &rq->gem_context->ppgtt->vm;
	struct i915_vma *hws, *vma;
	u32 *batch;
	int err;

	vma = i915_vma_instance(spin->obj, vm, NULL);
	if (IS_ERR(vma))
		return PTR_ERR(vma);

	hws = i915_vma_instance(spin->hws, vm, NULL);
	if (IS_ERR(hws))
		return PTR_ERR(hws);

	err = i915_vma_pin(vma, 0, 0, PIN_USER);
	if (err)
		return err;

	err = i915_vma_pin(hws, 0, 0, PIN_USER);
	if (err)
		goto unpin_vma;

	err = i915_vma_move_to_active(vma, rq, 0);
	if (err)
		goto unpin_hws;

	if (!i915_gem_object_has_active_reference(vma->obj)) {
		i915_gem_object_get(vma->obj);
		i915_gem_object_set_active_reference(vma->obj);
	}

	err = i915_vma_move_to_active(hws, rq, 0);
	if (err)
		goto unpin_hws;

	if (!i915_gem_object_has_active_reference(hws->obj)) {
		i915_gem_object_get(hws->obj);
		i915_gem_object_set_active_reference(hws->obj);
	}

	batch = spin->batch;

	*batch++ = MI_STORE_DWORD_IMM_GEN4;
	*batch++ = lower_32_bits(hws_address(hws, rq));
	*batch++ = upper_32_bits(hws_address(hws, rq));
	*batch++ = rq->fence.seqno;

	*batch++ = arbitration_command;

	*batch++ = MI_BATCH_BUFFER_START | 1 << 8 | 1;
	*batch++ = lower_32_bits(vma->node.start);
	*batch++ = upper_32_bits(vma->node.start);
	*batch++ = MI_BATCH_BUFFER_END; /* not reached */

	i915_gem_chipset_flush(spin->i915);

	err = rq->engine->emit_bb_start(rq, vma->node.start, PAGE_SIZE, 0);

unpin_hws:
	i915_vma_unpin(hws);
unpin_vma:
	i915_vma_unpin(vma);
	return err;
}

static struct i915_request *
spinner_create_request(struct spinner *spin,
		       struct i915_gem_context *ctx,
		       struct intel_engine_cs *engine,
		       u32 arbitration_command)
{
	struct i915_request *rq;
	int err;

	rq = i915_request_alloc(engine, ctx);
	if (IS_ERR(rq))
		return rq;

	err = emit_recurse_batch(spin, rq, arbitration_command);
	if (err) {
		i915_request_add(rq);
		return ERR_PTR(err);
	}

	return rq;
}

static u32 hws_seqno(const struct spinner *spin, const struct i915_request *rq)
{
	u32 *seqno = spin->seqno + seqno_offset(rq->fence.context);

	return READ_ONCE(*seqno);
}

static void spinner_end(struct spinner *spin)
{
	*spin->batch = MI_BATCH_BUFFER_END;
	i915_gem_chipset_flush(spin->i915);
}

static void spinner_fini(struct spinner *spin)
{
	spinner_end(spin);

	i915_gem_object_unpin_map(spin->obj);
	i915_gem_object_put(spin->obj);

	i915_gem_object_unpin_map(spin->hws);
	i915_gem_object_put(spin->hws);
}

static bool wait_for_spinner(struct spinner *spin, struct i915_request *rq)
{
	if (!wait_event_timeout(rq->execute,
				READ_ONCE(rq->global_seqno),
				msecs_to_jiffies(10)))
		return false;

	return !(wait_for_us(i915_seqno_passed(hws_seqno(spin, rq),
					       rq->fence.seqno),
			     10) &&
		 wait_for(i915_seqno_passed(hws_seqno(spin, rq),
					    rq->fence.seqno),
			  1000));
}

static int live_sanitycheck(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct intel_engine_cs *engine;
	struct i915_gem_context *ctx;
	enum intel_engine_id id;
	struct spinner spin;
	int err = -ENOMEM;

	if (!HAS_LOGICAL_RING_CONTEXTS(i915))
		return 0;

	mutex_lock(&i915->drm.struct_mutex);
	intel_runtime_pm_get(i915);

	if (spinner_init(&spin, i915))
		goto err_unlock;

	ctx = kernel_context(i915);
	if (!ctx)
		goto err_spin;

	for_each_engine(engine, i915, id) {
		struct i915_request *rq;

		rq = spinner_create_request(&spin, ctx, engine, MI_NOOP);
		if (IS_ERR(rq)) {
			err = PTR_ERR(rq);
			goto err_ctx;
		}

		i915_request_add(rq);
		if (!wait_for_spinner(&spin, rq)) {
			GEM_TRACE("spinner failed to start\n");
			GEM_TRACE_DUMP();
			i915_gem_set_wedged(i915);
			err = -EIO;
			goto err_ctx;
		}

		spinner_end(&spin);
		if (igt_flush_test(i915, I915_WAIT_LOCKED)) {
			err = -EIO;
			goto err_ctx;
		}
	}

	err = 0;
err_ctx:
	kernel_context_close(ctx);
err_spin:
	spinner_fini(&spin);
err_unlock:
	igt_flush_test(i915, I915_WAIT_LOCKED);
	intel_runtime_pm_put(i915);
	mutex_unlock(&i915->drm.struct_mutex);
	return err;
}

static int live_preempt(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct i915_gem_context *ctx_hi, *ctx_lo;
	struct spinner spin_hi, spin_lo;
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	int err = -ENOMEM;

	if (!HAS_LOGICAL_RING_PREEMPTION(i915))
		return 0;

	mutex_lock(&i915->drm.struct_mutex);
	intel_runtime_pm_get(i915);

	if (spinner_init(&spin_hi, i915))
		goto err_unlock;

	if (spinner_init(&spin_lo, i915))
		goto err_spin_hi;

	ctx_hi = kernel_context(i915);
	if (!ctx_hi)
		goto err_spin_lo;
	ctx_hi->sched.priority = I915_CONTEXT_MAX_USER_PRIORITY;

	ctx_lo = kernel_context(i915);
	if (!ctx_lo)
		goto err_ctx_hi;
	ctx_lo->sched.priority = I915_CONTEXT_MIN_USER_PRIORITY;

	for_each_engine(engine, i915, id) {
		struct i915_request *rq;

		rq = spinner_create_request(&spin_lo, ctx_lo, engine,
					    MI_ARB_CHECK);
		if (IS_ERR(rq)) {
			err = PTR_ERR(rq);
			goto err_ctx_lo;
		}

		i915_request_add(rq);
		if (!wait_for_spinner(&spin_lo, rq)) {
			GEM_TRACE("lo spinner failed to start\n");
			GEM_TRACE_DUMP();
			i915_gem_set_wedged(i915);
			err = -EIO;
			goto err_ctx_lo;
		}

		rq = spinner_create_request(&spin_hi, ctx_hi, engine,
					    MI_ARB_CHECK);
		if (IS_ERR(rq)) {
			spinner_end(&spin_lo);
			err = PTR_ERR(rq);
			goto err_ctx_lo;
		}

		i915_request_add(rq);
		if (!wait_for_spinner(&spin_hi, rq)) {
			GEM_TRACE("hi spinner failed to start\n");
			GEM_TRACE_DUMP();
			i915_gem_set_wedged(i915);
			err = -EIO;
			goto err_ctx_lo;
		}

		spinner_end(&spin_hi);
		spinner_end(&spin_lo);
		if (igt_flush_test(i915, I915_WAIT_LOCKED)) {
			err = -EIO;
			goto err_ctx_lo;
		}
	}

	err = 0;
err_ctx_lo:
	kernel_context_close(ctx_lo);
err_ctx_hi:
	kernel_context_close(ctx_hi);
err_spin_lo:
	spinner_fini(&spin_lo);
err_spin_hi:
	spinner_fini(&spin_hi);
err_unlock:
	igt_flush_test(i915, I915_WAIT_LOCKED);
	intel_runtime_pm_put(i915);
	mutex_unlock(&i915->drm.struct_mutex);
	return err;
}

static int live_late_preempt(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct i915_gem_context *ctx_hi, *ctx_lo;
	struct spinner spin_hi, spin_lo;
	struct intel_engine_cs *engine;
	struct i915_sched_attr attr = {};
	enum intel_engine_id id;
	int err = -ENOMEM;

	if (!HAS_LOGICAL_RING_PREEMPTION(i915))
		return 0;

	mutex_lock(&i915->drm.struct_mutex);
	intel_runtime_pm_get(i915);

	if (spinner_init(&spin_hi, i915))
		goto err_unlock;

	if (spinner_init(&spin_lo, i915))
		goto err_spin_hi;

	ctx_hi = kernel_context(i915);
	if (!ctx_hi)
		goto err_spin_lo;

	ctx_lo = kernel_context(i915);
	if (!ctx_lo)
		goto err_ctx_hi;

	for_each_engine(engine, i915, id) {
		struct i915_request *rq;

		rq = spinner_create_request(&spin_lo, ctx_lo, engine,
					    MI_ARB_CHECK);
		if (IS_ERR(rq)) {
			err = PTR_ERR(rq);
			goto err_ctx_lo;
		}

		i915_request_add(rq);
		if (!wait_for_spinner(&spin_lo, rq)) {
			pr_err("First context failed to start\n");
			goto err_wedged;
		}

		rq = spinner_create_request(&spin_hi, ctx_hi, engine, MI_NOOP);
		if (IS_ERR(rq)) {
			spinner_end(&spin_lo);
			err = PTR_ERR(rq);
			goto err_ctx_lo;
		}

		i915_request_add(rq);
		if (wait_for_spinner(&spin_hi, rq)) {
			pr_err("Second context overtook first?\n");
			goto err_wedged;
		}

		attr.priority = I915_PRIORITY_MAX;
		engine->schedule(rq, &attr);

		if (!wait_for_spinner(&spin_hi, rq)) {
			pr_err("High priority context failed to preempt the low priority context\n");
			GEM_TRACE_DUMP();
			goto err_wedged;
		}

		spinner_end(&spin_hi);
		spinner_end(&spin_lo);
		if (igt_flush_test(i915, I915_WAIT_LOCKED)) {
			err = -EIO;
			goto err_ctx_lo;
		}
	}

	err = 0;
err_ctx_lo:
	kernel_context_close(ctx_lo);
err_ctx_hi:
	kernel_context_close(ctx_hi);
err_spin_lo:
	spinner_fini(&spin_lo);
err_spin_hi:
	spinner_fini(&spin_hi);
err_unlock:
	igt_flush_test(i915, I915_WAIT_LOCKED);
	intel_runtime_pm_put(i915);
	mutex_unlock(&i915->drm.struct_mutex);
	return err;

err_wedged:
	spinner_end(&spin_hi);
	spinner_end(&spin_lo);
	i915_gem_set_wedged(i915);
	err = -EIO;
	goto err_ctx_lo;
}

static int live_preempt_hang(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct i915_gem_context *ctx_hi, *ctx_lo;
	struct spinner spin_hi, spin_lo;
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	int err = -ENOMEM;

	if (!HAS_LOGICAL_RING_PREEMPTION(i915))
		return 0;

	if (!intel_has_reset_engine(i915))
		return 0;

	mutex_lock(&i915->drm.struct_mutex);
	intel_runtime_pm_get(i915);

	if (spinner_init(&spin_hi, i915))
		goto err_unlock;

	if (spinner_init(&spin_lo, i915))
		goto err_spin_hi;

	ctx_hi = kernel_context(i915);
	if (!ctx_hi)
		goto err_spin_lo;
	ctx_hi->sched.priority = I915_CONTEXT_MAX_USER_PRIORITY;

	ctx_lo = kernel_context(i915);
	if (!ctx_lo)
		goto err_ctx_hi;
	ctx_lo->sched.priority = I915_CONTEXT_MIN_USER_PRIORITY;

	for_each_engine(engine, i915, id) {
		struct i915_request *rq;

		if (!intel_engine_has_preemption(engine))
			continue;

		rq = spinner_create_request(&spin_lo, ctx_lo, engine,
					    MI_ARB_CHECK);
		if (IS_ERR(rq)) {
			err = PTR_ERR(rq);
			goto err_ctx_lo;
		}

		i915_request_add(rq);
		if (!wait_for_spinner(&spin_lo, rq)) {
			GEM_TRACE("lo spinner failed to start\n");
			GEM_TRACE_DUMP();
			i915_gem_set_wedged(i915);
			err = -EIO;
			goto err_ctx_lo;
		}

		rq = spinner_create_request(&spin_hi, ctx_hi, engine,
					    MI_ARB_CHECK);
		if (IS_ERR(rq)) {
			spinner_end(&spin_lo);
			err = PTR_ERR(rq);
			goto err_ctx_lo;
		}

		init_completion(&engine->execlists.preempt_hang.completion);
		engine->execlists.preempt_hang.inject_hang = true;

		i915_request_add(rq);

		if (!wait_for_completion_timeout(&engine->execlists.preempt_hang.completion,
						 HZ / 10)) {
			pr_err("Preemption did not occur within timeout!");
			GEM_TRACE_DUMP();
			i915_gem_set_wedged(i915);
			err = -EIO;
			goto err_ctx_lo;
		}

		set_bit(I915_RESET_ENGINE + id, &i915->gpu_error.flags);
		i915_reset_engine(engine, NULL);
		clear_bit(I915_RESET_ENGINE + id, &i915->gpu_error.flags);

		engine->execlists.preempt_hang.inject_hang = false;

		if (!wait_for_spinner(&spin_hi, rq)) {
			GEM_TRACE("hi spinner failed to start\n");
			GEM_TRACE_DUMP();
			i915_gem_set_wedged(i915);
			err = -EIO;
			goto err_ctx_lo;
		}

		spinner_end(&spin_hi);
		spinner_end(&spin_lo);
		if (igt_flush_test(i915, I915_WAIT_LOCKED)) {
			err = -EIO;
			goto err_ctx_lo;
		}
	}

	err = 0;
err_ctx_lo:
	kernel_context_close(ctx_lo);
err_ctx_hi:
	kernel_context_close(ctx_hi);
err_spin_lo:
	spinner_fini(&spin_lo);
err_spin_hi:
	spinner_fini(&spin_hi);
err_unlock:
	igt_flush_test(i915, I915_WAIT_LOCKED);
	intel_runtime_pm_put(i915);
	mutex_unlock(&i915->drm.struct_mutex);
	return err;
}

static int random_range(struct rnd_state *rnd, int min, int max)
{
	return i915_prandom_u32_max_state(max - min, rnd) + min;
}

static int random_priority(struct rnd_state *rnd)
{
	return random_range(rnd, I915_PRIORITY_MIN, I915_PRIORITY_MAX);
}

struct preempt_smoke {
	struct drm_i915_private *i915;
	struct i915_gem_context **contexts;
	struct intel_engine_cs *engine;
	unsigned int ncontext;
	struct rnd_state prng;
	unsigned long count;
};

static struct i915_gem_context *smoke_context(struct preempt_smoke *smoke)
{
	return smoke->contexts[i915_prandom_u32_max_state(smoke->ncontext,
							  &smoke->prng)];
}

static int smoke_crescendo_thread(void *arg)
{
	struct preempt_smoke *smoke = arg;
	IGT_TIMEOUT(end_time);
	unsigned long count;

	count = 0;
	do {
		struct i915_gem_context *ctx = smoke_context(smoke);
		struct i915_request *rq;

		mutex_lock(&smoke->i915->drm.struct_mutex);

		ctx->sched.priority = count % I915_PRIORITY_MAX;

		rq = i915_request_alloc(smoke->engine, ctx);
		if (IS_ERR(rq)) {
			mutex_unlock(&smoke->i915->drm.struct_mutex);
			return PTR_ERR(rq);
		}

		i915_request_add(rq);

		mutex_unlock(&smoke->i915->drm.struct_mutex);

		count++;
	} while (!__igt_timeout(end_time, NULL));

	smoke->count = count;
	return 0;
}

static int smoke_crescendo(struct preempt_smoke *smoke)
{
	struct task_struct *tsk[I915_NUM_ENGINES] = {};
	struct preempt_smoke arg[I915_NUM_ENGINES];
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	unsigned long count;
	int err = 0;

	mutex_unlock(&smoke->i915->drm.struct_mutex);

	for_each_engine(engine, smoke->i915, id) {
		arg[id] = *smoke;
		arg[id].engine = engine;
		arg[id].count = 0;

		tsk[id] = kthread_run(smoke_crescendo_thread, &arg,
				      "igt/smoke:%d", id);
		if (IS_ERR(tsk[id])) {
			err = PTR_ERR(tsk[id]);
			break;
		}
	}

	count = 0;
	for_each_engine(engine, smoke->i915, id) {
		int status;

		if (IS_ERR_OR_NULL(tsk[id]))
			continue;

		status = kthread_stop(tsk[id]);
		if (status && !err)
			err = status;

		count += arg[id].count;
	}

	mutex_lock(&smoke->i915->drm.struct_mutex);

	pr_info("Submitted %lu crescendo requests across %d engines and %d contexts\n",
		count, INTEL_INFO(smoke->i915)->num_rings, smoke->ncontext);
	return 0;
}

static int smoke_random(struct preempt_smoke *smoke)
{
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	IGT_TIMEOUT(end_time);
	unsigned long count;

	count = 0;
	do {
		for_each_engine(engine, smoke->i915, id) {
			struct i915_gem_context *ctx = smoke_context(smoke);
			struct i915_request *rq;

			ctx->sched.priority = random_priority(&smoke->prng);

			rq = i915_request_alloc(engine, ctx);
			if (IS_ERR(rq))
				return PTR_ERR(rq);

			i915_request_add(rq);
			count++;
		}
	} while (!__igt_timeout(end_time, NULL));

	pr_info("Submitted %lu random requests across %d engines and %d contexts\n",
		count, INTEL_INFO(smoke->i915)->num_rings, smoke->ncontext);
	return 0;
}

static int live_preempt_smoke(void *arg)
{
	struct preempt_smoke smoke = {
		.i915 = arg,
		.prng = I915_RND_STATE_INITIALIZER(i915_selftest.random_seed),
		.ncontext = 1024,
	};
	int err = -ENOMEM;
	int n;

	if (!HAS_LOGICAL_RING_PREEMPTION(smoke.i915))
		return 0;

	smoke.contexts = kmalloc_array(smoke.ncontext,
				       sizeof(*smoke.contexts),
				       GFP_KERNEL);
	if (!smoke.contexts)
		return -ENOMEM;

	mutex_lock(&smoke.i915->drm.struct_mutex);
	intel_runtime_pm_get(smoke.i915);

	for (n = 0; n < smoke.ncontext; n++) {
		smoke.contexts[n] = kernel_context(smoke.i915);
		if (!smoke.contexts[n])
			goto err_ctx;
	}

	err = smoke_crescendo(&smoke);
	if (err)
		goto err_ctx;

	err = smoke_random(&smoke);
	if (err)
		goto err_ctx;

err_ctx:
	if (igt_flush_test(smoke.i915, I915_WAIT_LOCKED))
		err = -EIO;

	for (n = 0; n < smoke.ncontext; n++) {
		if (!smoke.contexts[n])
			break;
		kernel_context_close(smoke.contexts[n]);
	}

	intel_runtime_pm_put(smoke.i915);
	mutex_unlock(&smoke.i915->drm.struct_mutex);
	kfree(smoke.contexts);

	return err;
}

int intel_execlists_live_selftests(struct drm_i915_private *i915)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(live_sanitycheck),
		SUBTEST(live_preempt),
		SUBTEST(live_late_preempt),
		SUBTEST(live_preempt_hang),
		SUBTEST(live_preempt_smoke),
	};

	if (!HAS_EXECLISTS(i915))
		return 0;

	if (i915_terminally_wedged(&i915->gpu_error))
		return 0;

	return i915_subtests(tests, i915);
}
