/* Public domain. */

#include <drm/drm_gem.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_fourcc.h>

MALLOC_DECLARE(DRM_MEM_KMS);

void
drm_gem_fb_destroy(struct drm_framebuffer *fb)
{
	int i;

	for (i = 0; i < 4; i++)
		drm_gem_object_put(fb->obj[i]);
	drm_framebuffer_cleanup(fb);
	free(fb, DRM_MEM_KMS);
}

int
drm_gem_fb_create_handle(struct drm_framebuffer *fb, struct drm_file *file,
    unsigned int *handle)
{
	return drm_gem_handle_create(file, fb->obj[0], handle);
}

static int
drm_gem_fb_init(struct drm_device *dev,
                struct drm_framebuffer *fb,
                const struct drm_mode_fb_cmd2 *mode_cmd,
                struct drm_gem_object **obj, unsigned int num_planes,
                const struct drm_framebuffer_funcs *funcs)
{
    int ret, i;

    drm_helper_mode_fill_fb_struct(dev, fb, mode_cmd);

    for (i = 0; i < num_planes; i++)
        fb->obj[i] = obj[i];

    ret = drm_framebuffer_init(dev, fb, funcs);
    if (ret)
        drm_err(dev, "Failed to init framebuffer: %d\n", ret);

    return ret;
}


/**
 * drm_gem_fb_init_with_funcs() - Helper function for implementing
 *				  &drm_mode_config_funcs.fb_create
 *				  callback in cases when the driver
 *				  allocates a subclass of
 *				  struct drm_framebuffer
 * @dev: DRM device
 * @fb: framebuffer object
 * @file: DRM file that holds the GEM handle(s) backing the framebuffer
 * @mode_cmd: Metadata from the userspace framebuffer creation request
 * @funcs: vtable to be used for the new framebuffer object
 *
 * This function can be used to set &drm_framebuffer_funcs for drivers that need
 * custom framebuffer callbacks. Use drm_gem_fb_create() if you don't need to
 * change &drm_framebuffer_funcs. The function does buffer size validation.
 * The buffer size validation is for a general case, though, so users should
 * pay attention to the checks being appropriate for them or, at least,
 * non-conflicting.
 *
 * Returns:
 * Zero or a negative error code.
 */
int drm_gem_fb_init_with_funcs(struct drm_device *dev,
                               struct drm_framebuffer *fb,
                               struct drm_file *file,
                               const struct drm_mode_fb_cmd2 *mode_cmd,
                               const struct drm_framebuffer_funcs *funcs)
{
    const struct drm_format_info *info;
    struct drm_gem_object *objs[4];
    int ret, i;

    info = drm_get_format_info(dev, mode_cmd);


    for (i = 0; i < info->num_planes; i++) {
        unsigned int width = mode_cmd->width / (i ? info->hsub : 1);
        unsigned int height = mode_cmd->height / (i ? info->vsub : 1);
        unsigned int min_size;

        objs[i] = drm_gem_object_lookup(file, mode_cmd->handles[i]);
        if (!objs[i]) {
            drm_dbg_kms(dev, "Failed to lookup GEM object\n");
            ret = -ENOENT;
            goto err_gem_object_put;
        }

        min_size = (height - 1) * mode_cmd->pitches[i]
                   + drm_format_info_min_pitch(info, i, width)
                   + mode_cmd->offsets[i];

        if (objs[i]->size < min_size) {
            drm_gem_object_put(objs[i]);
            ret = -EINVAL;
            goto err_gem_object_put;
        }
    }

    ret = drm_gem_fb_init(dev, fb, mode_cmd, objs, i, funcs);
    if (ret)
        goto err_gem_object_put;

    return 0;

    err_gem_object_put:
    for (i--; i >= 0; i--)
        drm_gem_object_put(objs[i]);

    return ret;
}
EXPORT_SYMBOL_GPL(drm_gem_fb_init_with_funcs);

/**
 * drm_gem_fb_create_with_funcs() - Helper function for the
 *                                  &drm_mode_config_funcs.fb_create
 *                                  callback
 * @dev: DRM device
 * @file: DRM file that holds the GEM handle(s) backing the framebuffer
 * @mode_cmd: Metadata from the userspace framebuffer creation request
 * @funcs: vtable to be used for the new framebuffer object
 *
 * This function can be used to set &drm_framebuffer_funcs for drivers that need
 * custom framebuffer callbacks. Use drm_gem_fb_create() if you don't need to
 * change &drm_framebuffer_funcs. The function does buffer size validation.
 *
 * Returns:
 * Pointer to a &drm_framebuffer on success or an error pointer on failure.
 */
struct drm_framebuffer *
drm_gem_fb_create_with_funcs(struct drm_device *dev, struct drm_file *file,
                             const struct drm_mode_fb_cmd2 *mode_cmd,
                             const struct drm_framebuffer_funcs *funcs)
{
    struct drm_framebuffer *fb;
    int ret;

    fb = kzalloc(sizeof(*fb), GFP_KERNEL);
    if (!fb)
        return ERR_PTR(-ENOMEM);

    ret = drm_gem_fb_init_with_funcs(dev, fb, file, mode_cmd, funcs);
    if (ret) {
        kfree(fb);
        return ERR_PTR(ret);
    }

    return fb;
}
