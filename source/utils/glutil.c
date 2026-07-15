/*
 * Copyright (C) 2021      Andy Nguyen
 * Copyright (C) 2021      Rinnegatamante
 * Copyright (C) 2022-2023 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include "utils/glutil.h"

#include "utils/utils.h"
#include "utils/dialog.h"
#include "utils/logger.h"

#include <stdio.h>
#include <malloc.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/io/stat.h>

// Helpers for our handling of shaders
GLboolean skip_next_compile = GL_FALSE;
char next_shader_fname[256];
void load_shader(GLuint shader, const char * string, size_t length);
extern void port_trace(const char *format, ...);

#ifdef DUMP_COMPILED_SHADERS
#define SHADER_CACHE_BINARY_CAPACITY (64 * 1024)
static unsigned shader_cache_hits;
static unsigned shader_cache_misses;
static unsigned shader_cache_writes;
static unsigned shader_cache_failures;
static unsigned shader_cache_exports;

static void trace_shader_cache_counter(const char *event, unsigned count,
                                       const char *path, size_t bytes) {
    if (count <= 8 || (count % 64) == 0) {
        port_trace("GL cache: %s #%u bytes=%u path=%s", event, count,
                   (unsigned)bytes, path ? path : "(none)");
    }
}
#endif

/* VitaGL's postponed GLSL path expects a vertex shader to already be attached
 * when glBindAttribLocation is called. GLES2 applications are allowed to bind
 * attribute locations at any time before linking, and Purple does so before
 * glAttachShader. Keep the requests here and replay them immediately before
 * glLinkProgram, after Purple has attached both shaders. */
#define MAX_DEFERRED_ATTRIB_BINDS 256
#define MAX_DEFERRED_ATTRIB_NAME 128
#define MAX_TRACKED_SHADERS 512
#define MAX_TRACKED_PROGRAMS 256

typedef struct deferred_attrib_bind {
    GLboolean used;
    GLuint program;
    GLuint index;
    GLchar name[MAX_DEFERRED_ATTRIB_NAME];
} deferred_attrib_bind;

static deferred_attrib_bind deferred_attrib_binds[MAX_DEFERRED_ATTRIB_BINDS];
static char *tracked_shader_sources[MAX_TRACKED_SHADERS + 1];
static GLenum tracked_shader_types[MAX_TRACKED_SHADERS + 1];
static GLuint tracked_shader_first_program[MAX_TRACKED_SHADERS + 1];
static GLuint tracked_vertex_shaders[MAX_TRACKED_PROGRAMS + 1];
static GLuint tracked_fragment_shaders[MAX_TRACKED_PROGRAMS + 1];
static GLuint tracked_vertex_clones[MAX_TRACKED_PROGRAMS + 1];
static GLuint tracked_fragment_clones[MAX_TRACKED_PROGRAMS + 1];

#ifdef DUMP_COMPILED_SHADERS
/* In VitaGL postponed mode the GXP does not exist after glCompileShader; it
 * is produced while glLinkProgram translates the complete program.  Keep one
 * pending cache path per shader and export only after a successful link. */
static char tracked_shader_cache_paths[MAX_TRACKED_SHADERS + 1][256];
static uint8_t shader_cache_binary_buffer[SHADER_CACHE_BINARY_CAPACITY]
    __attribute__((aligned(64)));
static void cache_shader_binary_after_link(GLuint shader);
#endif

static GLboolean is_identifier_character(char c) {
    return c == '_' || isalnum((unsigned char)c);
}

static GLboolean shader_source_has_identifier(const char *source,
                                               const char *name) {
    if (!source || !name || !name[0]) {
        return GL_FALSE;
    }

    const size_t name_length = strlen(name);
    const char *match = source;
    while ((match = strstr(match, name)) != NULL) {
        const char before = match == source ? '\0' : match[-1];
        const char after = match[name_length];
        if (!is_identifier_character(before) &&
            !is_identifier_character(after)) {
            return GL_TRUE;
        }
        match += name_length;
    }
    return GL_FALSE;
}

/* Purple emits a few tiny shaders through a generic material generator.  The
 * VitaGL GLSL translator used by the softfp package crashes while parsing the
 * local lowp texture-color temporary below.  Replacing it with the equivalent
 * single expression keeps the shader result unchanged and avoids that parser
 * path.  The caller reserves a small amount of headroom so compatibility
 * qualifiers can also be inserted safely. */
