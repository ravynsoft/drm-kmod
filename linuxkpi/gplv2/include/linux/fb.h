#ifndef __LINUX_FB_H_
#define __LINUX_FB_H_
#include <sys/fbio.h>
#include <uapi/linux/fb.h>

#include <linux/fs.h>
#include <linux/init.h>
#include <linux/workqueue.h>
#include <linux/list.h>
#include <linux/pci.h>
#include <linux/backlight.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <asm/io.h>
#include <linux/notifier.h>

struct linux_fb_info;
struct videomode;
struct vm_area_struct;

#define KHZ2PICOS(a) (1000000000UL/(a))
#define FB_TYPE_PACKED_PIXELS		0	/* Packed Pixels	*/
#define FB_VISUAL_TRUECOLOR		2	/* True color	*/
#define FB_ACCEL_NONE		0	/* no hardware accelerator	*/
#define FB_ACTIVATE_NOW		0	/* set values immediately (or vbl)*/

struct fb_fix_screeninfo {
    char id[16];			/* identification string eg "TT Builtin" */
    unsigned long smem_start;	/* Start of frame buffer mem */
    /* (physical address) */
    uint32_t smem_len;			/* Length of frame buffer mem */
    uint32_t type;			/* see FB_TYPE_*		*/
    uint32_t type_aux;			/* Interleave for interleaved Planes */
    uint32_t visual;			/* see FB_VISUAL_*		*/
    __u16 xpanstep;			/* zero if no hardware panning  */
    __u16 ypanstep;			/* zero if no hardware panning  */
    __u16 ywrapstep;		/* zero if no hardware ywrap    */
    uint32_t line_length;		/* length of a line in bytes    */
    unsigned long mmio_start;	/* Start of Memory Mapped I/O   */
    /* (physical address) */
    uint32_t mmio_len;			/* Length of Memory Mapped I/O  */
    uint32_t accel;			/* Indicate to driver which	*/
    /*  specific chip/card we have	*/
    __u16 capabilities;		/* see FB_CAP_*			*/
    __u16 reserved[2];		/* Reserved for future compatibility */
};

struct fb_bitfield {
    uint32_t offset;			/* beginning of bitfield	*/
    uint32_t length;			/* length of bitfield		*/
    uint32_t msb_right;		/* != 0 : Most significant bit is */
    /* right */
};

struct fb_var_screeninfo {


    uint32_t xres;			/* visible resolution		*/
    uint32_t yres;
    uint32_t xres_virtual;		/* virtual resolution		*/
    uint32_t yres_virtual;
    uint32_t xoffset;			/* offset from virtual to visible */
    uint32_t yoffset;			/* resolution			*/

    uint32_t bits_per_pixel;		/* guess what			*/
    uint32_t grayscale;		/* 0 = color, 1 = grayscale,	*/
    /* >1 = FOURCC			*/
    struct fb_bitfield red;		/* bitfield in fb mem if true color, */
    struct fb_bitfield green;	/* else only length is significant */
    struct fb_bitfield blue;
    struct fb_bitfield transp;	/* transparency			*/

    uint32_t nonstd;			/* != 0 Non standard pixel format */

    uint32_t activate;			/* see FB_ACTIVATE_*		*/

    uint32_t height;			/* height of picture in mm    */
    uint32_t width;			/* width of picture in mm     */

    uint32_t accel_flags;		/* (OBSOLETE) see fb_info.flags */

    /* Timing: All values in pixclocks, except pixclock (of course) */
    uint32_t pixclock;			/* pixel clock in ps (pico seconds) */
    uint32_t left_margin;		/* time from sync to picture	*/
    uint32_t right_margin;		/* time from picture to sync	*/
    uint32_t upper_margin;		/* time from sync to picture	*/
    uint32_t lower_margin;
    uint32_t hsync_len;		/* length of horizontal sync	*/
    uint32_t vsync_len;		/* length of vertical sync	*/
    uint32_t sync;			/* see FB_SYNC_*		*/
    uint32_t vmode;			/* see FB_VMODE_*		*/
    uint32_t rotate;			/* angle we rotate counter clockwise */
    uint32_t colorspace;		/* colorspace for FOURCC-based modes */
    uint32_t reserved[4];		/* Reserved for future compatibility */
};

