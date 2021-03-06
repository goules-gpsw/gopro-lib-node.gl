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

#include <string.h>

#include "gtimer.h"
#include "log.h"
#include "memory.h"

static void noop(const struct glcontext *gl, ...)
{
}

int ngli_gtimer_init(struct gtimer *s, struct ngl_ctx *ctx)
{
    struct glcontext *gl = ctx->glcontext;

    s->ctx = ctx;

    if (gl->features & NGLI_FEATURE_TIMER_QUERY) {
        s->glGenQueries          = ngli_glGenQueries;
        s->glDeleteQueries       = ngli_glDeleteQueries;
        s->glBeginQuery          = ngli_glBeginQuery;
        s->glEndQuery            = ngli_glEndQuery;
        s->glGetQueryObjectui64v = ngli_glGetQueryObjectui64v;
    } else if (gl->features & NGLI_FEATURE_EXT_DISJOINT_TIMER_QUERY) {
        s->glGenQueries          = ngli_glGenQueriesEXT;
        s->glDeleteQueries       = ngli_glDeleteQueriesEXT;
        s->glBeginQuery          = ngli_glBeginQueryEXT;
        s->glEndQuery            = ngli_glEndQueryEXT;
        s->glGetQueryObjectui64v = ngli_glGetQueryObjectui64vEXT;
    } else {
        s->glGenQueries          = (void *)noop;
        s->glDeleteQueries       = (void *)noop;
        s->glBeginQuery          = (void *)noop;
        s->glEndQuery            = (void *)noop;
        s->glGetQueryObjectui64v = (void *)noop;
    }

    s->glGenQueries(gl, 1, &s->query);
    return 0;
}

int ngli_gtimer_start(struct gtimer *s)
{
    struct ngl_ctx *ctx = s->ctx;
    struct glcontext *gl = ctx->glcontext;

    if (ctx->timer_active) {
        LOG(WARNING, "only one instance of GPU timings can be present "
            "in the same graph due to OpenGL limitations");
        return 0;
    }

    /*
     * This specific instance of gtimer was able to grab the global
     * "timer active" lock
     */
    ctx->timer_active = 1;
    s->started = 1;
    s->query_result = 0;
    s->glBeginQuery(gl, GL_TIME_ELAPSED, s->query);
    return 0;
}

int ngli_gtimer_stop(struct gtimer *s)
{
    struct ngl_ctx *ctx = s->ctx;
    struct glcontext *gl = ctx->glcontext;

    if (s->started) {
        s->glEndQuery(gl, GL_TIME_ELAPSED);
        s->glGetQueryObjectui64v(gl, s->query, GL_QUERY_RESULT, &s->query_result);
        s->started = 0;
        ctx->timer_active = 0;
    }
    return 0;
}

int64_t ngli_gtimer_read(struct gtimer *s)
{
    return s->query_result;
}

void ngli_gtimer_reset(struct gtimer *s)
{
    if (!s->ctx)
        return;

    struct glcontext *gl = s->ctx->glcontext;
    s->glDeleteQueries(gl, 1, &s->query);
    memset(s, 0, sizeof(*s));
}