static GLboolean replace_source_fragment(char *source, size_t *length,
                                         size_t capacity,
                                         const char *needle,
                                         const char *replacement) {
    char *position = strstr(source, needle);
    if (!position) {
        return GL_FALSE;
    }

    const size_t needle_length = strlen(needle);
    const size_t replacement_length = strlen(replacement);
    const size_t new_length = *length - needle_length + replacement_length;
    if (new_length + 1 > capacity) {
        return GL_FALSE;
    }

    const size_t tail_length = strlen(position + needle_length) + 1;
    memmove(position + replacement_length, position + needle_length,
            tail_length);
    memcpy(position, replacement, replacement_length);
    *length = new_length;
    return GL_TRUE;
}

static void clear_tracked_shader(GLuint shader) {
    if (shader == 0 || shader > MAX_TRACKED_SHADERS) {
        return;
    }
    free(tracked_shader_sources[shader]);
    tracked_shader_sources[shader] = NULL;
    tracked_shader_types[shader] = 0;
    tracked_shader_first_program[shader] = 0;
#ifdef DUMP_COMPILED_SHADERS
    tracked_shader_cache_paths[shader][0] = '\0';
#endif
}

static GLboolean track_shader_source_copy(GLuint shader, GLenum shader_type,
                                          const char *source,
                                          size_t length) {
    if (shader == 0 || shader > MAX_TRACKED_SHADERS || !source) {
        return GL_FALSE;
    }

    clear_tracked_shader(shader);
    tracked_shader_sources[shader] = malloc(length + 1);
    if (!tracked_shader_sources[shader]) {
        return GL_FALSE;
    }
    memcpy(tracked_shader_sources[shader], source, length);
    tracked_shader_sources[shader][length] = '\0';
    tracked_shader_types[shader] = shader_type;
    return GL_TRUE;
}

static void clear_deferred_attrib_binds(GLuint program) {
    for (unsigned i = 0; i < MAX_DEFERRED_ATTRIB_BINDS; ++i) {
        if (deferred_attrib_binds[i].used &&
            deferred_attrib_binds[i].program == program) {
            deferred_attrib_binds[i].used = GL_FALSE;
        }
    }
}

GLuint glCreateProgram_soloader(void) {
    GLuint program = glCreateProgram();
    if (program != 0) {
        clear_deferred_attrib_binds(program);
        if (program <= MAX_TRACKED_PROGRAMS) {
            tracked_vertex_shaders[program] = 0;
            tracked_fragment_shaders[program] = 0;
            tracked_vertex_clones[program] = 0;
            tracked_fragment_clones[program] = 0;
        }
    }
    port_trace("GL: glCreateProgram -> %u", program);
    return program;
}

void glDeleteProgram_soloader(GLuint program) {
    port_trace("GL: glDeleteProgram program=%u", program);
    if (program == 0) {
        return;
    }
    GLuint vertex_clone = 0;
    GLuint fragment_clone = 0;
    clear_deferred_attrib_binds(program);
    if (program <= MAX_TRACKED_PROGRAMS) {
        vertex_clone = tracked_vertex_clones[program];
        fragment_clone = tracked_fragment_clones[program];
        tracked_vertex_shaders[program] = 0;
        tracked_fragment_shaders[program] = 0;
        tracked_vertex_clones[program] = 0;
        tracked_fragment_clones[program] = 0;
    }
    glDeleteProgram(program);
    if (vertex_clone != 0) {
        glDeleteShader(vertex_clone);
        clear_tracked_shader(vertex_clone);
    }
    if (fragment_clone != 0 && fragment_clone != vertex_clone) {
        glDeleteShader(fragment_clone);
        clear_tracked_shader(fragment_clone);
    }
}

GLuint glCreateShader_soloader(GLenum shader_type) {
    GLuint shader = glCreateShader(shader_type);
    if (shader <= MAX_TRACKED_SHADERS) {
        clear_tracked_shader(shader);
        tracked_shader_types[shader] = shader_type;
    }
    port_trace("GL: glCreateShader type=0x%x -> %u", shader_type, shader);
    return shader;
}

void glDeleteShader_soloader(GLuint shader) {
    port_trace("GL: glDeleteShader shader=%u", shader);
    clear_tracked_shader(shader);
    if (shader != 0) {
        glDeleteShader(shader);
    }
}

