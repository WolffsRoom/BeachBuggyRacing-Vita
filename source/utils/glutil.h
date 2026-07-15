/*
 * Copyright (C) 2021      Andy Nguyen
 * Copyright (C) 2021      Rinnegatamante
 * Copyright (C) 2022-2023 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

/**
 * @file  glutil.h
 * @brief OpenGL API initializer, related functions.
 */

#ifndef SOLOADER_GLUTIL_H
#define SOLOADER_GLUTIL_H

#include <vitaGL.h>

#ifdef __cplusplus
extern "C" {
#endif

void gl_init();

void gl_preload();

void gl_swap();

void glCompileShader_soloader(GLuint shader);
const GLubyte *glGetString_soloader(GLenum name);

GLuint glCreateProgram_soloader(void);
void glDeleteProgram_soloader(GLuint program);
GLuint glCreateShader_soloader(GLenum shader_type);
void glDeleteShader_soloader(GLuint shader);
void glAttachShader_soloader(GLuint program, GLuint shader);
void glBindAttribLocation_soloader(GLuint program, GLuint index,
                                   const GLchar *name);
void glLinkProgram_soloader(GLuint program);
void glGetProgramiv_soloader(GLuint program, GLenum pname, GLint *params);

void glShaderSource_soloader(GLuint shader, GLsizei count,
                             const GLchar **string, const GLint *_length);

#ifdef __cplusplus
};
#endif

#endif // SOLOADER_GLUTIL_H
