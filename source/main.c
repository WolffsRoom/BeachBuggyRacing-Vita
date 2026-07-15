#include "utils/dialog.h"
#include "utils/init.h"
#include "utils/logger.h"

#include <psp2/kernel/processmgr.h>
#include <psp2/io/fcntl.h>
#include <psp2/apputil.h>

#include <falso_jni/FalsoJNI.h>
#include <so_util/so_util.h>

#include "reimpl/asset_manager.h"
#include "reimpl/controls.h"
#include "reimpl/native_activity.h"

extern void port_trace(const char *format, ...);

int _newlib_heap_size_user = 256 * 1024 * 1024;

#ifdef USE_SCELIBC_IO
int sceLibcHeapSize = 4 * 1024 * 1024;
#endif

so_module so_mod;
/* Kept under the established loader names to minimize cross-module loader
 * changes: these hold libfmod.so and libfmodstudio.so respectively. */
so_module so_fmodex_mod;
so_module so_fmodevent_mod;

typedef void (*android_main_fn)(android_app *app);
typedef void (*purple_string_fn)(JNIEnv *env, jobject object, jstring value);
typedef void (*purple_boolean_fn)(JNIEnv *env, jobject object, jboolean value);

static uintptr_t required_symbol(const char *name) {
    uintptr_t address = so_symbol(&so_mod, name);
    if (!address)
        fatal_error("Required libPurple.so symbol is missing: %s", name);
    return address;
}

static void write_raw_boot_marker(void) {
    static const char marker[] =
        "Beach Buggy Racing Vita Shader Cache v30 reached main()\n";
    SceUID fd = sceIoOpen("ux0:data/bbr_boot.log",
                          SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    if (fd >= 0) {
        sceIoWrite(fd, marker, sizeof(marker) - 1);
        sceIoClose(fd);
    }
}

static android_app *bootstrap_native_activity(void) {
    port_trace("main: creating NativeActivity bridge");
    AAssetManager *assets = AAssetManager_create();
    android_app *app = native_activity_create(&jvm, &jni, assets, DATA_PATH);
    if (!app)
        fatal_error("Could not allocate the Android NativeActivity bridge.");

    purple_string_fn set_internal_path = (purple_string_fn)required_symbol(
        "Java_com_vectorunit_purple_googleplay_Purple_setInternalDataPath");
    purple_string_fn set_command_line = (purple_string_fn)required_symbol(
        "Java_com_vectorunit_purple_googleplay_Purple_setCmdLine");
    purple_boolean_fn set_has_touch = (purple_boolean_fn)required_symbol(
        "Java_com_vectorunit_purple_googleplay_Purple_setHasTouch");

    jstring data_path = jni->NewStringUTF(&jni, DATA_PATH);
    jstring command_line = jni->NewStringUTF(&jni, "");
    set_internal_path(&jni, app->activity->clazz, data_path);
    port_trace("main: internal data path set to %s", DATA_PATH);
    set_command_line(&jni, app->activity->clazz, command_line);
    port_trace("main: command line set");
    set_has_touch(&jni, app->activity->clazz, JNI_TRUE);
    port_trace("main: touch capability set");

    l_success("Beach Buggy Racing NativeActivity bridge initialized.");
    return app;
}

int main(void) {
    /* Initialize the application runtime before touching ux0.  The old
     * boilerplate also called sceAppUtilReceiveAppEvent synchronously during
     * startup; BBR does not use its configurator path, so that potential wait
     * is intentionally omitted. */
    sceAppUtilInit(&(SceAppUtilInitParam){}, &(SceAppUtilBootParam){});
    write_raw_boot_marker();
    sceIoRemove(DATA_PATH "port.log");
    port_trace("Beach Buggy Racing Vita port trace v30 (post-link VitaGL shader cache + touch + OpenSL audio)");
    port_trace("build id: BBR-v30-post-link-shader-cache-20260713");
    port_trace("main: process entered, DATA_PATH=%s", DATA_PATH);
    soloader_init_all();

    port_trace("main: loader initialization completed");

    android_app *app = bootstrap_native_activity();
    android_main_fn android_main = (android_main_fn)required_symbol(
        "android_main");

    /* android_main owns the renderer loop. ALooper_pollAll in the bridge
     * supplies lifecycle commands and polls Vita controls on that same
     * thread, matching the Android native_app_glue execution model. */
    port_trace("main: entering libPurple android_main=%p app=%p",
               android_main, app);
    android_main(app);
    port_trace("main: libPurple android_main returned");
    sceKernelExitProcess(0);
    return 0;
}

void controls_handler_key(int32_t keycode, ControlsAction action) {
    native_activity_send_key(keycode, action);
}

void controls_handler_touch(int32_t id, float x, float y,
                            ControlsAction action) {
    native_activity_send_touch(id, x, y, action);
}

void controls_handler_analog(ControlsStickId which, float x, float y,
                             ControlsAction action) {
    native_activity_send_analog(which, x, y, action);
}