void glAttachShader_soloader(GLuint program, GLuint shader) {
    port_trace("GL: glAttachShader program=%u shader=%u", program, shader);
    if (program == 0 || shader == 0) {
        port_trace("GL: glAttachShader ignored invalid zero handle");
        return;
    }
    const GLboolean tracked_source =
        program <= MAX_TRACKED_PROGRAMS &&
        shader <= MAX_TRACKED_SHADERS &&
        tracked_shader_sources[shader] &&
        (tracked_shader_types[shader] == GL_VERTEX_SHADER ||
         tracked_shader_types[shader] == GL_FRAGMENT_SHADER);
    const GLboolean reused_shader =
        tracked_source && tracked_shader_first_program[shader] != 0 &&
        tracked_shader_first_program[shader] != program;

    if (reused_shader) {
        const GLenum shader_type = tracked_shader_types[shader];
        const char *source = tracked_shader_sources[shader];
        const GLint source_length = (GLint)strlen(source);
        GLuint clone = glCreateShader(shader_type);
        if (clone != 0 && clone <= MAX_TRACKED_SHADERS &&
            track_shader_source_copy(clone, shader_type, source,
                                     (size_t)source_length)) {
            const GLchar *clone_source = tracked_shader_sources[clone];
            tracked_shader_first_program[clone] = program;
            /* Use the same persistent binary cache as ordinary shaders.
             * Purple reuses a number of shader objects across programs and
             * this compatibility layer clones them to isolate VitaGL state.
             * Routing clones through load_shader avoids translating the same
             * source again during the first run and on later launches. */
            load_shader(clone, clone_source, (size_t)source_length);
            glCompileShader_soloader(clone);
            glAttachShader(program, clone);

            GLuint *clone_slot;
            if (shader_type == GL_VERTEX_SHADER) {
                clone_slot = &tracked_vertex_clones[program];
                tracked_vertex_shaders[program] = clone;
            } else {
                clone_slot = &tracked_fragment_clones[program];
                tracked_fragment_shaders[program] = clone;
            }
            const GLuint old_clone = *clone_slot;
            *clone_slot = clone;
            if (old_clone != 0 && old_clone != clone) {
                glDeleteShader(old_clone);
                clear_tracked_shader(old_clone);
            }

            port_trace("GL: cloned shader program=%u original=%u clone=%u type=0x%x",
                       program, shader, clone, shader_type);
            port_trace("GL: glAttachShader completed program=%u shader=%u clone=%u",
                       program, shader, clone);
            return;
        }
        if (clone != 0) {
            glDeleteShader(clone);
            clear_tracked_shader(clone);
        }
        port_trace("GL: shader clone failed; attaching original=%u", shader);
    }
    if (tracked_source && tracked_shader_first_program[shader] == 0) {
        tracked_shader_first_program[shader] = program;
        port_trace("GL: first shader attachment program=%u shader=%u",
                   program, shader);
    }
    if (program <= MAX_TRACKED_PROGRAMS &&
        shader <= MAX_TRACKED_SHADERS) {
        if (tracked_shader_types[shader] == GL_VERTEX_SHADER) {
            tracked_vertex_shaders[program] = shader;
        } else if (tracked_shader_types[shader] == GL_FRAGMENT_SHADER) {
            tracked_fragment_shaders[program] = shader;
        }
    }
    glAttachShader(program, shader);
    port_trace("GL: glAttachShader completed program=%u shader=%u",
               program, shader);
}

void glBindAttribLocation_soloader(GLuint program, GLuint index,
                                   const GLchar *name) {
    port_trace("GL: glBindAttribLocation deferred program=%u index=%u name=%s",
               program, index, name ? name : "(null)");
    if (program == 0 || !name) {
        port_trace("GL: glBindAttribLocation ignored invalid arguments");
        return;
    }

    for (unsigned i = 0; i < MAX_DEFERRED_ATTRIB_BINDS; ++i) {
        if (!deferred_attrib_binds[i].used) {
            deferred_attrib_binds[i].used = GL_TRUE;
            deferred_attrib_binds[i].program = program;
            deferred_attrib_binds[i].index = index;
            strncpy(deferred_attrib_binds[i].name, name,
                    MAX_DEFERRED_ATTRIB_NAME - 1);
            deferred_attrib_binds[i].name[MAX_DEFERRED_ATTRIB_NAME - 1] = '\0';
            return;
        }
    }

    port_trace("GL: deferred attribute table full; binding was not queued");
}

