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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "spirv.h"
#include "utils.h"
#include "hmap.h"
#include "darray.h"

struct spirv_header {
    uint32_t magic;
    uint32_t version;
    uint32_t gen_magic;
    uint32_t bound;
    uint32_t reserved;
};

struct shader_variable_internal {
    const char *name;
    uint16_t offset;
};

struct shader_type_internal {
    const char *name;
    struct shader_variable_internal variables[8];
    uint8_t nb_variables;
    uint16_t size;
    uint8_t index;
    uint16_t flag;
};

struct shader_internal {
    struct shader_type_internal types[256];
    struct darray variable_type_indices;
    struct darray block_type_indices;
};

static int op_name(struct shader_internal *s, const uint32_t *code)
{
    const uint32_t type_id = code[1];
    const char *name = (const char *)&code[2];
    struct shader_type_internal *type = &s->types[type_id];
    type->name = name;
    return 0;
}

static int op_membername(struct shader_internal *s, const uint32_t *code)
{
    const uint32_t type_id = code[1];
    const uint32_t variable_index = code[2];
    const char *name = (const char *)&code[3];

    struct shader_type_internal *type = &s->types[type_id];
    type->nb_variables++;

    struct shader_variable_internal *variable = &type->variables[variable_index];
    variable->name = name;
    return 0;
}

static int op_typefloat(struct shader_internal *s, const uint32_t *code)
{
    const uint32_t type_id = code[1];
    const uint32_t type_size = code[2];
    struct shader_type_internal *type = &s->types[type_id];
    type->size = type_size / 8;
    return 0;
}

static int op_typevector(struct shader_internal *s, const uint32_t *code)
{
    const uint32_t type_id = code[1];
    const uint32_t component_type_id = code[2];
    const uint32_t component_count = code[3];
    struct shader_type_internal *type = &s->types[type_id];
    struct shader_type_internal *component_type = &s->types[component_type_id];
    type->size = component_type->size * component_count;
    return 0;
}

static int op_typematrix(struct shader_internal *s, const uint32_t *code)
{
    const uint32_t type_id = code[1];
    const uint32_t column_type_id = code[2];
    const uint32_t column_count = code[3];
    struct shader_type_internal *type = &s->types[type_id];
    struct shader_type_internal *column_type = &s->types[column_type_id];
    type->size = column_type->size * column_count;
    return 0;
}

static int op_typeimage(struct shader_internal *s, const uint32_t *code)
{
    const uint32_t type_id = code[1];
    struct shader_type_internal *type = &s->types[type_id];
    type->flag |= NGLI_SHADER_TEXTURE;
    return 0;
}

static int op_typesampler(struct shader_internal *s, const uint32_t *code)
{
    const uint32_t pointer_id = code[1];
    const uint32_t type_id = code[2];

    struct shader_type_internal *pointer_type = &s->types[pointer_id];
    pointer_type->flag = NGLI_SHADER_INDIRECTION;
    pointer_type->index = type_id;

    struct shader_type_internal *type = &s->types[type_id];
    type->flag |= NGLI_SHADER_SAMPLER;
    return 0;
}

static int op_typesampledimage(struct shader_internal *s, const uint32_t *code)
{
    const uint32_t pointer_id = code[1];
    const uint32_t type_id = code[2];

    struct shader_type_internal *pointer_type = &s->types[pointer_id];
    pointer_type->flag = NGLI_SHADER_INDIRECTION;
    pointer_type->index = type_id;

    struct shader_type_internal *type = &s->types[type_id];
    type->flag |= NGLI_SHADER_SAMPLER;
    return 0;
}

static int op_typeruntimearray(struct shader_internal *s, const uint32_t *code)
{
    const uint32_t pointer_id = code[1];
    const uint32_t type_id = code[2];

    struct shader_type_internal *type = &s->types[pointer_id];
    type->index = type_id;
    type->flag = (uint8_t)-1;
    return 0;
}

static int op_typestruct(struct shader_internal *s, const uint32_t *code)
{
    const uint32_t type_id = code[1];

    struct shader_type_internal *type = &s->types[type_id];
    const uint8_t last_variable_index = type->nb_variables - 1;
    const uint32_t member_type_id = code[2 + last_variable_index];
    struct shader_type_internal *member_type = &s->types[member_type_id];
    struct shader_variable_internal *variable = &type->variables[last_variable_index];
    type->size = variable->offset + member_type->size;
    return 0;
}

