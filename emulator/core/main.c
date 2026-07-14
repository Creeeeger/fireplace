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

#include <pthread.h>
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <fireplace/core/emulator.h>
#include <fireplace/gui/gui.h>

bool emu_running = false;

static pthread_t emulator_thread;
static struct fireplace_emulator_options gui_emulator_options = {
	.lun_directory = ".",
	.boot_mode = FIREPLACE_BOOT_ANDROID,
};

static void terminate_on_suspend(int signal_number)
{
	_exit(128 + signal_number);
}

static int install_signal_handlers(void)
{
	struct sigaction action = {0};

	action.sa_handler = terminate_on_suspend;
	sigemptyset(&action.sa_mask);
	if (sigaction(SIGTSTP, &action, NULL) == -1) {
		fprintf(stderr, "Failed to install Ctrl-Z handler: %s\n",
			strerror(errno));
		return -1;
	}
	return 0;
}

void create_emulator_thread(void)
{
	int err = pthread_create(&emulator_thread, NULL, emulator_thread_main,
				 &gui_emulator_options);

	if (err != 0)
		fprintf(stderr, "Failed to start emulator thread: %s\n",
			strerror(err));
}

void set_emulator_boot_mode(enum fireplace_boot_mode mode)
{
	if (mode >= FIREPLACE_BOOT_ANDROID && mode <= FIREPLACE_BOOT_DOWNLOAD)
		gui_emulator_options.boot_mode = mode;
}

enum fireplace_boot_mode get_emulator_boot_mode(void)
{
	return gui_emulator_options.boot_mode;
}

static void usage(const char *program)
{
	printf("Usage: %s [--headless] [--lun-dir PATH] "
	       "[--boot-mode android|recovery|download]\n", program);
}

static bool parse_boot_mode(const char *value,
			    enum fireplace_boot_mode *mode)
{
	if (strcmp(value, "android") == 0)
		*mode = FIREPLACE_BOOT_ANDROID;
	else if (strcmp(value, "recovery") == 0)
		*mode = FIREPLACE_BOOT_RECOVERY;
	else if (strcmp(value, "download") == 0)
		*mode = FIREPLACE_BOOT_DOWNLOAD;
	else {
		fprintf(stderr, "Invalid boot mode: %s\n", value);
		return false;
	}
	return true;
}


int main(int argc, char **argv)
{
	struct fireplace_emulator_options options = {0};
	bool headless = false;

	if (install_signal_handlers() != 0)
		return EXIT_FAILURE;
	setvbuf(stdout, NULL, _IONBF, 0);

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--headless") == 0) {
			headless = true;
		} else if (strcmp(argv[i], "--lun-dir") == 0) {
			if (++i == argc) {
				fprintf(stderr, "--lun-dir requires a path\n");
				return 2;
			}
			options.lun_directory = argv[i];
		} else if (strcmp(argv[i], "--boot-mode") == 0) {
			if (++i == argc ||
			    !parse_boot_mode(argv[i], &options.boot_mode))
				return 2;
		} else if (strcmp(argv[i], "--help") == 0 ||
			   strcmp(argv[i], "-h") == 0) {
			usage(argv[0]);
			return 0;
		} else {
			fprintf(stderr, "Unknown option: %s\n", argv[i]);
			usage(argv[0]);
			return 2;
		}
	}
	if (headless && !options.lun_directory) {
		fprintf(stderr, "--lun-dir PATH is required for UFS boot\n");
		return 2;
	}
	options.headless = headless;
	if (headless)
		return emulator_run(&options);
	if (!options.lun_directory)
		options.lun_directory = ".";
	gui_emulator_options = options;

	/* SDL and OpenGL must run on the main thread on macOS. */
	gui_core(NULL);
	if (emu_running)
		pthread_join(emulator_thread, NULL);

	return 0;
}