void glLinkProgram_soloader(GLuint program) {
    unsigned replayed = 0;
    unsigned skipped = 0;
    const char *vertex_source = NULL;
    port_trace("GL: glLinkProgram program=%u enter", program);
    if (program == 0) {
        port_trace("GL: glLinkProgram ignored invalid zero handle");
        return;
    }

    if (program <= MAX_TRACKED_PROGRAMS) {
        const GLuint vertex_shader = tracked_vertex_shaders[program];
        if (vertex_shader != 0 && vertex_shader <= MAX_TRACKED_SHADERS) {
            vertex_source = tracked_shader_sources[vertex_shader];
        }
    }

    for (unsigned i = 0; i < MAX_DEFERRED_ATTRIB_BINDS; ++i) {
        deferred_attrib_bind *bind = &deferred_attrib_binds[i];
        if (!bind->used || bind->program != program) {
            continue;
        }

        if (vertex_source &&
            !shader_source_has_identifier(vertex_source, bind->name)) {
            port_trace("GL: skip inactive attrib program=%u index=%u name=%s",
                       program, bind->index, bind->name);
            bind->used = GL_FALSE;
            ++skipped;
            continue;
        }

        port_trace("GL: replay attrib program=%u index=%u name=%s",
                   program, bind->index, bind->name);
        glBindAttribLocation(program, bind->index, bind->name);
        bind->used = GL_FALSE;
        ++replayed;
    }

    port_trace("GL: glLinkProgram program=%u replayed=%u skipped=%u",
               program, replayed, skipped);
    glLinkProgram(program);
    port_trace("GL: glLinkProgram program=%u native link returned", program);
#if defined(USE_GLSL_SHADERS) && defined(DUMP_COMPILED_SHADERS)
    /* VGL_MODE_POSTPONED performs the expensive GLSL translation during
     * glLinkProgram.  Export only now, after the program has linked. */
    GLint cache_link_status = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &cache_link_status);
    if (cache_link_status == GL_TRUE && program <= MAX_TRACKED_PROGRAMS) {
        cache_shader_binary_after_link(tracked_vertex_shaders[program]);
        cache_shader_binary_after_link(tracked_fragment_shaders[program]);
    }
#endif
}

void glGetProgramiv_soloader(GLuint program, GLenum pname, GLint *params) {
    port_trace("GL: glGetProgramiv program=%u pname=0x%x", program, pname);
    if (program == 0 || !params) {
        if (params) {
            *params = GL_FALSE;
        }
        port_trace("GL: glGetProgramiv ignored invalid arguments");
        return;
    }
    glGetProgramiv(program, pname, params);
    port_trace("GL: glGetProgramiv program=%u pname=0x%x -> %d",
               program, pname, *params);
}

void gl_preload() {
    if (!file_exists("ur0:/data/libshacccg.suprx")
        && !file_exists("ur0:/data/external/libshacccg.suprx")) {
        fatal_error("Error: libshacccg.suprx is not installed. "
                    "Google \"ShaRKBR33D\" for quick installation.");
    }

#ifdef USE_GLSL_SHADERS
    vglSetSemanticBindingMode(VGL_MODE_POSTPONED);
#endif
}

void gl_init() {
    /* Purple compiles its own splash shaders immediately after EGL setup.
     * Let VitaGL's asynchronous splash/initialization worker finish first;
     * ShaccCg is not safe when two startup paths enter it concurrently. */
    vglInitExtended(0, 960, 544, 6 * 1024 * 1024,
                    SCE_GXM_MULTISAMPLE_NONE);
    port_trace("GL: VitaGL init returned; waiting for startup worker");
    sceKernelDelayThread(4000 * 1000);
    port_trace("GL: startup serialization delay completed");
}

void gl_swap() {
    vglSwapBuffers(GL_FALSE);
}

/* Purple queries the GL identity immediately after eglMakeCurrent.  The v6
 * core stopped in that transition before the engine could draw its splash.
 * Return immutable GLES2 capability strings without entering VitaGL's
 * information path during its own splashscreen/GC startup. */
const GLubyte *glGetString_soloader(GLenum name) {
    const char *value;
    switch (name) {
        case GL_VENDOR:
            value = "VitaGL";
            break;
        case GL_RENDERER:
            value = "PowerVR SGX 543MP4+ (VitaGL)";
            break;
        case GL_VERSION:
            value = "OpenGL ES 2.0 VitaGL";
            break;
        case GL_SHADING_LANGUAGE_VERSION:
            value = "OpenGL ES GLSL ES 1.00";
            break;
        case GL_EXTENSIONS:
            value =
                "GL_OES_compressed_ETC1_RGB8_texture "
                "GL_IMG_texture_compression_pvrtc "
                "GL_OES_depth24 "
                "GL_OES_depth_texture "
                "GL_OES_element_index_uint "
                "GL_OES_framebuffer_object "
                "GL_OES_mapbuffer "
                "GL_OES_packed_depth_stencil "
                "GL_OES_rgb8_rgba8 "
                "GL_OES_standard_derivatives "
                "GL_EXT_blend_minmax "
                "GL_EXT_blend_subtract "
                "GL_EXT_discard_framebuffer "
                "GL_EXT_texture_filter_anisotropic";
            break;
        default:
            value = "";
            break;
    }
    port_trace("GL: glGetString name=0x%x -> %.48s", name, value);
    return (const GLubyte *)value;
}

