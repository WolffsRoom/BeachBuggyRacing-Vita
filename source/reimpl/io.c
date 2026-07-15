/*
 * Copyright (C) 2021      Andy Nguyen
 * Copyright (C) 2022      Rinnegatamante
 * Copyright (C) 2022-2024 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include "reimpl/io.h"

#include <string.h>
#include <sys/stat.h>
#include <sys/unistd.h>
#include <stdlib.h>
#include <dirent.h>
#include <stdarg.h>
#include <psp2/kernel/threadmgr.h>

#ifdef USE_SCELIBC_IO
#include <libc_bridge/libc_bridge.h>
#endif

#include "utils/logger.h"
#include "utils/utils.h"

extern void port_trace(const char *format, ...);

// Includes the following inline utilities:
// int oflags_musl_to_newlib(int flags);
// dirent64_bionic * dirent_newlib_to_bionic(struct dirent* dirent_newlib);
// void stat_newlib_to_bionic(struct stat * src, stat64_bionic * dst);
#include "reimpl/bits/_struct_converters.c"

// Compatibility for engines that pass bundle:// or res/ asset names through
// libc instead of AAssetManager. Beach Buggy Racing normally opens its APF
// containers through AAssetManager, so this is a harmless fallback.
static const char *translate_bundle_path(const char *path, char translated[PATH_MAX]) {
    static const char prefix[] = "bundle://";
    const char *relative = path;

    if (!path) {
        return path;
    }

    if (strncmp(relative, prefix, sizeof(prefix) - 1) == 0) {
        relative += sizeof(prefix) - 1;
    } else {
        while (strncmp(relative, "./", 2) == 0) relative += 2;
        while (*relative == '/') relative++;
        // AndroidFileMgr strips the URI scheme before some POSIX calls.
        if (strncmp(relative, "res/", 4) != 0) return path;
    }

    while (*relative == '/') relative++;

    int length = snprintf(translated, PATH_MAX, "%sassets/%s",
                          DATA_PATH, relative);
    if (length < 0 || length >= PATH_MAX) {
        l_warn("Bundle path is too long: %s", path);
        return path;
    }

    l_debug("Translated %s to %s", path, translated);
    return translated;
}

FILE * fopen_soloader(const char * filename, const char * mode) {
    if (strcmp(filename, "/proc/cpuinfo") == 0) {
        return fopen_soloader("app0:/cpuinfo", mode);
    } else if (strcmp(filename, "/proc/meminfo") == 0) {
        return fopen_soloader("app0:/meminfo", mode);
    }

    char translated[PATH_MAX];
    filename = translate_bundle_path(filename, translated);

#ifdef USE_SCELIBC_IO
    FILE* ret = sceLibcBridge_fopen(filename, mode);
#else
    FILE* ret = fopen(filename, mode);
#endif

    if (ret)
        l_debug("fopen(%s, %s): %p", filename, mode, ret);
    else
        l_warn("fopen(%s, %s): %p", filename, mode, ret);

    return ret;
}

int open_soloader(const char * path, int oflag, ...) {
    if (strcmp(path, "/proc/cpuinfo") == 0) {
        return open_soloader("app0:/cpuinfo", oflag);
    } else if (strcmp(path, "/proc/meminfo") == 0) {
        return open_soloader("app0:/meminfo", oflag);
    } else if (strcmp(path, "/dev/urandom") == 0) {
        return open_soloader("app0:/urandom", oflag);
    }

    char translated[PATH_MAX];
    path = translate_bundle_path(path, translated);

    mode_t mode = 0666;
    if (((oflag & BIONIC_O_CREAT) == BIONIC_O_CREAT) ||
        ((oflag & BIONIC_O_TMPFILE) == BIONIC_O_TMPFILE)) {
        va_list args;
        va_start(args, oflag);
        mode = (mode_t)(va_arg(args, int));
        va_end(args);
    }

    oflag = oflags_bionic_to_newlib(oflag);
    int ret = open(path, oflag, mode);
    if (ret >= 0)
        l_debug("open(%s, %x): %i", path, oflag, ret);
    else
        l_warn("open(%s, %x): %i", path, oflag, ret);
    return ret;
}

int fstat_soloader(int fd, stat64_bionic * buf) {
    struct stat st;
    int res = fstat(fd, &st);

    if (res == 0)
        stat_newlib_to_bionic(&st, buf);

    l_debug("fstat(%i): %i", fd, res);
    return res;
}

int stat_soloader(const char * path, stat64_bionic * buf) {
    if (strcmp(path, "/system/lib/libOpenSLES.so") == 0) {
        // FMOD checks both the return value and the file type before enabling
        // its OpenSL backend. Returning success with an untouched output
        // buffer makes that check fail nondeterministically.
        memset(buf, 0, sizeof(*buf));
        buf->st_mode = S_IFREG | 0555;
        buf->st_nlink = 1;
        buf->st_size = 1;
        port_trace("stat OpenSL probe: regular file reported");
        l_debug("stat(%s): reporting a regular OpenSLES library", path);
        return 0;
    }

    char translated[PATH_MAX];
    path = translate_bundle_path(path, translated);

    struct stat st;
    int res = stat(path, &st);

    if (res == 0)
        stat_newlib_to_bionic(&st, buf);

    l_debug("stat(%s): %i", path, res);
    return res;
}

int fclose_soloader(FILE * f) {
#ifdef USE_SCELIBC_IO
    int ret = sceLibcBridge_fclose(f);
#else
    int ret = fclose(f);
#endif

    l_debug("fclose(%p): %i", f, ret);
    return ret;
}

int close_soloader(int fd) {
    int ret = close(fd);
    l_debug("close(%i): %i", fd, ret);
    return ret;
}

DIR* opendir_soloader(char* _pathname) {
    char translated[PATH_MAX];
    const char *path = translate_bundle_path(_pathname, translated);
    DIR* ret = opendir(path);
    l_debug("opendir(\"%s\"): %p", path, ret);
    return ret;
}

int access_soloader(const char *path, int mode) {
    char translated[PATH_MAX];
    path = translate_bundle_path(path, translated);

    int ret = access(path, mode);
    if (ret == 0)
        l_debug("access(%s, %x): %i", path, mode, ret);
    else
        l_warn("access(%s, %x): %i", path, mode, ret);
    return ret;
}

struct dirent64_bionic * readdir_soloader(DIR * dir) {
    static struct dirent64_bionic dirent_tmp;

    struct dirent* ret = readdir(dir);
    l_debug("readdir(%p): %p", dir, ret);

    if (ret) {
        dirent64_bionic* entry_tmp = dirent_newlib_to_bionic(ret);
        memcpy(&dirent_tmp, entry_tmp, sizeof(dirent64_bionic));
        free(entry_tmp);
        return &dirent_tmp;
    }

    return NULL;
}

int readdir_r_soloader(DIR * dirp, dirent64_bionic * entry,
                       dirent64_bionic ** result) {
    struct dirent dirent_tmp;
    struct dirent * pdirent_tmp;

    int ret = readdir_r(dirp, &dirent_tmp, &pdirent_tmp);

    if (ret == 0) {
        dirent64_bionic* entry_tmp = dirent_newlib_to_bionic(&dirent_tmp);
        memcpy(entry, entry_tmp, sizeof(dirent64_bionic));
        *result = (pdirent_tmp != NULL) ? entry : NULL;
        free(entry_tmp);
    }

    l_debug("readdir_r(%p, %p, %p): %i", dirp, entry, result, ret);
    return ret;
}

int closedir_soloader(DIR * dir) {
    int ret = closedir(dir);
    l_debug("closedir(%p): %i", dir, ret);
    return ret;
}

int fcntl_soloader(int fd, int cmd, ...) {
    l_warn("fcntl(%i, %i, ...): not implemented", fd, cmd);
    return 0;
}

int ioctl_soloader(int fd, int request, ...) {
    l_warn("ioctl(%i, %i, ...): not implemented", fd, request);
    return 0;
}

int fsync_soloader(int fd) {
    int ret = fsync(fd);
    l_debug("fsync(%i): %i", fd, ret);
    return ret;
}
