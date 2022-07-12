/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2021 Alex Richardson <arichardson@FreeBSD.org>
 *
 * This work was supported by Innovate UK project 105694, "Digital Security by
 * Design (DSbD) Technology Platform Prototype".
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include <sys/cdefs.h>
#include <sys/types.h>
#include <sys/bus.h>
#include <sys/module.h>
#include <sys/sglist.h>

/* Not strictly necessary, but explicit names make mistakes less likely. */
#define virtqueue bsd_virtqueue
#include <dev/virtio/virtqueue.h>
#include <dev/virtio/virtio.h>

#define __DO_NOT_DEFINE_LINUX_VIRTIO_NAMES
#include <linux/scatterlist.h>
#include <linux/virtio.h>
#include <linux/virtio_config.h>

static inline struct bsd_virtqueue *
linux_to_bsd_virtqueue(struct linux_virtqueue *vq)
{
    return vq->bsdq;
}

int
linux_virtqueue_add_sgs(struct linux_virtqueue *vq, struct scatterlist *sgs[],
                        unsigned int out_sgs, unsigned int in_sgs, void *data, gfp_t gfp)
{
    struct sglist *native_sg;
    size_t total_sgs = out_sgs + in_sgs;
    int err;

    /* convert to a native sglist and then call virtqueue_enqueue(). */
    native_sg = sglist_alloc(total_sgs, gfp);
    if (!native_sg)
        return (-ENOMEM);
    for (size_t i = 0; i < total_sgs; i++) {
        struct scatterlist *sg = sgs[i];
        KASSERT(sg_is_last(sg), ("Unexpected chain in sglist %p", sg));
        err = sglist_append(native_sg, sg_virt(sg), sg->length);
        if (err)
            return -err;
    }
    err = virtqueue_enqueue(linux_to_bsd_virtqueue(vq), data, native_sg,
                            out_sgs, in_sgs);
    return (err == 0 ? 0 : -err);
}

void
linux_virtqueue_disable_cb(struct linux_virtqueue *vq)
{
    virtqueue_disable_intr(linux_to_bsd_virtqueue(vq));
}

bool
linux_virtqueue_enable_cb(struct linux_virtqueue *vq)
{
    /* FIXME: is this the correct return value? */
    return (!virtqueue_enable_intr(linux_to_bsd_virtqueue(vq)));
}
void *
linux_virtqueue_get_buf(struct linux_virtqueue *vq, unsigned int *len)
{
    return (virtqueue_poll(linux_to_bsd_virtqueue(vq), len));
}

unsigned
linux_virtqueue_num_free(struct linux_virtqueue *vq)
{
    return (virtqueue_nfree(linux_to_bsd_virtqueue(vq)));
}

/*
 * In Linux virtqueue_notify() is the lower-level API and virtqueue_kick()
 * maps to FreeBSD virtqueue_notify().
 */
bool
linux_virtqueue_kick(struct linux_virtqueue *vq)
{
    return (virtqueue_notify(linux_to_bsd_virtqueue(vq)));
}

bool
linux_virtqueue_kick_prepare(struct linux_virtqueue *vq)
{
    return (vq_ring_must_notify_host(linux_to_bsd_virtqueue(vq)));
}

bool linux_virtqueue_notify(struct linux_virtqueue *vq) {
    return (vq_ring_notify_host(linux_to_bsd_virtqueue(vq)));
}

bool
linux_virtio_has_feature(const struct linux_virtio_device *vdev,
                         unsigned int fbit)
{
    return (virtio_with_feature(vdev->bsddev, UINT64_C(1) << fbit));
}

bool
linux_virtio_has_iommu_quirk(const struct linux_virtio_device *vdev)
{
    return (!virtio_with_feature(vdev->bsddev, VIRTIO_F_IOMMU_PLATFORM));
}

/* Inlined device specific read/write functions for common lengths. */
#define VIRTIO_RDWR_DEVICE_CONFIG(size, type)                            \
	type linux_virtio_cread##size(struct linux_virtio_device *vdev,  \
	    unsigned int offset)                                         \
	{                                                                \
		type val;                                                \
		virtio_read_device_config(vdev->bsddev, offset, &val,    \
		    sizeof(type));                                       \
		return (val);                                            \
	}                                                                \
                                                                         \
	void linux_virtio_cwrite##size(struct linux_virtio_device *vdev, \
	    unsigned int offset, type val)                               \
	{                                                                \
		virtio_write_device_config(vdev->bsddev, offset, &val,   \
		    sizeof(type));                                       \
	}

VIRTIO_RDWR_DEVICE_CONFIG(8, uint8_t)
VIRTIO_RDWR_DEVICE_CONFIG(16, uint16_t)
VIRTIO_RDWR_DEVICE_CONFIG(32, uint32_t)
VIRTIO_RDWR_DEVICE_CONFIG(64, uint64_t)
#undef VIRTIO_RDWR_DEVICE_CONFIG

void
linux_virtio_cread_bytes(struct linux_virtio_device *vdev, unsigned offset,
                         void *buf, unsigned len)
{
    virtio_read_device_config(vdev->bsddev, offset, buf, len);
}

void
linux_virtio_cwrite_bytes(struct linux_virtio_device *vdev, unsigned offset,
                          const void *buf, unsigned len)
{
    virtio_write_device_config(vdev->bsddev, offset, buf, len);
}

int
linux_virtio_find_vqs(struct linux_virtio_device *vdev, unsigned nvqs,
                      struct linux_virtqueue *vqs[], vq_callback_t *callbacks[],
                      const char *const names[], struct irq_affinity *desc)
{
    struct vq_alloc_info *info;
    int error;

    info = malloc(sizeof(struct vq_alloc_info) * nvqs, M_TEMP, M_NOWAIT);
    if (info == NULL)
        return (-ENOMEM);

    for (unsigned i = 0; i < nvqs; i++) {
        VQ_ALLOC_INFO_INIT(&info[i], 0, (virtqueue_intr_t*)callbacks[i],
                           vqs[i], &vqs[i]->bsdq, "%s", names[i]);
    }

    error = virtio_alloc_virtqueues(vdev->bsddev, 0, nvqs, info);
    free(info, M_TEMP);
    return (error == 0 ? 0 : -error);
}

void linux_virtio_device_ready(struct linux_virtio_device *dev)
{
    virtio_reinit_complete(dev->bsddev);
}

void linux_virtio_reset(struct linux_virtio_device *dev)
{
    panic("%s not implemented!", __func__);
}

void linux_virtio_del_vqs(struct linux_virtio_device *dev)
{
    panic("%s not implemented!", __func__);
}

MODULE_DEPEND(linuxkpi, virtio, 1, 1, 1);