void glShaderSource_soloader(GLuint shader, GLsizei count,
                             const GLchar **string, const GLint *_length) {
    static unsigned source_call;
    unsigned call = ++source_call;
    port_trace("GL: glShaderSource #%u shader=%u count=%d lengths=%p",
               call, shader, count, _length);
#ifdef DEBUG_OPENGL
    sceClibPrintf("[gl_dbg] glShaderSource<%p>(shader: %i, count: %i, string: %p, length: %p)\n", __builtin_return_address(0), shader, count, string, _length);
#endif
    if (!string) {
        l_error("<%p> Shader source string is NULL, count: %i",
                   __builtin_return_address(0), count);
        skip_next_compile = GL_TRUE;
        return;
    } else if (!*string) {
        l_error("<%p> Shader source *string is NULL, count: %i",
                   __builtin_return_address(0), count);
        skip_next_compile = GL_TRUE;
        return;
    }

    size_t total_length = 0;

    for (int i = 0; i < count; ++i) {
        if (!_length) {
            total_length += strlen(string[i]);
        } else {
            total_length += _length[i];
        }
    }

    /* Keep room for the compatibility rewrites inserted below. */
    const size_t source_capacity = total_length + 512 + 1;
    char * str = malloc(source_capacity);
    if (!str) {
        l_error("<%p> Could not allocate %u bytes for shader source",
                __builtin_return_address(0), (unsigned)source_capacity);
        skip_next_compile = GL_TRUE;
        return;
    }
    size_t l = 0;

    for (int i = 0; i < count; ++i) {
        if (!_length) {
            memcpy(str + l, string[i], strlen(string[i]));
            l += strlen(string[i]);
        } else {
            memcpy(str + l, string[i], _length[i]);
            l += _length[i];
        }
    }
    str[total_length] = '\0';

    static const char texture_color_temporary[] =
        "lowp vec4 color=texture2D(tex0,vUv);color*=vCol;"
        "gl_FragColor=color;";
    static const char texture_color_direct[] =
        "gl_FragColor=texture2D(tex0,vUv)*vCol;";
    if (replace_source_fragment(str, &total_length, source_capacity,
                                texture_color_temporary,
                                texture_color_direct)) {
        port_trace("GL: shader #%u rewrote texture-color temporary", call);
    }

    /* Purple's generated vertex-color shader leaves aColor at the default
     * mediump precision and sends it directly to a lowp varying.  Keep the
     * input and output precision matched so the softfp VitaGL/ShaRK path does
     * not produce an invalid GXM program for this valid GLES conversion. */
    static const char vertex_color_default[] =
        "attribute vec4 aColor;varying lowp vec4 vCol;";
    static const char vertex_color_lowp[] =
        "attribute lowp vec4 aColor;varying lowp vec4 vCol;";
    if (replace_source_fragment(str, &total_length, source_capacity,
                                vertex_color_default,
                                vertex_color_lowp)) {
        port_trace("GL: shader #%u normalized aColor to lowp", call);
    }

    /* One Purple material combines the model-view-projection transform and
     * vertex color in a very small vertex shader.  The softfp VitaGL GLSL
     * translator deterministically walks past its parser state on that exact
     * statement sequence.  Program 3 uses the same operations followed by a
     * scale and offset and translates correctly, so express identity scale
     * and offset explicitly here.  This preserves every output value while
     * keeping the translator on its known-good path. */
    static const char mvp_vertex_color_compact[] =
        "gl_Position=gModelViewProjMatrix*aPosition;vCol=aColor;";
    static const char mvp_vertex_color_safe[] =
        "gl_Position=gModelViewProjMatrix*aPosition;"
        "gl_Position*=vec4(1,1,1,1);"
        "gl_Position+=vec4(0,0,0,0);"
        "vCol=aColor;";
    if (replace_source_fragment(str, &total_length, source_capacity,
                                mvp_vertex_color_compact,
                                mvp_vertex_color_safe)) {
        port_trace("GL: shader #%u normalized MVP vertex-color path", call);
    }

    /* A later v20 run reached the compact uniform-color MVP program and
     * crashed inside the same VitaGL parser before glLinkProgram returned
     * (R0=0x29, R3=0x28, PC=0x811C5F58).  The exact shader body performs the
     * same two-statement sequence as the former program 7 failure, except the
     * color comes from a uniform.  Route only this complete body through the
     * identity scale/offset sequence already proven by the earlier fixes.
     * Keeping the full main body in the match avoids touching the textured
     * uniform-color shader used by the following program. */
    static const char compact_mvp_uniform_color_body[] =
        "void main(void){"
        "gl_Position=gModelViewProjMatrix*aPosition;"
        "vCol=gColor;"
        "}";
    static const char compact_mvp_uniform_color_safe[] =
        "void main(void){"
        "gl_Position=gModelViewProjMatrix*aPosition;"
        "gl_Position*=vec4(1,1,1,1);"
        "gl_Position+=vec4(0,0,0,0);"
        "vCol=gColor;"
        "}";
    if (replace_source_fragment(str, &total_length, source_capacity,
                                compact_mvp_uniform_color_body,
                                compact_mvp_uniform_color_safe)) {
        port_trace("GL: shader #%u normalized compact MVP uniform-color path", call);
    }

    /* Program 9 in the v18 trace is a depth/post-process vertex shader with
     * three matrix-vector products.  VitaGL translated it successfully but its
     * internal parser walked one element past the final statement while
     * linking (R0=0x189, R3=0x188, PC=0x811C5ED8).  Remove the redundant
     * vPosModel alias and route gl_Position through the identity scale/offset
     * sequence already proven by programs 3 and 7.  The replacement is
     * restricted to the complete exact body so unrelated materials remain
     * untouched. */
    static const char multi_matrix_depth_compact[] =
        "vec4 vPosModel=aPosition;"
        "vec4 worldPos=gModelMatrix*vPosModel;"
        "gl_Position=gViewProjMatrix*worldPos;"
        "vPixel=gViewMatrix*worldPos;"
        "vTexCoord=aTexCoord;";
    static const char multi_matrix_depth_safe[] =
        "vec4 worldPos=gModelMatrix*aPosition;"
        "gl_Position=gViewProjMatrix*worldPos;"
        "gl_Position*=vec4(1,1,1,1);"
        "gl_Position+=vec4(0,0,0,0);"
        "vPixel=gViewMatrix*worldPos;"
        "vTexCoord=aTexCoord;";
    if (replace_source_fragment(str, &total_length, source_capacity,
                                multi_matrix_depth_compact,
                                multi_matrix_depth_safe)) {
        port_trace("GL: shader #%u normalized program 9 multi-matrix path", call);
    }

    /* The matching depth fragment shader writes only gl_FragColor.r.
     * That masked component assignment compiles but crashes the VitaGL GLSL
     * linker on this generated material.  Emit the same red depth value with
     * explicit zero/one values for the remaining channels.  This is exact for
     * the original shader and does not alter unrelated fragment programs. */
    static const char masked_depth_fragment_compact[] =
        "gl_FragColor.r=-vPixel.z/gNearFarPlanes.y;";
    static const char masked_depth_fragment_safe[] =
        "gl_FragColor=vec4(-vPixel.z/gNearFarPlanes.y,0.0,0.0,1.0);";
    if (replace_source_fragment(str, &total_length, source_capacity,
                                masked_depth_fragment_compact,
                                masked_depth_fragment_safe)) {
        port_trace("GL: shader #%u expanded masked depth gl_FragColor write", call);
    }

    const size_t payload_length = total_length;
#ifndef PERFORMANCE_BUILD
    port_trace("GL: glShaderSource #%u bytes=%u prefix=%.80s", call,
               (unsigned)payload_length, str);
    if (call <= 24 && payload_length < 1200) {
        port_trace("GL: glShaderSource #%u full=<<<%s>>>", call, str);
    }
#endif

    /* The v21 core still stopped in VitaGL's GLSL parser after the exact
     * program-5 rewrite had been applied.  The saved heap contains a complete
     * source string, while the faulting routine is scanning the first token
     * (registers contain "prec"/"isio").  Previous failures also moved when
     * harmless source length changed.  Give the postponed parser an explicit
     * trailing-whitespace guard inside the length supplied to glShaderSource.
     * GLSL ignores this whitespace, but word-at-a-time token lookahead remains
     * inside the copied allocation instead of landing immediately on its end.
     * Track the padded form too so shader clones receive the same guard. */
    enum { GLSL_EOF_GUARD_BYTES = 64 };
    if (total_length + GLSL_EOF_GUARD_BYTES + 1 <= source_capacity) {
        str[total_length++] = '\n';
        memset(str + total_length, ' ', GLSL_EOF_GUARD_BYTES - 2);
        total_length += GLSL_EOF_GUARD_BYTES - 2;
        str[total_length++] = '\n';
        str[total_length] = '\0';
        port_trace("GL: shader #%u appended GLSL EOF guard payload=%u total=%u",
                   call, (unsigned)payload_length, (unsigned)total_length);
    } else {
        port_trace("GL: shader #%u could not append GLSL EOF guard", call);
    }

    if (shader != 0 && shader <= MAX_TRACKED_SHADERS) {
        track_shader_source_copy(shader, tracked_shader_types[shader], str,
                                 total_length);
    }

    load_shader(shader, str, total_length);

    port_trace("GL: glShaderSource #%u completed", call);

    free(str);
}