static int op_typepointer(struct shader_internal *s, const uint32_t *code)
{
    const uint32_t pointer_id = code[1];
    const uint32_t storage_type = code[2];
    const uint32_t type_id = code[3];

    switch (storage_type) {
        case 0:   // UniformConstant
        case 2: { // Uniform
            struct shader_type_internal *type = &s->types[pointer_id];
            type->flag = NGLI_SHADER_INDIRECTION;
            type->index = type_id;
            break;
        }

        // PushConstant
        case 9: {
            struct shader_type_internal *type = &s->types[pointer_id];
            type->flag = NGLI_SHADER_INDIRECTION;
            type->index = type_id;

            struct shader_type_internal *block_type = &s->types[type_id];
            block_type->flag &= ~NGLI_SHADER_UNIFORM;
            block_type->flag |= NGLI_SHADER_CONSTANT;
            break;
        }
    }
    return 0;
}

static int op_variable(struct shader_internal *s, const uint32_t *code)
{
    const uint32_t pointer_id = code[1];
    const uint32_t type_id = code[2];
    const uint32_t storage_type = code[3];

    struct shader_type_internal *type = &s->types[type_id];
    switch (storage_type) {
        // Input
        case 1: {
            type->flag |= NGLI_SHADER_INPUT;
            break;
        }

        case 0: // UniformConstant
        case 2: // Uniform
        case 9: {
            // indirection to proper structureu
            uint32_t block_id = pointer_id;
            struct shader_type_internal *block_type = NULL;
            do {
                block_type = &s->types[block_id];
                block_id = block_type->index;
            } while (block_type->flag == NGLI_SHADER_INDIRECTION); // FIXME: use & instead of ==

            memcpy(type->variables, block_type->variables, sizeof(type->variables));
            type->nb_variables = block_type->nb_variables;
            type->size = block_type->size;
            type->flag = block_type->flag;
            ngli_darray_push(&s->block_type_indices, &type_id);
        }

        // Output
        case 3: {
            type->flag |= NGLI_SHADER_OUTPUT;
            break;
        }
    }
    return 0;
}

static int op_decorate(struct shader_internal *s, const uint32_t *code)
{
    const uint32_t type_id = code[1];
    const uint32_t decoration = code[2];
    switch (decoration) {
        // Block
        case 2: {
            struct shader_type_internal *type = &s->types[type_id];
            type->flag |= NGLI_SHADER_BLOCK;
            type->flag |= NGLI_SHADER_UNIFORM;
            break;
        }

        // Buffer Block
        case 3: {
            struct shader_type_internal *type = &s->types[type_id];
            type->flag |= NGLI_SHADER_BLOCK;
            type->flag |= NGLI_SHADER_STORAGE;
            break;
        }

        // Location
        case 30: {
            const uint32_t index = code[3];
            s->types[type_id].index = index;
            s->types[type_id].flag |= NGLI_SHADER_ATTRIBUTE;
            ngli_darray_push(&s->variable_type_indices, &type_id);
            break;
        }

        case 33: {
            const uint32_t index = code[3];
            s->types[type_id].index = index;
            break;
        }
    }
    return 0;
}

static int op_memberdecorate(struct shader_internal *s, const uint32_t *code)
{
    const uint32_t type_id = code[1];
    const uint32_t variable_index = code[2];
    const uint32_t decoration = code[3];

    // Offset
    if (decoration == 35) {
        const uint32_t offset = code[4];
        struct shader_variable_internal *variable = &s->types[type_id].variables[variable_index];
        variable->offset = offset;
    }
    return 0;
}

static const struct {
    int (*func)();
    //int min_size;
} op_map[] = {
    [ 5] = {op_name},
    [ 6] = {op_membername},
    [22] = {op_typefloat},
    [23] = {op_typevector},
    [24] = {op_typematrix},
    [25] = {op_typeimage},
    [26] = {op_typesampler},
    [27] = {op_typesampledimage},
    [29] = {op_typeruntimearray},
    [30] = {op_typestruct},
    [32] = {op_typepointer},
    [59] = {op_variable},
    [71] = {op_decorate},
    [72] = {op_memberdecorate},
};

struct spirv_desc *ngli_spirv_parse(const uint32_t *code, size_t size)
{
    if (size < sizeof(struct spirv_header))
        return NULL;

    struct spirv_header *header = (struct spirv_header*)code;
    if (header->magic != 0x07230203)
        return NULL;
    if (header->version != 0x00010000) // XXX: allow more?
        return NULL;

    code += sizeof(struct spirv_header) / sizeof(uint32_t);
    size -= sizeof(struct spirv_header);

    struct shader_internal internal;
    memset(&internal, 0, sizeof(internal));

    struct darray *variable_type_indices_array = &internal.variable_type_indices;
    struct darray *block_type_indices_array = &internal.block_type_indices;

    ngli_darray_init(variable_type_indices_array, sizeof(uint32_t), 0);
    ngli_darray_init(block_type_indices_array, sizeof(uint32_t), 0);

