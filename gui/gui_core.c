/*
 *   Copyright (c) 2025 Igor Belwon <igor.belwon@mentallysanemainliners.org>
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include <math.h>
#include <limits.h>
#include <time.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdatomic.h>

// Include SDL2 first
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>

// Nuklear
#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#define NK_IMPLEMENTATION
#define NK_SDL_GL2_IMPLEMENTATION

#include <external/nuklear.h>
#include <external/nuklear_sdl_gl2.h>

#include <fireplace/core/core.h>
#include <fireplace/core/emulator.h>
#include <fireplace/gui/gui.h>
#include <fireplace/soc/fb/fb.h>
#include <fireplace/soc/hardware_buttons/hardware_buttons.h>

#define DEFAULT_RESOLUTION_SCALE (0.4)
#define WINDOW_MARGIN 64
#define TARGET_FRAME_MS (1000 / 30)

void gui_init(void)
{
	/* SDL setup */
	SDL_SetHint(SDL_HINT_VIDEO_HIGHDPI_DISABLED, "0");
	SDL_Init(SDL_INIT_VIDEO);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);

	return;
}

void blit_window(struct nk_context *ctx)
{
	static enum fireplace_boot_mode selected_mode = FIREPLACE_BOOT_ANDROID;
	static bool mode_initialized;
	state emuState = 0;

	emuState = atomic_load(&sharedState);
	if (!mode_initialized) {
		selected_mode = get_emulator_boot_mode();
		mode_initialized = true;
	}

	/* GUI */
	if (nk_begin(ctx, "Emulator setup", nk_rect(0, 0, 360, 190),
		     NK_WINDOW_BORDER | NK_WINDOW_MOVABLE | NK_WINDOW_SCALABLE |
			 NK_WINDOW_MINIMIZABLE | NK_WINDOW_TITLE))
	{
		nk_layout_row_static(ctx, 30, 80, 1);
		if (emuState != STATE_RUNNING)
		{
			nk_layout_row_dynamic(ctx, 30, 3);
			if (nk_option_label(ctx, "Android",
					    selected_mode == FIREPLACE_BOOT_ANDROID)) {
				selected_mode = FIREPLACE_BOOT_ANDROID;
				set_emulator_boot_mode(selected_mode);
			}
			if (nk_option_label(ctx, "Recovery",
					    selected_mode == FIREPLACE_BOOT_RECOVERY)) {
				selected_mode = FIREPLACE_BOOT_RECOVERY;
				set_emulator_boot_mode(selected_mode);
			}
			if (nk_option_label(ctx, "Download",
					    selected_mode == FIREPLACE_BOOT_DOWNLOAD)) {
				selected_mode = FIREPLACE_BOOT_DOWNLOAD;
				set_emulator_boot_mode(selected_mode);
			}

			nk_layout_row_dynamic(ctx, 30, 1);
			if (nk_button_label(ctx, "Start"))
				create_emulator_thread();
		}

		nk_layout_row_dynamic(ctx, 30, 1);

		switch (emuState)
		{
		case STATE_OFF:
			nk_label_colored(ctx, "Emulator state: off",
					 NK_TEXT_LEFT, nk_rgb(208, 211, 212));
			break;
		case STATE_RUNNING:
			nk_label_colored(ctx, "Emulator state: running",
					 NK_TEXT_LEFT, nk_rgb(9, 255, 0));
			break;
		case STATE_CRASHED:
			nk_label_colored(ctx, "Emulator state: crashed",
					 NK_TEXT_LEFT, nk_rgb(208, 211, 212));
			break;
		default:
			nk_label_colored(ctx, "Emulator state: unknown",
					 NK_TEXT_LEFT, nk_rgb(255, 0, 166));
			break;
		}
	}
	nk_end(ctx);
}

static void choose_window_size(int *width, int *height)
{
	double scale = DEFAULT_RESOLUTION_SCALE;
	SDL_Rect usable;

	if (SDL_GetDisplayUsableBounds(0, &usable) == 0) {
		int usable_width = usable.w - WINDOW_MARGIN;
		int usable_height = usable.h - WINDOW_MARGIN;
		double width_scale;
		double height_scale;

		if (usable_width > 0 && usable_height > 0) {
			width_scale = (double)usable_width / FB_WIDTH;
			height_scale = (double)usable_height / FB_HEIGHT;
			if (scale > width_scale)
				scale = width_scale;
			if (scale > height_scale)
				scale = height_scale;
		}
	}
	*width = (int)(FB_WIDTH * scale);
	*height = (int)(FB_HEIGHT * scale);
	if (*width < 1)
		*width = 1;
	if (*height < 1)
		*height = 1;
}

