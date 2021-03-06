/*
 * Copyright 2020 GoPro Inc.
 *
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#ifndef GTIMER_H
#define GTIMER_H

#include <stdint.h>

#include "glincludes.h"
#include "nodes.h"

struct ngl_ctx;

struct gtimer {
    struct ngl_ctx *ctx;

    int started;
    GLuint query;
    GLuint64 query_result;
    void (*glGenQueries)(const struct glcontext *gl, GLsizei n, GLuint * ids);
    void (*glDeleteQueries)(const struct glcontext *gl, GLsizei n, const GLuint * ids);
    void (*glBeginQuery)(const struct glcontext *gl, GLenum target, GLuint id);
    void (*glEndQuery)(const struct glcontext *gl, GLenum target);
    void (*glGetQueryObjectui64v)(const struct glcontext *gl, GLuint id, GLenum pname, GLuint64 *params);
};

int ngli_gtimer_init(struct gtimer *s, struct ngl_ctx *ctx);
int ngli_gtimer_start(struct gtimer *s);
int ngli_gtimer_stop(struct gtimer *s);
int64_t ngli_gtimer_read(struct gtimer *s);
void ngli_gtimer_reset(struct gtimer *s);

#endif
