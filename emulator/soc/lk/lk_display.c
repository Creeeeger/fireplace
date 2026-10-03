
#include <stdlib.h>

#include <turbojpeg.h>

#include "bootchain/bootchain_internal.h"
#include "lk/lk_internal.h"

#include <fireplace/soc/fb/fb.h>

#define LK_JPEG_DRAW_ADDR UINT64_C(0xe8032528)
#define LK_JPEG_MAX_SIZE (4U * 1024U * 1024U)

static bool lk_read_reg(uc_engine *uc, int reg, uint64_t *value)
{
	return uc_reg_read(uc, reg, value) == UC_ERR_OK;
}

static bool lk_jpeg_position(uint64_t requested_x, uint64_t requested_y,
			     uint64_t screen_width, uint64_t screen_height,
			     bool align, int image_width, int image_height,
			     int *draw_x, int *draw_y)
{
	int64_t x;
	int64_t y;

	if (screen_width == 0 || screen_height == 0 ||
	    screen_width > FB_WIDTH || screen_height > FB_HEIGHT)
		return false;

	if (!align) {
		x = (int64_t)requested_x;
		y = (int64_t)requested_y;
	} else {
		x = requested_x == 0 ?
			((int64_t)screen_width - image_width) / 2 :
			(int64_t)requested_x - image_width;
		y = requested_y == 0 ?
			((int64_t)screen_height - image_height) / 2 :
			(int64_t)requested_y - image_height;
	}

	if (x < 0 || y < 0 || x + image_width > FB_WIDTH ||
	    y + image_height > FB_HEIGHT)
		return false;

	*draw_x = (int)x;
	*draw_y = (int)y;
	return true;
}

static bool lk_draw_jpeg_on_host(uc_engine *uc)
{
	uint64_t requested_x;
	uint64_t requested_y;
	uint64_t screen_width;
	uint64_t screen_height;
	uint64_t align;
	uint64_t jpeg_address;
	uint64_t jpeg_size;
	unsigned char *jpeg = NULL;
	tjhandle decoder = NULL;
	int image_width = 0;
	int image_height = 0;
	int jpeg_subsampling;
	int jpeg_colorspace;
	int draw_x;
	int draw_y;
	bool drawn = false;

	if (!lk_read_reg(uc, UC_ARM64_REG_X0, &requested_x) ||
	    !lk_read_reg(uc, UC_ARM64_REG_X1, &requested_y) ||
	    !lk_read_reg(uc, UC_ARM64_REG_X2, &screen_width) ||
	    !lk_read_reg(uc, UC_ARM64_REG_X3, &screen_height) ||
	    !lk_read_reg(uc, UC_ARM64_REG_X4, &align) ||
	    !lk_read_reg(uc, UC_ARM64_REG_X5, &jpeg_address) ||
	    !lk_read_reg(uc, UC_ARM64_REG_X6, &jpeg_size) ||
	    jpeg_size == 0 || jpeg_size > LK_JPEG_MAX_SIZE)
		return false;

	jpeg = malloc((size_t)jpeg_size);
	if (!jpeg || uc_mem_read(uc, jpeg_address, jpeg,
				(size_t)jpeg_size) != UC_ERR_OK)
		goto out;

	/* This API is available in both libjpeg-turbo 2.x and 3.x. */
	decoder = tjInitDecompress();
	if (!decoder || tjDecompressHeader3(decoder, jpeg,
					   (unsigned long)jpeg_size,
					   &image_width, &image_height,
					   &jpeg_subsampling, &jpeg_colorspace) != 0)
		goto out;

	if (image_width <= 0 || image_height <= 0 ||
	    !lk_jpeg_position(requested_x, requested_y, screen_width,
			      screen_height, align != 0, image_width,
			      image_height, &draw_x, &draw_y))
		goto out;

	drawn = tjDecompress2(
		decoder, jpeg, (unsigned long)jpeg_size,
		framebuffer + ((size_t)draw_y * FB_WIDTH + draw_x) * FB_BPP,
		image_width, FB_WIDTH * FB_BPP, image_height, TJPF_BGRA,
		TJFLAG_FASTUPSAMPLE | TJFLAG_FASTDCT) == 0;
out:
	if (decoder)
		tjDestroy(decoder);
	free(jpeg);
	return drawn;
}

static void lk_jpeg_draw_cb(uc_engine *uc, uint64_t address, uint32_t size,
			    void *user_data)
{
	(void)address;
	(void)size;
	(void)user_data;

	if (bootchain_stage() == BOOTCHAIN_STAGE_LK &&
	    lk_draw_jpeg_on_host(uc))
		(void)bootchain_return_to_link(uc);
}

uc_err lk_display_init(uc_engine *uc)
{
	const struct bootchain_hook hook =
		BOOTCHAIN_CODE_HOOK(lk_jpeg_draw_cb, LK_JPEG_DRAW_ADDR);

	return bootchain_install_hooks(uc, &hook, 1);
}