struct fb_copyarea {
    uint32_t dx;
    uint32_t dy;
    uint32_t width;
    uint32_t height;
    uint32_t sx;
    uint32_t sy;
};

struct fb_fillrect {
    uint32_t dx;	/* screen-relative */
    uint32_t dy;
    uint32_t width;
    uint32_t height;
    uint32_t color;
    uint32_t rop;
};

struct fb_cmap {
    uint32_t start;			/* First entry	*/
    uint32_t len;			/* Number of entries */
    __u16 *red;			/* Red values	*/
    __u16 *green;
    __u16 *blue;
    __u16 *transp;			/* transparency, can be NULL */
};

struct fb_image {
    uint32_t dx;		/* Where to place image */
    uint32_t dy;
    uint32_t width;		/* Size of image */
    uint32_t height;
    uint32_t fg_color;		/* Only used when a mono bitmap */
    uint32_t bg_color;
    __u8  depth;		/* Depth of the image */
    const char *data;	/* Pointer to image data */
    struct fb_cmap cmap;	/* color map info */
};

struct lfbcurpos {
    __u16 x, y;
};

struct fb_cursor {
    __u16 set;		/* what to set */
    __u16 enable;		/* cursor on/off */
    __u16 rop;		/* bitop operation */
    const char *mask;	/* cursor mask bits */
    struct lfbcurpos hot;	/* cursor hot spot */
    struct fb_image	image;	/* Cursor image */
};

struct fb_blit_caps {
    u32 x;
    u32 y;
    u32 len;
    u32 flags;
};

struct fb_ops {
    /* open/release and usage marking */
    struct module *owner;
    int (*fb_open)(struct linux_fb_info *info, int user);
    int (*fb_release)(struct linux_fb_info *info, int user);

    /* For framebuffers with strange non linear layouts or that do not
     * work with normal memory mapped access
     */
    ssize_t (*fb_read)(struct linux_fb_info *info, char __user *buf,
    size_t count, loff_t *ppos);
    ssize_t (*fb_write)(struct linux_fb_info *info, const char __user *buf,
    size_t count, loff_t *ppos);

    /* checks var and eventually tweaks it to something supported,
     * DO NOT MODIFY PAR */
    int (*fb_check_var)(struct fb_var_screeninfo *var, struct linux_fb_info *info);

    /* set the video mode according to info->var */
    int (*fb_set_par)(struct linux_fb_info *info);

    /* set color register */
    int (*fb_setcolreg)(unsigned regno, unsigned red, unsigned green,
                        unsigned blue, unsigned transp, struct linux_fb_info *info);

    /* set color registers in batch */
    int (*fb_setcmap)(struct fb_cmap *cmap, struct linux_fb_info *info);

    /* blank display */
    int (*fb_blank)(int blank, struct linux_fb_info *info);

    /* pan display */
    int (*fb_pan_display)(struct fb_var_screeninfo *var, struct linux_fb_info *info);

    /* Draws a rectangle */
    void (*fb_fillrect) (struct linux_fb_info *info, const struct fb_fillrect *rect);
    /* Copy data from area to another */
    void (*fb_copyarea) (struct linux_fb_info *info, const struct fb_copyarea *region);
    /* Draws a image to the display */
    void (*fb_imageblit) (struct linux_fb_info *info, const struct fb_image *image);

    /* Draws cursor */
    int (*fb_cursor) (struct linux_fb_info *info, struct fb_cursor *cursor);

    /* Rotates the display */
    void (*fb_rotate)(struct linux_fb_info *info, int angle);

