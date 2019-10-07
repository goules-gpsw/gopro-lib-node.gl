/*
 * Copyright 2017 GoPro Inc.
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

#include "glincludes.h"
#include "hwupload.h"
#include "log.h"
#include "math_utils.h"
#include "memory.h"
#include "nodegl.h"
#include "nodes.h"

extern const struct hwmap_class ngli_hwmap_common_class;
extern const struct hwmap_class ngli_hwmap_mc_class;
extern const struct hwmap_class ngli_hwmap_vt_darwin_class;
extern const struct hwmap_class ngli_hwmap_vt_ios_class;
extern const struct hwmap_class ngli_hwmap_vaapi_class;

static const struct hwmap_class *hwupload_class_map[] = {
    [SXPLAYER_PIXFMT_RGBA]        = &ngli_hwmap_common_class,
    [SXPLAYER_PIXFMT_BGRA]        = &ngli_hwmap_common_class,
    [SXPLAYER_SMPFMT_FLT]         = &ngli_hwmap_common_class,
#if defined(TARGET_ANDROID)
    [SXPLAYER_PIXFMT_MEDIACODEC]  = &ngli_hwmap_mc_class,
#elif defined(TARGET_DARWIN)
    [SXPLAYER_PIXFMT_VT]          = &ngli_hwmap_vt_darwin_class,
#elif defined(TARGET_IPHONE)
    [SXPLAYER_PIXFMT_VT]          = &ngli_hwmap_vt_ios_class,
#elif defined(HAVE_VAAPI_X11)
    [SXPLAYER_PIXFMT_VAAPI]       = &ngli_hwmap_vaapi_class,
#endif
};

static const struct hwmap_class *get_hwmap_class(struct sxplayer_frame *frame)
{
    if (frame->pix_fmt < 0 || frame->pix_fmt >= NGLI_ARRAY_NB(hwupload_class_map))
        return NULL;

    return hwupload_class_map[frame->pix_fmt];
}

static int init_hwconv(struct ngl_node *node)
{
    struct ngl_ctx *ctx = node->ctx;
    struct texture_priv *s = node->priv_data;
    struct texture *texture = &s->texture;
    struct image *image = &s->image;
    struct image *mapped_image = &s->hwupload_mapped_image;
    struct hwconv *hwconv = &s->hwupload_hwconv;

    const int image_width = image->layout ? image->planes[0]->params.width : 0;
    const int image_height = image->layout ? image->planes[0]->params.height : 0;
    const int mapped_image_width = mapped_image->layout ? mapped_image->planes[0]->params.width : 0;
    const int mapped_image_height = mapped_image->layout ? mapped_image->planes[0]->params.height : 0;

    if (s->hwupload_hwconv_initialized     &&
        image_width  == mapped_image_width &&
        image_height == mapped_image_height)
        return 0;

    ngli_hwconv_reset(hwconv);
    ngli_image_reset(image);
    ngli_texture_reset(texture);

    LOG(DEBUG, "converting texture '%s' from %s to rgba", node->label, s->hwupload_map_class->name);

    struct texture_params params = s->params;
    params.format = NGLI_FORMAT_R8G8B8A8_UNORM;
    params.width  = mapped_image_width;
    params.height = mapped_image_height;

    int ret = ngli_texture_init(&s->texture, ctx, &params);
    if (ret < 0)
        goto end;

    ngli_image_init(&s->image, NGLI_IMAGE_LAYOUT_DEFAULT, &s->texture);

    ret = ngli_hwconv_init(hwconv, ctx, &s->texture, mapped_image->layout);
    if (ret < 0)
        goto end;

    s->hwupload_hwconv_initialized = 1;
    return 0;

end:
    ngli_hwconv_reset(hwconv);
    ngli_image_reset(image);
    ngli_texture_reset(texture);
    return ret;
}

static int exec_hwconv(struct ngl_node *node)
{
    struct texture_priv *s = node->priv_data;
    struct texture *texture = &s->texture;
    struct image *mapped_image = &s->hwupload_mapped_image;
    struct hwconv *hwconv = &s->hwupload_hwconv;

    int ret = ngli_hwconv_convert_image(hwconv, mapped_image);
    if (ret < 0)
        return ret;

    if (ngli_texture_has_mipmap(texture))
        ngli_texture_generate_mipmap(texture);

    return 0;
}

int ngli_hwupload_upload_frame(struct ngl_node *node)
{
    struct texture_priv *s = node->priv_data;
    struct media_priv *media = s->data_src->priv_data;
    struct sxplayer_frame *frame = media->frame;
    if (!frame)
        return 0;
    media->frame = NULL;

    const struct hwmap_class *hwmap_class = get_hwmap_class(frame);
    if (!hwmap_class) {
        sxplayer_release_frame(frame);
        return NGL_ERROR_UNSUPPORTED;
    }

    if (s->hwupload_map_class != hwmap_class) {
        ngli_hwupload_uninit(node);

        if (hwmap_class->priv_size) {
            s->hwupload_priv_data = ngli_calloc(1, hwmap_class->priv_size);
            if (!s->hwupload_priv_data) {
                sxplayer_release_frame(frame);
                return NGL_ERROR_MEMORY;
            }
        }

        int ret = hwmap_class->init(node, frame);
        if (ret < 0) {
            sxplayer_release_frame(frame);
            return ret;
        }
        s->hwupload_map_class = hwmap_class;

        LOG(DEBUG, "mapping texture '%s' with method: %s", node->label, hwmap_class->name);
    }

    int ret = hwmap_class->map_frame(node, frame);
    if (ret < 0)
        goto end;

    if (s->hwupload_require_hwconv) {
        ret = init_hwconv(node);
        if (ret < 0)
            return ret;
        ret = exec_hwconv(node);
        if (ret < 0)
            return ret;
    } else {
        s->image = s->hwupload_mapped_image;
    }

end:
    s->image.ts = frame->ts;

    if (!(hwmap_class->flags &  HWMAP_FLAG_FRAME_OWNER))
        sxplayer_release_frame(frame);
    return ret;
}

void ngli_hwupload_uninit(struct ngl_node *node)
{
    struct texture_priv *s = node->priv_data;
    ngli_hwconv_reset(&s->hwupload_hwconv);
    s->hwupload_hwconv_initialized = 0;
    s->hwupload_require_hwconv = 0;
    ngli_image_reset(&s->hwupload_mapped_image);
    if (s->hwupload_map_class && s->hwupload_map_class->uninit) {
        s->hwupload_map_class->uninit(node);
    }
    ngli_free(s->hwupload_priv_data);
    s->hwupload_priv_data = NULL;
    s->hwupload_map_class = NULL;
    ngli_image_reset(&s->image);
}
