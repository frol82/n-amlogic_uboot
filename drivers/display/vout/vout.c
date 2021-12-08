/*
 * drivers/display/vout/vout.c
 *
 * Copyright (C) 2015 Amlogic, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * Author: Platform-BJ @platform.bj@amlogic.com
 *
*/


#include <common.h>
#include <asm/arch/io.h>
#include <asm/arch/secure_apb.h>
#include <amlogic/vmode.h>
#include <amlogic/vout.h>
#ifdef CONFIG_AML_LCD
#include <amlogic/aml_lcd.h>
#endif

#define VOUT_LOG_DBG 0
#define VOUT_LOG_TAG "[VOUT]"
#define vout_log(fmt, ...) printf(VOUT_LOG_TAG fmt, ##__VA_ARGS__)
#define vout_logl() \
	do { \
		if (VOUT_LOG_DBG > 0) \
			vout_log("%s:%d\n", __func__, __LINE__); \
	} while (0)

#define REG_OFFSET_VCBUS(reg)           ((reg << 2))
#define REG_ADDR_VCBUS(reg)             (REG_BASE_VCBUS + REG_OFFSET_VCBUS(reg))

static int g_vmode = -1;

typedef struct vout_set_s {
	char *name;
	int mode;
	ulong width;
	ulong height;
} vout_set_t;