void glCompileShader_soloader(GLuint shader) {
    static unsigned compile_call;
    unsigned call = ++compile_call;
    port_trace("GL: glCompileShader #%u shader=%u enter", call, shader);
#ifdef DEBUG_OPENGL
    sceClibPrintf("[gl_dbg] glCompileShader<%p>(shader: %i)\n", __builtin_return_address(0), shader);
#endif

#ifndef USE_GXP_SHADERS
    if (!skip_next_compile) {
        glCompileShader(shader);
        port_trace("GL: glCompileShader #%u native compile returned", call);
    }
    /* A cache hit already supplied a compiled GXP with glShaderBinary. */
    skip_next_compile = GL_FALSE;
#endif
    port_trace("GL: glCompileShader #%u leave", call);
}

#if defined(USE_GLSL_SHADERS) && defined(DUMP_COMPILED_SHADERS)
static void cache_shader_binary_after_link(GLuint shader) {
    if (shader == 0 || shader > MAX_TRACKED_SHADERS ||
        tracked_shader_cache_paths[shader][0] == '\0') {
        return;
    }

    char path[256];
    snprintf(path, sizeof(path), "%s", tracked_shader_cache_paths[shader]);
    /* Clear before entering VitaGL so a failure cannot cause repeated export
     * attempts on a later program that reuses this shader object. */
    tracked_shader_cache_paths[shader][0] = '\0';

    const unsigned export_no = ++shader_cache_exports;
    trace_shader_cache_counter("export-enter", export_no, path, 0);
    GLsizei len = 0;
    vglGetShaderBinary(shader, SHADER_CACHE_BINARY_CAPACITY, &len,
                       shader_cache_binary_buffer);
    trace_shader_cache_counter("export-return", export_no, path,
                               len > 0 ? (size_t)len : 0);
    if (len > 0 && len <= SHADER_CACHE_BINARY_CAPACITY &&
        file_mkpath(path, 0777) &&
        file_save(path, shader_cache_binary_buffer, (size_t)len)) {
        ++shader_cache_writes;
        trace_shader_cache_counter("write-after-link", shader_cache_writes,
                                   path, (size_t)len);
    } else {
        ++shader_cache_failures;
        trace_shader_cache_counter("export-failed", shader_cache_failures,
                                   path, len > 0 ? (size_t)len : 0);
    }
}

