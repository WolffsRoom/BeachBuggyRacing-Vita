/* Beach Buggy Racing / Vector Unit Purple engine patches and trace. */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <psp2/io/fcntl.h>

#include <so_util/so_util.h>

extern so_module so_mod;
extern so_module so_fmodex_mod;
extern so_module so_fmodevent_mod;

static int performance_trace_allowed(const char *line) {
#ifdef PERFORMANCE_BUILD
    if (!line)
        return 0;

    /* Keep only a compact boot/loader trace and actionable failures. */
    if (strncmp(line, "Beach Buggy", 11) == 0 ||
        strncmp(line, "build id:", 9) == 0 ||
        strncmp(line, "main:", 5) == 0 ||
        strncmp(line, "loader:", 7) == 0 ||
        strncmp(line, "patch:", 6) == 0 ||
        strncmp(line, "FMOD", 4) == 0 ||
        strncmp(line, "OpenSL", 6) == 0 ||
        strncmp(line, "Audio dlopen:", 13) == 0 ||
        strncmp(line, "GL cache:", 9) == 0 ||
        strstr(line, "slCreateEngine") ||
        strstr(line, "normalized ") ||
        strstr(line, "expanded masked") ||
        strstr(line, "failed") ||
        strstr(line, "fatal") ||
        strstr(line, "error") ||
        strstr(line, "missing")) {
        return 1;
    }
    return 0;
#else
    (void)line;
    return 1;
#endif
}

void port_trace(const char *format, ...) {
    char line[768];
    va_list args;
    va_start(args, format);
    int length = vsnprintf(line, sizeof(line) - 2, format, args);
    va_end(args);
    if (length < 0)
        return;
    if (length > (int)sizeof(line) - 2)
        length = sizeof(line) - 2;
    line[length] = '\0';

    if (!performance_trace_allowed(line))
        return;

    line[length++] = '\n';

#ifdef PERFORMANCE_BUILD
    /* Avoid the old open/write/close cycle on every trace line. */
    static SceUID fd = -1;
    if (fd < 0) {
        fd = sceIoOpen(DATA_PATH "port.log",
                       SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
    }
    if (fd >= 0)
        sceIoWrite(fd, line, length);
#else
    SceUID fd = sceIoOpen(DATA_PATH "port.log",
                          SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
    if (fd >= 0) {
        sceIoWrite(fd, line, length);
        sceIoClose(fd);
    }
#endif
}

void so_patch(void) {
    port_trace("patch: v30 reached so_patch (post-link VitaGL shader cache + touch + OpenSL audio)");
    port_trace("libPurple=%p libfmod=%p libfmodstudio=%p",
               (void *)so_mod.text_base, (void *)so_fmodex_mod.text_base,
               (void *)so_fmodevent_mod.text_base);
}