static const vout_set_t vout_sets[] = {
	{ /* VMODE_480I */
		.name              = "480i",
		.mode              = VMODE_480I,
		.width             = 720,
		.height            = 480,
	},
	{ /* VMODE_480CVBS*/
		.name              = "480cvbs",
		.mode              = VMODE_480CVBS,
		.width             = 720,
		.height            = 480,
	},
	{ /* VMODE_480P */
		.name              = "480p",
		.mode              = VMODE_480P,
		.width             = 720,
		.height            = 480,
	},
	{ /* VMODE_576I */
		.name              = "576i",
		.mode              = VMODE_576I,
		.width             = 720,
		.height            = 576,
	},
	{ /* VMODE_576I */
		.name              = "576cvbs",
		.mode              = VMODE_576CVBS,
		.width             = 720,
		.height            = 576,
	},
	{ /* VMODE_576P */
		.name              = "576p",
		.mode              = VMODE_576P,
		.width             = 720,
		.height            = 576,
	},
	{ /* VMODE_720P */
		.name              = "720p",
		.mode              = VMODE_720P,
		.width             = 1280,
		.height            = 720,
	},
	{ /* VMODE_768P */
		.name              = "768p",
		.mode              = VMODE_768P,
		.width             = 1366,
		.height            = 768,
	},
	{ /* VMODE_1080I */
		.name              = "1080i",
		.mode              = VMODE_1080I,
		.width             = 1920,
		.height            = 1080,
	},
	{ /* VMODE_1080P */
		.name              = "1080p",
		.mode              = VMODE_1080P,
		.width             = 1920,
		.height            = 1080,
	},
	{ /* VMODE_4K2K_60HZ */
		.name              = "2160p",
		.mode              = VMODE_4K2K_60HZ,
		.width             = 3840,
		.height            = 2160,
	},
	{ /* VMODE_4K2K_SMPTE */
		.name              = "smpte",
		.mode              = VMODE_4K2K_SMPTE,
		.width             = 4096,
		.height            = 2160,
	},
	{ /* VMODE_vga */
		.name              = "vga",
		.mode              = VMODE_VGA,
		.width             = 640,
		.height            = 480,
	},
	{ /* VMODE_SVGA */
		.name              = "svga",
		.mode              = VMODE_SVGA,
		.width             = 800,
		.height            = 600,
	},
	{ /* VMODE_XGA */
		.name              = "xga",
		.mode              = VMODE_XGA,
		.width             = 1024,
		.height            = 768,
	},
	{ /* VMODE_sxga */
		.name              = "sxga",
		.mode              = VMODE_SXGA,
		.width             = 1280,
		.height            = 1024,
	},
	{ /* VMODE_wsxga */
		.name              = "wsxga",
		.mode              = VMODE_WSXGA,
		.width             = 1440,
		.height            = 900,
	},
	{ /* VMODE_fhdvga */
		.name              = "fhdvga",
		.mode              = VMODE_FHDVGA,
		.width             = 1920,
		.height            = 1080,
	},
	{ /* VMODE_LCD */
		.name              = "panel",
		.mode              = VMODE_LCD,
		.width             = 1280,
		.height            = 720,
	},
	{ /* VMODE_640x480p60hz */
		.name              = "640x480p60hz",
		.mode              = VMODE_640x480p60hz,
		.width             = 640,
		.height            = 480,
	},
	{ /* VMODE_800x480p60hz */
		.name              = "800x480p60hz",
		.mode              = VMODE_800x480p60hz,
		.width             = 800,
		.height            = 480,
	},
	{ /* VMODE_800x600p60hz */
		.name              = "800x600p60hz",
		.mode              = VMODE_800x600p60hz,
		.width             = 800,
		.height            = 600,
	},
	{ /* VMODE_852x480p60hz */
		.name              = "852x480p60hz",
		.mode              = VMODE_852x480p60hz,
		.width             = 852,
		.height            = 480,
	},
	{ /* VMODE_854x480p60hz */
		.name              = "854x480p60hz",
		.mode              = VMODE_854x480p60hz,
		.width             = 854,
		.height            = 480,
	},
	{ /* VMODE_1024x768p60hz */
		.name              = "1024x768p60hz",
		.mode              = VMODE_1024x768p60hz,
		.width             = 1024,
		.height            = 768,
	},
	{ /* VMODE_1152x864p75hz */
		.name              = "1152x864p75hz",
		.mode              = VMODE_1152x864p75hz,
		.width             = 1152,
		.height            = 864,
	},
	{ /* VMODE_1280x600p60hz */
		.name              = "1280x600p60hz",
		.mode              = VMODE_1280x600p60hz,
		.width             = 1280,
		.height            = 600,
	},
	{ /* VMODE_1280x768p60hz */
		.name              = "1280x768p60hz",
		.mode              = VMODE_1280x768p60hz,
		.width             = 1280,
		.height            = 768,
	},
	{ /* VMODE_1280x800p60hz */
		.name              = "1280x800p60hz",
		.mode              = VMODE_1280x800p60hz,
		.width             = 1280,
		.height            = 800,
	},
	{ /* VMODE_1280x960p60hz */
		.name              = "1280x960p60hz",
		.mode              = VMODE_1280x960p60hz,
		.width             = 1280,
		.height            = 960,
	},
	{ /* VMODE_1280x1024p60hz */
		.name              = "1280x1024p60hz",
		.mode              = VMODE_1280x1024p60hz,
		.width             = 1280,
		.height            = 1024,
	},
	{ /* VMODE_1360x768p60hz */
		.name              = "1360x768p60hz",
		.mode              = VMODE_1360x768p60hz,
		.width             = 1360,
		.height            = 768,
	},
	{ /* VMODE_1366x768p60hz */
		.name              = "1366x768p60hz",
		.mode              = VMODE_1366x768p60hz,
		.width             = 1366,
		.height            = 768,
	},
	{ /* VMODE_1400x1050p60hz */
		.name              = "1400x1050p60hz",
		.mode              = VMODE_1400x1050p60hz,
		.width             = 1400,
		.height            = 1050,
	},
	{ /* VMODE_1440x900p60hz */
		.name              = "1440x900p60hz",
		.mode              = VMODE_1440x900p60hz,
		.width             = 1440,
		.height            = 900,
	},
	{ /* VMODE_1600x900p60hz */
		.name              = "1600x900p60hz",
		.mode              = VMODE_1600x900p60hz,
		.width             = 1600,
		.height            = 900,
	},
	{ /* VMODE_1600x1200p60hz */
		.name              = "1600x1200p60hz",
		.mode              = VMODE_1600x1200p60hz,
		.width             = 1600,
		.height            = 1200,
	},
	{ /* VMODE_1680x1050p60hz */
		.name              = "1680x1050p60hz",
		.mode              = VMODE_1680x1050p60hz,
		.width             = 1680,
		.height            = 1050,
	},
	{ /* VMODE_1920x1200p60hz */
		.name              = "1920x1200p60hz",
		.mode              = VMODE_1920x1200p60hz,
		.width             = 1920,
		.height            = 1200,
	},
	{ /* VMODE_2160x1200p90hz */
		.name			= "1920x1200p90hz",
		.mode	 		= VMODE_2160x1200p90hz,
		.width	 		= 2160,
		.height 		= 1200,
	},
	{ /* VMODE_2560x1080p60hz */
		.name              = "2560x1080p60hz",
		.mode              = VMODE_2560x1080p60hz,
		.width             = 2560,
		.height            = 1080,
	},
};

