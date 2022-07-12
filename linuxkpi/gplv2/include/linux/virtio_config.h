/*-
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
#ifndef	__LKPI_LINUX_VIRTIO_CONFIG_H
#define	__LKPI_LINUX_VIRTIO_CONFIG_H

#include <linux/virtio.h>

#ifndef __DO_NOT_DEFINE_LINUX_VIRTIO_NAMES
/* Virtio-gpu needs this defined in the Linux way (bit index not mask). */
#define VIRTIO_F_VERSION_1		32
#endif


typedef void(vq_callback_t)(struct linux_virtqueue *);

VIRTIO_NAMESPACED_FN(bool, virtio_has_feature,
        (const struct linux_virtio_device *vdev, unsigned int fbit));
VIRTIO_NAMESPACED_FN(bool, virtio_has_iommu_quirk,
        (const struct linux_virtio_device *vdev));

VIRTIO_NAMESPACED_FN(uint8_t, virtio_cread8,
(struct linux_virtio_device * vdev, unsigned int offset));
VIRTIO_NAMESPACED_FN(uint16_t, virtio_cread16,
(struct linux_virtio_device * vdev, unsigned int offset));
VIRTIO_NAMESPACED_FN(uint32_t, virtio_cread32,
(struct linux_virtio_device * vdev, unsigned int offset));
VIRTIO_NAMESPACED_FN(uint64_t, virtio_cread64,
(struct linux_virtio_device * vdev, unsigned int offset));
VIRTIO_NAMESPACED_FN(void, virtio_cwrite8,
        (struct linux_virtio_device * vdev, unsigned int offset, uint8_t val));
VIRTIO_NAMESPACED_FN(void, virtio_cwrite16,
        (struct linux_virtio_device * vdev, unsigned int offset, uint16_t val));
VIRTIO_NAMESPACED_FN(void, virtio_cwrite32,
        (struct linux_virtio_device * vdev, unsigned int offset, uint32_t val));
VIRTIO_NAMESPACED_FN(void, virtio_cwrite64,
        (struct linux_virtio_device * vdev, unsigned int offset, uint64_t val));

/*
 * virtio_cwrite_bytes() doesn't seem to exist in Linux, but it makes it a lot
 * easier to implement virtio_cwrite().
 */
VIRTIO_NAMESPACED_FN(void, virtio_cread_bytes,
        (struct linux_virtio_device * vdev, unsigned offset, void *buf,
         unsigned len));
VIRTIO_NAMESPACED_FN(void, virtio_cwrite_bytes,
        (struct linux_virtio_device * vdev, unsigned offset, const void *buf,
         unsigned len));

struct irq_affinity;
VIRTIO_NAMESPACED_FN(int, virtio_find_vqs,
        (struct linux_virtio_device * vdev, unsigned nvqs, struct linux_virtqueue *vqs[],
         vq_callback_t *callbacks[], const char *const names[],
         struct irq_affinity *desc));
VIRTIO_NAMESPACED_FN(void, virtio_del_vqs, (struct linux_virtio_device *vdev));
VIRTIO_NAMESPACED_FN(void, virtio_reset, (struct linux_virtio_device *vdev));
VIRTIO_NAMESPACED_FN(void, virtio_device_ready, (struct linux_virtio_device *dev));

/* Simple implementation of virtio_cread/virtio_cwrite without type checks. */
#define virtio_cread(vdev, structname, member, ptr)                 \
	virtio_cread_bytes(vdev, offsetof(structname, member), ptr, \
	    sizeof(*ptr))
#define virtio_cwrite(vdev, structname, member, ptr)                 \
	virtio_cwrite_bytes(vdev, offsetof(structname, member), ptr, \
	    sizeof(*ptr))

#endif /* __LKPI_LINUX_VIRTIO_CONFIG_H */
