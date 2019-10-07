/*
 * Copyright 2018 GoPro Inc.
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

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sxplayer.h>

#include <libavcodec/mediacodec.h>

#include "android_surface.h"
#include "format.h"
#include "glincludes.h"
#include "hwupload.h"
#include "image.h"
#include "log.h"
#include "math_utils.h"
#include "nodegl.h"
#include "nodes.h"

static int mc_common_render_frame(struct ngl_node *node, struct sxplayer_frame *frame, float *matrix)
{
    struct texture_priv *s = node->priv_data;
    struct media_priv *media = s->data_src->priv_data;
    AVMediaCodecBuffer *buffer = (AVMediaCodecBuffer *)frame->data;

    NGLI_ALIGNED_MAT(flip_matrix) = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f,-1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 1.0f,
    };

    ngli_android_surface_render_buffer(media->android_surface, buffer, matrix);
    ngli_mat4_mul(matrix, matrix, flip_matrix);

    ngli_texture_set_dimensions(&media->android_texture, frame->width, frame->height, 0);

    return 0;
}

static int support_direct_rendering(struct ngl_node *node)
{
    struct texture_priv *s = node->priv_data;
    int direct_rendering = s->supported_image_layouts & (1 << NGLI_IMAGE_LAYOUT_MEDIACODEC);

    if (direct_rendering) {
        const struct texture_params *params = &s->params;

        if (params->mipmap_filter) {
            LOG(WARNING, "external textures do not support mipmapping: "
                "disabling direct rendering");
            direct_rendering = 0;
        } else if (params->wrap_s != NGLI_WRAP_CLAMP_TO_EDGE || params->wrap_t != NGLI_WRAP_CLAMP_TO_EDGE) {
            LOG(WARNING, "external textures only support clamp to edge wrapping: "
                "disabling direct rendering");
            direct_rendering = 0;
        }
    }

    return direct_rendering;
}

static int mc_dr_init(struct ngl_node *node, struct sxplayer_frame *frame)
{
    struct ngl_ctx *ctx = node->ctx;
    struct glcontext *gl = ctx->glcontext;
    struct texture_priv *s = node->priv_data;
    const struct texture_params *params = &s->params;
    struct media_priv *media = s->data_src->priv_data;

    GLint id = media->android_texture.id;
    GLenum target = media->android_texture.target;
    const GLint min_filter = ngli_texture_get_gl_min_filter(params->min_filter, params->mipmap_filter);
    const GLint mag_filter = ngli_texture_get_gl_mag_filter(params->mag_filter);

    ngli_glBindTexture(gl, target, id);
    ngli_glTexParameteri(gl, target, GL_TEXTURE_MIN_FILTER, min_filter);
    ngli_glTexParameteri(gl, target, GL_TEXTURE_MAG_FILTER, mag_filter);
    ngli_glBindTexture(gl, target, 0);

    ngli_image_init(&s->hwupload_mapped_image, NGLI_IMAGE_LAYOUT_MEDIACODEC, &media->android_texture);

    s->hwupload_require_hwconv = !support_direct_rendering(node);

    return 0;
}

static int mc_dr_map_frame(struct ngl_node *node, struct sxplayer_frame *frame)
{
    struct texture_priv *s = node->priv_data;

    int ret = mc_common_render_frame(node, frame, s->hwupload_mapped_image.coordinates_matrix);
    if (ret < 0)
        return ret;

    return 0;
}

const struct hwmap_class ngli_hwmap_mc_class = {
    .name      = "mediacodec (oes zero-copy)",
    .init      = mc_dr_init,
    .map_frame = mc_dr_map_frame,
};