vidinfo_t tv_info = {
	.vl_col	= 1280,              /* Number of columns (i.e. 160) */
	.vl_row	= 720,               /* Number of rows (i.e. 100) */
	.vl_bpix = 24,               /* Bits per pixel */
	.vd_base = NULL,             /* Start of framebuffer memory	*/
	.vd_console_address = NULL,  /* Start of console buffer	*/
	.console_col = 0,
	.console_row = 0,

	.vd_color_fg = 0xffff,
	.vd_color_bg = 0,
	.max_bl_level = 255,
	.cmap = NULL,                /* Pointer to the colormap */
	.priv = NULL,                /* Pointer to driver-specific data */
};

static inline void vout_reg_write(u32 reg,
				 const u32 val)
{
	*(volatile unsigned int *)REG_ADDR_VCBUS(reg) = (val);
}

static int vout_find_mode_by_name(const char *name)
{
	int mode = -1;
	int i = 0;

	for (i = 0; i < sizeof(vout_sets) / sizeof(struct vout_set_s); i++) {
		if (strncmp(name, vout_sets[i].name, strlen(vout_sets[i].name)) == 0) {
			mode = vout_sets[i].mode;
			return mode;
		}
	}

	vout_log("mode: %s not found\n", name);
	return -1;
}

static int vout_find_width_by_name(const char* name)
{
	int i = 0;
	ulong width = 0;

	for (i = 0; i < sizeof(vout_sets) / sizeof(struct vout_set_s); i++) {
		if (strncmp(name, vout_sets[i].name, strlen(vout_sets[i].name)) == 0) {
			width = vout_sets[i].width;
			return width;
		}
	}

	return width;
}

static int vout_find_height_by_name(const char* name)
{
	int height = 0;
	int i = 0;

	for (i = 0; i < sizeof(vout_sets) / sizeof(struct vout_set_s); i++) {
		if (strncmp(name, vout_sets[i].name, strlen(vout_sets[i].name)) == 0) {
			height = vout_sets[i].height;
			return height;
		}
	}

	return height;
}

static void vout_vinfo_init(void)
{
	tv_info.vd_base = (void *)get_fb_addr();
	tv_info.vl_col = simple_strtoul(getenv("display_width"), NULL, 0);
	tv_info.vl_row = simple_strtoul(getenv("display_height"), NULL, 0);
	tv_info.vl_bpix = simple_strtoul(getenv("display_bpp"), NULL, 10);
	tv_info.vd_color_fg = simple_strtoul(getenv("display_color_fg"), NULL, 0);
	tv_info.vd_color_bg = simple_strtoul(getenv("display_color_bg"), NULL, 0);
}

static void vout_axis_init(ulong w, ulong h)
{
	ulong width = w;
	ulong height = h;

	setenv_ulong("display_width", width);
	setenv_ulong("display_height", height);
}

static void vout_vmode_init(void)
{
	char *outputmode = NULL;
	int vmode = -1;
	ulong width = 0;
	ulong height = 0;
#ifdef CONFIG_AML_LCD
	struct aml_lcd_drv_s *lcd_drv;
#endif

	outputmode = getenv("outputmode");
	vmode = vout_find_mode_by_name(outputmode);
	vout_set_current_vmode(vmode);
	switch (vmode) {
#ifdef CONFIG_AML_LCD
	case VMODE_LCD:
		lcd_drv = aml_lcd_get_driver();
		width = lcd_drv->lcd_config->lcd_basic.h_active;
		height = lcd_drv->lcd_config->lcd_basic.v_active;
		break;
#endif
	default:
		width = vout_find_width_by_name(outputmode);
		height = vout_find_height_by_name(outputmode);
		break;
	}
	vout_reg_write(VPP_POSTBLEND_H_SIZE, width);
	vout_axis_init(width, height);
}

static int my_atoi(const char *str)
{
	int result = 0;
	int signal = 1;

	if ((*str >= '0' && *str <= '9') || *str == '-' || *str == '+') {
		if (*str == '-' || *str == '+') {
			if (*str == '-')
				signal = -1;
			str++;
		}
	} else
		return 0;

	while (*str >= '0' && *str <= '9')
		result = result * 10 + (*str++ -'0');

	return signal * result;
}

static int getenv_int(char *env, int def)
{
	if (getenv(env) == NULL)
		return def;
	else
		return my_atoi(getenv(env));
}