void blit_hardware_button_window(struct nk_context *ctx)
{
	state emuState = 0;

	emuState = atomic_load(&sharedState);

	if(emuState == STATE_RUNNING)
	{
		if(nk_begin(ctx, "Hardware Button Control", nk_rect(25, 25, 321, 83),
			    NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_MOVABLE | NK_WINDOW_MINIMIZABLE | NK_WINDOW_TITLE))
		{
			nk_layout_row_static(ctx, 40, 100, 3);

			if (nk_button_label(ctx, "Power"))
			{
				trigger_key(POWER);
			}

			if (nk_button_label(ctx, "Volume Up"))
			{
				trigger_key(VOL_UP);
			}

			if (nk_button_label(ctx, "Volume Down"))
			{
				trigger_key(VOL_DOWN);
			}
		}
		nk_end(ctx);
	}
}

static GLuint fb_texture = 0;

static GLuint prepare_fb_texture()
{
	bool texture_created = false;

	if (fb_texture == 0)
	{
		glGenTextures(1, &fb_texture);
		texture_created = true;
	}

	glBindTexture(GL_TEXTURE_2D, fb_texture);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, FB_WIDTH);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

	if (texture_created)
	{
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, FB_WIDTH,
			     FB_HEIGHT, 0, GL_BGRA, GL_UNSIGNED_BYTE,
			     framebuffer);
	} else {
		/*
		 * The guest writes directly into framebuffer through
		 * uc_mem_map_ptr(). Sampling once per GUI frame avoids a memory
		 * callback for every guest store and cannot miss a presentation.
		 */
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, FB_WIDTH,
				(GLsizei)FB_HEIGHT, GL_BGRA, GL_UNSIGNED_BYTE,
				framebuffer);
	}

	if (texture_created)
	{
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	}
	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
	glBindTexture(GL_TEXTURE_2D, 0);

	return fb_texture;
}

void render_ogl(SDL_Window *win, int win_width, int win_height)
{
	SDL_GetWindowSize(win, &win_width, &win_height);
	glViewport(0, 0, win_width, win_height);
	glClear(GL_COLOR_BUFFER_BIT);

	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, prepare_fb_texture());

	glBegin(GL_QUADS);
	glTexCoord2f(0.0f, 1.0f);
	glVertex2f(-1.0f, -1.0f); // Bottom-left
	glTexCoord2f(1.0f, 1.0f);
	glVertex2f(1.0f, -1.0f); // Bottom-right
	glTexCoord2f(1.0f, 0.0f);
	glVertex2f(1.0f, 1.0f); // Top-right
	glTexCoord2f(0.0f, 0.0f);
	glVertex2f(-1.0f, 1.0f); // Top-left
	glEnd();

	glBindTexture(GL_TEXTURE_2D, 0);

	nk_sdl_render(NK_ANTI_ALIASING_ON);
	SDL_GL_SwapWindow(win);
}

void *gui_core(void *dummy)
{
	/* Platform */
	SDL_Window *win;
	SDL_GLContext glContext;
	int win_width, win_height;
	int running = 1;

	/* GUI */
	struct nk_context *ctx;

	gui_init();
	choose_window_size(&win_width, &win_height);

	win = SDL_CreateWindow("Fireplace",
				       SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
				       win_width, win_height,
				       SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN |
					       SDL_WINDOW_ALLOW_HIGHDPI |
					       SDL_WINDOW_RESIZABLE);
	glContext = SDL_GL_CreateContext(win);
	SDL_GL_SetSwapInterval(1);
	SDL_GetWindowSize(win, &win_width, &win_height);

	/* GUI */
	ctx = nk_sdl_init(win);
	{
		struct nk_font_atlas *atlas;
		nk_sdl_font_stash_begin(&atlas);
		nk_sdl_font_stash_end();
	}

	while (running)
	{
		Uint64 frame_start = SDL_GetTicks64();

		/* Input */
		SDL_Event evt;
		nk_input_begin(ctx);
		while (SDL_PollEvent(&evt))
		{
			if (evt.type == SDL_QUIT)
				goto cleanup;
			nk_sdl_handle_event(&evt);
		}
		nk_input_end(ctx);

		blit_window(ctx);
		blit_hardware_button_window(ctx);

		render_ogl(win, win_height, win_width);

		Uint64 frame_time = SDL_GetTicks64() - frame_start;
		if (frame_time < TARGET_FRAME_MS)
			SDL_Delay(TARGET_FRAME_MS - frame_time);
	}

cleanup:
	nk_sdl_shutdown();
	SDL_GL_DeleteContext(glContext);
	SDL_DestroyWindow(win);
	SDL_Quit();
	return dummy;
}