    while (size > 0) {
        const uint32_t opcode0    = code[0];
        const uint16_t opcode     = opcode0 & 0xffff;
        const uint16_t word_count = opcode0 >> 16;

        const uint32_t instruction_size = word_count * sizeof(uint32_t);
        if (instruction_size > size)
            return NULL;

        if (opcode < NGLI_ARRAY_NB(op_map) && op_map[opcode].func) {
            op_map[opcode].func(&internal, code);
        }

        code += word_count;
        size -= instruction_size;
    }

    const int nb_blocks    = ngli_darray_count(block_type_indices_array);
    const int nb_variables = ngli_darray_count(variable_type_indices_array);
    const uint32_t *block_type_indices    = ngli_darray_data(block_type_indices_array);
    const uint32_t *variable_type_indices = ngli_darray_data(variable_type_indices_array);

    // allocate spirv_desc memory
    uint32_t variable_bytes = nb_variables * sizeof(struct spirv_variable);
    uint32_t block_bytes = nb_blocks * sizeof(struct spirv_block);
    for (uint32_t i = 0; i < nb_blocks; i++) {
        const uint32_t block_type_id = block_type_indices[i];
        struct shader_type_internal *type = &internal.types[block_type_id];
        block_bytes += type->nb_variables * sizeof(struct spirv_variable);
    }
    uint32_t reflection_bytes = sizeof(struct spirv_desc) + variable_bytes + block_bytes;
    uint8_t *allocation = malloc(reflection_bytes); // XXX

    // initialize spirv_desc
    struct spirv_desc *spirv_desc = (struct spirv_desc *)allocation;
    spirv_desc->attributes = nb_variables ? ngli_hmap_create() : NULL;
    spirv_desc->bindings = nb_blocks ? ngli_hmap_create() : NULL;

    // initialize variables
    struct spirv_variable *variable = (struct spirv_variable*)(allocation + sizeof(struct spirv_desc));
    for (uint32_t i = 0; i < nb_variables; i++) {
        const uint32_t variable_type_id = variable_type_indices[i];
        struct shader_type_internal *type = &internal.types[variable_type_id];

        variable->offset = type->index;
        variable->flag = type->flag;
        ngli_hmap_set(spirv_desc->attributes, type->name, variable++);
    }

    // initialize bindings
    struct spirv_binding *binding = (struct spirv_binding*)variable;
    for (uint32_t i = 0; i < nb_blocks; i++) {
        const uint32_t block_type_id = block_type_indices[i];
        struct shader_type_internal *type_internal = &internal.types[block_type_id];

        binding->index = type_internal->index;
        binding->flag = type_internal->flag;

        // initialize block
        uint32_t binding_byte = 0;
        if (type_internal->flag & NGLI_SHADER_BLOCK) {
            struct spirv_block *block = (struct spirv_block *)binding;
            block->size = type_internal->size;
            binding_byte += sizeof(struct spirv_block);
            const uint32_t nb_variables = type_internal->nb_variables;
            if (nb_variables > 0) {
                block->variables = ngli_hmap_create();
                struct spirv_variable *variable = (struct spirv_variable *)(((uint8_t*)binding) + binding_byte);
                for (uint32_t j = 0; j < nb_variables; j++) {
                    struct shader_variable_internal *v = &type_internal->variables[j];
                    variable->offset = v->offset;
                    variable->flag = 0;
                    ngli_hmap_set(block->variables, v->name, variable++);
                }
                binding_byte += nb_variables * sizeof(struct spirv_variable);
             }
        } else if (type_internal->flag & NGLI_SHADER_TEXTURE) {
            struct spirv_texture *texture = (struct spirv_texture *)binding;
            texture->format = 0; // XXX
            binding_byte += sizeof(struct spirv_texture);
        } else {
            ngli_assert(0);
        }

        ngli_hmap_set(spirv_desc->bindings, type_internal->name, binding);
        binding = (struct spirv_binding*)(((uint8_t*)binding) + binding_byte);
     }

    ngli_darray_reset(&internal.variable_type_indices);
    ngli_darray_reset(&internal.block_type_indices);

    return spirv_desc;
}

void ngli_spirv_freep(struct spirv_desc **reflection)
{
    struct spirv_desc *s = *reflection;

    if (!s)
        return;

    ngli_hmap_freep(&s->attributes);

    if (s->bindings) {
        const struct hmap_entry *entry = NULL;
        while ((entry = ngli_hmap_next(s->bindings, entry))) {
            struct spirv_binding *binding = entry->data;
            if (binding->flag & NGLI_SHADER_BLOCK) {
                struct spirv_block *block = entry->data;
                ngli_hmap_freep(&block->variables);
            }
        }
    }
    ngli_hmap_freep(&s->bindings);

    free(s);
    s = NULL;
}