static int get_window_axis(int *axis)
{
	int ret = 0;
	char *mode = getenv("outputmode");

	if (strncmp(mode, "480i", 4) == 0 || strcmp(mode, "480cvbs") == 0) {
		axis[0] = getenv_int("480i_x", 0);
		axis[1] = getenv_int("480i_y", 0);
		axis[2] = getenv_int("480i_w", 720);
		axis[3] = getenv_int("480i_h", 480);
	} else if (strncmp(mode, "480p", 4) == 0) {
		axis[0] = getenv_int("480p_x", 0);
		axis[1] = getenv_int("480p_y", 0);
		axis[2] = getenv_int("480p_w", 720);
		axis[3] = getenv_int("480p_h", 480);
	} else if (strncmp(mode, "576i", 4) == 0 || strcmp(mode, "576cvbs") == 0) {
		axis[0] = getenv_int("576i_x", 0);
		axis[1] = getenv_int("576i_y", 0);
		axis[2] = getenv_int("576i_w", 720);
		axis[3] = getenv_int("576i_h", 576);
	} else if (strncmp(mode, "576p", 4) == 0) {
		axis[0] = getenv_int("576p_x", 0);
		axis[1] = getenv_int("576p_y", 0);
		axis[2] = getenv_int("576p_w", 720);
		axis[3] = getenv_int("576p_h", 576);
	} else if (strncmp(mode, "720p", 4) == 0) {
		axis[0] = getenv_int("720p_x", 0);
		axis[1] = getenv_int("720p_y", 0);
		axis[2] = getenv_int("720p_w", 1280);
		axis[3] = getenv_int("720p_h", 720);
	} else if (strncmp(mode, "768p", 4) == 0) {
		axis[0] = getenv_int("768p_x", 0);
		axis[1] = getenv_int("768p_y", 0);
		axis[2] = getenv_int("768p_w", 1366);
		axis[3] = getenv_int("768p_h", 768);
	} else if (strncmp(mode, "1080i", 5) == 0) {
		axis[0] = getenv_int("1080i_x", 0);
		axis[1] = getenv_int("1080i_y", 0);
		axis[2] = getenv_int("1080i_w", 1920);
		axis[3] = getenv_int("1080i_h", 1080);
	} else if (strncmp(mode, "1080p", 5) == 0) {
		axis[0] = getenv_int("1080p_x", 0);
		axis[1] = getenv_int("1080p_y", 0);
		axis[2] = getenv_int("1080p_w", 1920);
		axis[3] = getenv_int("1080p_h", 1080);
	} else if (strncmp(mode, "2160p", 5) == 0) {
		axis[0] = getenv_int("2160p_x", 0);
		axis[1] = getenv_int("2160p_y", 0);
		axis[2] = getenv_int("2160p_w", 3840);
		axis[3] = getenv_int("2160p_h", 2160);
	} else if (strncmp(mode, "smpte",5) == 0) {
		axis[0] = getenv_int("4k2ksmpte_x", 0);
		axis[1] = getenv_int("4k2ksmpte_y", 0);
		axis[2] = getenv_int("4k2ksmpte_w", 4096);
		axis[3] = getenv_int("4k2ksmpte_h", 2160);
	} else {
		axis[0] = getenv_int("1080p_x", 0);
		axis[1] = getenv_int("1080p_y", 0);
		axis[2] = getenv_int("1080p_w", 1920);
		axis[3] = getenv_int("1080p_h", 1080);
	}

	return ret;
}

void vout_set_current_vmode(int mode)
{
	g_vmode = mode;
}

int vout_get_current_vmode(void)
{
	vout_logl();
	return g_vmode;
}

vidinfo_t *vout_get_current_vinfo(void)
{
	vidinfo_t *info = &tv_info;

	vout_logl();

	return info;
}

int vout_get_current_axis(int *axis)
{
	return get_window_axis(axis);
}

void vout_vinfo_dump(void)
{
	vidinfo_t *info = NULL;

	vout_logl();
	info = vout_get_current_vinfo();
	vout_log("vinfo.vd_base: 0x%p\n", info->vd_base);
	vout_log("vinfo.vl_col: %d\n", info->vl_col);
	vout_log("vinfo.vl_row: %d\n", info->vl_row);
	vout_log("vinfo.vl_bpix: %d\n", info->vl_bpix);
	vout_log("vinfo.vd_color_fg: %d\n", info->vd_color_fg);
	vout_log("vinfo.vd_color_bg: %d\n", info->vd_color_bg);
}

void vout_init(void)
{
	vout_logl();
	vout_vmode_init();
	vout_vinfo_init();
}
