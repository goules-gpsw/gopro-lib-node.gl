/*
 * Copyright 2019 GoPro Inc.
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


#include "pipeline.h"
#include "gctx.h"

struct pipeline *ngli_pipeline_create(struct gctx *gctx)
{
    return gctx->class->pipeline_create(gctx);
}

int ngli_pipeline_init(struct pipeline *s, const struct pipeline_params *params)
{
    return s->gctx->class->pipeline_init(s, params);
}

int ngli_pipeline_update_uniform(struct pipeline *s, int index, const void *value)
{
    return s->gctx->class->pipeline_update_uniform(s, index, value);
}

int ngli_pipeline_update_texture(struct pipeline *s, int index, struct texture *texture)
{
    return s->gctx->class->pipeline_update_texture(s, index, texture);
}

void ngli_pipeline_exec(struct pipeline *s)
{
    s->gctx->class->pipeline_exec(s);
}

void ngli_pipeline_freep(struct pipeline **sp)
{
    if (!*sp)
        return;
    (*sp)->gctx->class->pipeline_freep(sp);
}