void load_shader(GLuint shader, const char * string, size_t length) {
    if (shader != 0 && shader <= MAX_TRACKED_SHADERS) {
        tracked_shader_cache_paths[shader][0] = '\0';
    }

    char *sha_name = str_sha1sum(string, length);
    if (!sha_name) {
        const GLint source_length = (GLint)length;
        glShaderSource(shader, 1, &string, &source_length);
        ++shader_cache_failures;
        trace_shader_cache_counter("hash-failed", shader_cache_failures,
                                   NULL, 0);
        return;
    }

    const GLenum shader_type =
        (shader <= MAX_TRACKED_SHADERS) ? tracked_shader_types[shader] : 0;
    const char stage = shader_type == GL_FRAGMENT_SHADER ? 'f' : 'v';

    char gxp_path[256];
    snprintf(gxp_path, sizeof(gxp_path),
             DATA_PATH"gxp-v30/%c-%s.gxp", stage, sha_name);

    GLboolean cache_loaded = GL_FALSE;
    if (file_exists(gxp_path)) {
        uint8_t *buffer = NULL;
        size_t size = 0;
        if (file_load(gxp_path, &buffer, &size) &&
            size > 0 && size <= SHADER_CACHE_BINARY_CAPACITY) {
            glShaderBinary(1, &shader, 0, buffer, (int32_t)size);
            cache_loaded = GL_TRUE;
            skip_next_compile = GL_TRUE;
            ++shader_cache_hits;
            trace_shader_cache_counter("hit", shader_cache_hits,
                                       gxp_path, size);
        } else {
            ++shader_cache_failures;
            trace_shader_cache_counter("invalid", shader_cache_failures,
                                       gxp_path, size);
        }
        free(buffer);
    }

    if (!cache_loaded) {
        ++shader_cache_misses;
        trace_shader_cache_counter("deferred-miss", shader_cache_misses,
                                   gxp_path, 0);
        file_mkpath(gxp_path, 0777);
        const GLint source_length = (GLint)length;
        glShaderSource(shader, 1, &string, &source_length);
        if (shader != 0 && shader <= MAX_TRACKED_SHADERS) {
            snprintf(tracked_shader_cache_paths[shader],
                     sizeof(tracked_shader_cache_paths[shader]),
                     "%s", gxp_path);
        }
    }

    free(sha_name);
}
#elif defined(USE_GLSL_SHADERS)
void load_shader(GLuint shader, const char * string, size_t length) {
    glShaderSource(shader, 1, &string, &length);
}
#elif defined(USE_CG_SHADERS) && defined(DUMP_COMPILED_SHADERS)
void load_shader(GLuint shader, const char * string, size_t length) {
    char* sha_name = str_sha1sum(string, length);

    char gxp_path[256];
    char cg_path[256];
    snprintf(gxp_path, sizeof(gxp_path), DATA_PATH"gxp/%s.gxp", sha_name);
    snprintf(cg_path, sizeof(cg_path), DATA_PATH"cg/%s.cg", sha_name);

    if (file_exists(gxp_path)) {
        uint8_t *buffer;
        size_t size;

        file_load(gxp_path, &buffer, &size);

        glShaderBinary(1, &shader, 0, buffer, (int32_t) size);

        free(buffer);
        skip_next_compile = GL_TRUE;
    } else if (file_exists(cg_path)) {
        char *buffer;
        size_t size;

        file_load(cg_path, (uint8_t **) &buffer, &size);

        glShaderSource(shader, 1, &string, &size);
        strcpy(next_shader_fname, gxp_path);

        free(buffer);
        skip_next_compile = GL_FALSE;
    } else {
        l_warn("Encountered an untranslated shader %s, saving GLSL "
               "and using a dummy shader.", sha_name);

        char glsl_path[256];
        snprintf(glsl_path, sizeof(glsl_path), DATA_PATH"glsl/%s.glsl", sha_name);
        file_mkpath(glsl_path, 0777);
        file_save(glsl_path, (const uint8_t *) string, length);

        if (strstr(string, "gl_FragColor")) {
            const char *dummy_shader = "float4 main() { return float4(1.0,1.0,1.0,1.0); }";
            int32_t dummy_shader_len = (int32_t) strlen(dummy_shader);
            glShaderSource(shader, 1, &dummy_shader, &dummy_shader_len);
        } else {
            const char *dummy_shader = "void main(float4 out gl_Position : POSITION ) { gl_Position = float4(1.0,1.0,1.0,1.0); }";
            int32_t dummy_shader_len = (int32_t) strlen(dummy_shader);
            glShaderSource(shader, 1, &dummy_shader, &dummy_shader_len);
        }

        skip_next_compile = GL_FALSE;
    }

    free(sha_name);
}
#elif defined(USE_CG_SHADERS) || defined(USE_GXP_SHADERS)
void load_shader(GLuint shader, const char * string, size_t length) {
    char* sha_name = str_sha1sum(string, length);

    char path[256];
#ifdef USE_CG_SHADERS
    snprintf(path, sizeof(path), DATA_PATH"cg/%s.cg", sha_name);
#else
    snprintf(path, sizeof(path), DATA_PATH"gxp/%s.gxp", sha_name);
#endif

    if (file_exists(path)) {
#ifdef USE_CG_SHADERS
        char *buffer;
        size_t size;

        file_load(path, (uint8_t **) &buffer, &size);

        glShaderSource(shader, 1, &string, &size);

        free(buffer);
#else
        uint8_t *buffer;
        size_t size;

        file_load(path, &buffer, &size);

        glShaderBinary(1, &shader, 0, buffer, (int32_t) size);

        free(buffer);
#endif
    } else {
        l_warn("Encountered an untranslated shader %s, saving GLSL "
               "and using a dummy shader.", sha_name);

        char glsl_path[256];
        snprintf(glsl_path, sizeof(glsl_path), DATA_PATH"glsl/%s.glsl", sha_name);
        file_mkpath(glsl_path, 0777);
        file_save(glsl_path, (const uint8_t *) string, length);

        if (strstr(string, "gl_FragColor")) {
            const char *dummy_shader = "float4 main() { return float4(1.0,1.0,1.0,1.0); }";
            int32_t dummy_shader_len = (int32_t) strlen(dummy_shader);
            glShaderSource(shader, 1, &dummy_shader, &dummy_shader_len);
        } else {
            const char *dummy_shader = "void main(float4 out gl_Position : POSITION ) { gl_Position = float4(1.0,1.0,1.0,1.0); }";
            int32_t dummy_shader_len = (int32_t) strlen(dummy_shader);
            glShaderSource(shader, 1, &dummy_shader, &dummy_shader_len);
        }
    }

    free(sha_name);
}
#else
#error "Define one of (USE_GLSL_SHADERS, USE_CG_SHADERS, USE_GXP_SHADERS)"
#endif
