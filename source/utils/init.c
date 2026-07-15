/*
 * Copyright (C) 2021      Andy Nguyen
 * Copyright (C) 2021-2022 Rinnegatamante
 * Copyright (C) 2022-2024 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include "utils/init.h"

#include "utils/dialog.h"
#include "utils/glutil.h"
#include "utils/logger.h"
#include "utils/utils.h"
#include "utils/settings.h"

#include <reimpl/controls.h>

#include <string.h>

#include <psp2/appmgr.h>
#include <psp2/apputil.h>
#include <psp2/kernel/clib.h>
#include <psp2/power.h>

#include <falso_jni/FalsoJNI.h>
#include <so_util/so_util.h>
#include <fios/fios.h>

// Keep one 16 MiB arena per Android module. The actual PT_LOAD ranges are
// much smaller, but the spacing leaves room for so_util patch arenas.
#define FMOD_LOAD_ADDRESS       0x98000000
#define FMODSTUDIO_LOAD_ADDRESS 0x99000000
#define PURPLE_LOAD_ADDRESS     0x9A000000

extern so_module so_mod;
extern so_module so_fmodex_mod;
extern so_module so_fmodevent_mod;
extern void port_trace(const char *format, ...);

typedef jint (*jni_onload_fn)(JavaVM *vm, void *reserved);

static void initialize_fmod_jni_bridge(void) {
    uintptr_t address = so_symbol(&so_fmodex_mod, "JNI_OnLoad");
    if (!address) {
        port_trace("FMOD JNI bootstrap failed: JNI_OnLoad export missing");
        return;
    }

    jni_onload_fn on_load = (jni_onload_fn)address;
    jint version = on_load(&jvm, NULL);
    port_trace("FMOD JNI_OnLoad returned=0x%x address=%p", version,
               (void *)address);
}

void soloader_init_all() {
	port_trace("loader: soloader_init_all entered");
	port_trace("loader: AppUtil already initialized; startup event polling skipped");

    // Set default overclock values
    scePowerSetArmClockFrequency(444);
    scePowerSetBusClockFrequency(222);
    scePowerSetGpuClockFrequency(222);
    scePowerSetGpuXbarClockFrequency(166);

#ifdef USE_SCELIBC_IO
    port_trace("loader: starting FIOS for %s", DATA_PATH);
    if (fios_init(DATA_PATH) == 0)
        l_success("FIOS initialized.");
    port_trace("loader: FIOS initialization returned");
#endif

    port_trace("loader: checking kubridge");
    if (!module_loaded("kubridge")) {
        l_fatal("kubridge is not loaded.");
        fatal_error("Error: kubridge.skprx is not installed.");
    }
    l_success("kubridge check passed.");
    port_trace("loader: kubridge check passed");

    port_trace("loader: checking Android data files");
    if (!file_exists(PURPLE_SO_PATH) ||
        !file_exists(FMOD_SO_PATH) ||
        !file_exists(FMODSTUDIO_SO_PATH)) {
        fatal_error("Looks like you haven't installed the data files for this "
                    "port. Expected libPurple.so, libfmod.so and "
                    "libfmodstudio.so under %s.", DATA_PATH);
    }
    port_trace("loader: all Android data files found");

    port_trace("loader: loading libfmod.so at 0x%x", FMOD_LOAD_ADDRESS);
    if (so_file_load(&so_fmodex_mod, FMOD_SO_PATH, FMOD_LOAD_ADDRESS) < 0) {
        fatal_error("Error: could not load %s.", FMOD_SO_PATH);
    }
    port_trace("loader: libfmod.so loaded text=%p", so_fmodex_mod.text_base);
    port_trace("loader: loading libfmodstudio.so at 0x%x",
               FMODSTUDIO_LOAD_ADDRESS);
    if (so_file_load(&so_fmodevent_mod, FMODSTUDIO_SO_PATH, FMODSTUDIO_LOAD_ADDRESS) < 0) {
        fatal_error("Error: could not load %s.", FMODSTUDIO_SO_PATH);
    }
    port_trace("loader: libfmodstudio.so loaded text=%p",
               so_fmodevent_mod.text_base);
    port_trace("loader: loading libPurple.so at 0x%x", PURPLE_LOAD_ADDRESS);
    if (so_file_load(&so_mod, PURPLE_SO_PATH, PURPLE_LOAD_ADDRESS) < 0) {
        fatal_error("Error: could not load %s.", PURPLE_SO_PATH);
    }
    port_trace("loader: libPurple.so loaded text=%p", so_mod.text_base);
    l_success("Android modules loaded.");

    settings_load();
    l_success("Settings loaded.");
    port_trace("loader: settings loaded");

    port_trace("loader: relocating libfmod.so");
    so_relocate(&so_fmodex_mod);
    port_trace("loader: relocating libfmodstudio.so");
    so_relocate(&so_fmodevent_mod);
    port_trace("loader: relocating libPurple.so");
    so_relocate(&so_mod);
    l_success("Android modules relocated.");
    port_trace("loader: all modules relocated");

    port_trace("loader: resolving libfmod.so imports");
    resolve_imports(&so_fmodex_mod);
    port_trace("loader: resolving libfmodstudio.so imports");
    resolve_imports(&so_fmodevent_mod);
    port_trace("loader: resolving libPurple.so imports");
    resolve_imports(&so_mod);
    l_success("Android module imports resolved.");
    port_trace("loader: all imports resolved");

    port_trace("loader: applying game patches");
    so_patch();
    l_success("SO patched.");
    port_trace("loader: game patches applied");

    so_flush_caches(&so_fmodex_mod);
    so_flush_caches(&so_fmodevent_mod);
    so_flush_caches(&so_mod);
    l_success("Android module caches flushed.");

    port_trace("loader: initializing libfmod.so constructors");
    so_initialize(&so_fmodex_mod);
    port_trace("loader: initializing libfmodstudio.so constructors");
    so_initialize(&so_fmodevent_mod);
    port_trace("loader: initializing libPurple.so constructors");
    so_initialize(&so_mod);
    l_success("Android modules initialized.");
    port_trace("loader: all module constructors completed");

    port_trace("loader: checking shader compiler");
    gl_preload();
    l_success("OpenGL preloaded.");

    jni_init();
    l_success("FalsoJNI initialized.");
    port_trace("loader: FalsoJNI initialized");

    /* Android normally invokes this when System.loadLibrary loads FMOD.
     * The custom ELF loader must do it explicitly so FMOD receives the
     * JavaVM and can resolve org.fmod.FMOD helper methods. */
    initialize_fmod_jni_bridge();

    controls_init();
    l_success("Controls initialized.");
    port_trace("loader: controls initialized");
}
