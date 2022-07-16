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
#ifndef __LKPI_LINUX_VIRTIO_H
#define __LKPI_LINUX_VIRTIO_H

#include <linux/types.h>

#include <linux/device.h>
#include <linux/gfp.h>
#include <linux/mod_devicetable.h>
#include <linux/scatterlist.h>
#include <linux/spinlock.h>

struct linux_virtio_device;

struct bsd_virtqueue;

struct linux_virtqueue {
    struct linux_virtio_device *vdev;
    struct bsd_virtqueue *bsdq;
    unsigned int num_free;
    void *priv;
};

struct linux_virtio_device {
    struct device dev;
    device_t bsddev;
    void *priv;
};

static inline struct linux_virtio_device *
dev_to_virtio(struct device *d)
{
    return container_of(d, struct linux_virtio_device, dev);
}

/*
 * The Linux and FreeBSD virtio function names partially overlap. To perform the
 * API translation we have to namespace the functions and types.
 */
#ifndef __DO_NOT_DEFINE_LINUX_VIRTIO_NAMES
#define virtqueue linux_virtqueue
#define virtio_device linux_virtio_device
/* Rename the symbol to the linuxkpi name using asm() */
#define VIRTIO_NAMESPACED_FN(ret, name, args) \
	ret name args __asm__(__XSTRING(__CONCAT(linux_, name)))
#else
#define VIRTIO_NAMESPACED_FN(ret, name, args) ret __CONCAT(linux_, name) args
#endif

VIRTIO_NAMESPACED_FN(int, virtqueue_add_sgs,
                     (struct linux_virtqueue * vq, struct scatterlist *sgs[],
                             unsigned int out_sgs, unsigned int in_sgs, void *data, gfp_t gfp));
VIRTIO_NAMESPACED_FN(void, virtqueue_disable_cb, (struct linux_virtqueue * vq));
VIRTIO_NAMESPACED_FN(bool, virtqueue_enable_cb, (struct linux_virtqueue * vq));
VIRTIO_NAMESPACED_FN(void *, virtqueue_get_buf,
                     (struct linux_virtqueue * vq, unsigned int *len));
VIRTIO_NAMESPACED_FN(bool, virtqueue_kick, (struct linux_virtqueue * vq));
VIRTIO_NAMESPACED_FN(bool, virtqueue_kick_prepare,
                     (struct linux_virtqueue * vq));
VIRTIO_NAMESPACED_FN(bool, virtqueue_notify, (struct linux_virtqueue * vq));
VIRTIO_NAMESPACED_FN(unsigned, virtqueue_num_free, (struct linux_virtqueue * vq));

#endif /* __LKPI_LINUX_VIRTIO_H */