    /* wait for blit idle, optional */
    int (*fb_sync)(struct linux_fb_info *info);

    /* perform fb specific ioctl (optional) */
    int (*fb_ioctl)(struct linux_fb_info *info, unsigned int cmd,
                    unsigned long arg);

    /* Handle 32bit compat ioctl (optional) */
    int (*fb_compat_ioctl)(struct linux_fb_info *info, unsigned cmd,
                           unsigned long arg);

    /* perform fb specific mmap */
    int (*fb_mmap)(struct linux_fb_info *info, struct vm_area_struct *vma);

    /* get capability given var */
    void (*fb_get_caps)(struct linux_fb_info *info, struct fb_blit_caps *caps,
                        struct fb_var_screeninfo *var);

    /* teardown any resources to do with this framebuffer */
    void (*fb_destroy)(struct linux_fb_info *info);

    /* called at KDB enter and leave time to prepare the console */
    int (*fb_debug_enter)(struct linux_fb_info *info);
    int (*fb_debug_leave)(struct linux_fb_info *info);
};

struct linux_fb_info {
	struct fb_var_screeninfo var;	/* Current var */
	struct fb_fix_screeninfo fix;	/* Current fix */

	const struct fb_ops *fbops;
	struct device *device;		/* This is the parent */
	struct device *dev;		/* This is this fb device */
	union {
		char __iomem *screen_base;	/* Virtual address */
		char *screen_buffer;
	};
	unsigned long screen_size;	/* Amount of ioremapped VRAM or 0 */ 
	void *pseudo_palette;		/* Fake palette of 16 colors */ 
#define FBINFO_STATE_RUNNING	0
#define FBINFO_STATE_SUSPENDED	1
	u32 state;			/* Hardware state i.e suspend */
	/* From here on everything is device dependent */
	void *par;
	/* we need the PCI or similar aperture base/size not
	   smem_start/size as smem_start may just be an object
	   allocated inside the aperture so may not actually overlap */
	struct apertures_struct {
		unsigned int count;
		struct aperture {
			resource_size_t base;
			resource_size_t size;
		} ranges[0];
	} *apertures;

	struct fb_info fbio;
	device_t fb_bsddev;
} __aligned(sizeof(long));

static inline struct apertures_struct *alloc_apertures(unsigned int max_num) {
	struct apertures_struct *a = kzalloc(sizeof(struct apertures_struct)
			+ max_num * sizeof(struct aperture), GFP_KERNEL);
	if (!a)
		return NULL;
	a->count = max_num;
	return a;
}

extern void cfb_fillrect(struct linux_fb_info *info, const struct fb_fillrect *rect);
extern void cfb_copyarea(struct linux_fb_info *info, const struct fb_copyarea *area);
extern void cfb_imageblit(struct linux_fb_info *info, const struct fb_image *image);

int linux_register_framebuffer(struct linux_fb_info *fb_info);
int linux_unregister_framebuffer(struct linux_fb_info *fb_info);
int remove_conflicting_framebuffers(struct apertures_struct *a,
	const char *name, bool primary);
int remove_conflicting_pci_framebuffers(struct pci_dev *pdev, const char *name);
struct linux_fb_info *framebuffer_alloc(size_t size, struct device *dev);
void framebuffer_release(struct linux_fb_info *info);
#define	fb_set_suspend(x, y)	0

/* updated FreeBSD fb_info */
int linux_fb_get_options(const char *name, char **option);
#define	fb_get_options	linux_fb_get_options

/*
 * GPL licensed routines that need to be replaced:
 */


extern void tainted_cfb_fillrect(struct linux_fb_info *p, const struct fb_fillrect *rect);
extern void tainted_cfb_copyarea(struct linux_fb_info *p, const struct fb_copyarea *area);
extern void tainted_cfb_imageblit(struct linux_fb_info *p, const struct fb_image *image);



void vt_unfreeze_main_vd(void);

#endif /* __LINUX_FB_H_ */

