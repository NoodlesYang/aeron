/*
 * Copyright 2014-2020 Real Logic Limited.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <errno.h>
#include <inttypes.h>

#include "aeron_fragment_assembler.h"
#include "aeron_alloc.h"
#include "util/aeron_error.h"
#include "aeron_image.h"

int aeron_buffer_builder_create(aeron_buffer_builder_t **buffer_builder)
{
    aeron_buffer_builder_t *_buffer_builder;

    if (aeron_alloc((void **)&_buffer_builder, sizeof(aeron_buffer_builder_t)) < 0)
    {
        aeron_set_err_from_last_err_code("%s:%d", __FILE__, __LINE__);
        return -1;
    }

    _buffer_builder->buffer = NULL;
    _buffer_builder->buffer_length = 0;
    _buffer_builder->limit = 0;

    *buffer_builder = _buffer_builder;
    return 0;
}

#define AERON_BUFFER_BUILDER_MIN_ALLOCATED_CAPACITY (4096)
#define AERON_BUFFER_BUILDER_MAX_CAPACITY (INT32_MAX - 8)

int aeron_buffer_builder_find_suitable_capacity(size_t current_capacity, size_t required_capacity)
{
    size_t capacity = current_capacity;

    do
    {
        const size_t candidate_capacity = capacity + (capacity >> 1u);
        const size_t new_capacity = candidate_capacity > AERON_BUFFER_BUILDER_MIN_ALLOCATED_CAPACITY ?
            candidate_capacity : AERON_BUFFER_BUILDER_MIN_ALLOCATED_CAPACITY;

        if (new_capacity > candidate_capacity)
        {
            if (capacity == AERON_BUFFER_BUILDER_MAX_CAPACITY)
            {
                errno = EINVAL;
                aeron_set_err(EINVAL, "max capacity reached: %" PRId32, AERON_BUFFER_BUILDER_MAX_CAPACITY);
                return -1;
            }

            capacity = AERON_BUFFER_BUILDER_MAX_CAPACITY;
        }
        else
        {
            capacity = new_capacity;
        }
    }
    while (capacity < required_capacity);

    return (int)capacity;
}

int aeron_buffer_builder_ensure_capacity(aeron_buffer_builder_t *buffer_builder, size_t additional_capacity)
{
    size_t required_capacity = buffer_builder->limit + additional_capacity;

    if (required_capacity > buffer_builder->buffer_length)
    {
        int suitable_capacity = aeron_buffer_builder_find_suitable_capacity(
            buffer_builder->buffer_length, required_capacity);

        if (suitable_capacity < 0)
        {
            return -1;
        }

        if (aeron_alloc((void **)&buffer_builder->buffer, suitable_capacity) < 0)
        {
            aeron_set_err_from_last_err_code("%s:%d", __FILE__, __LINE__);
            return -1;
        }

        buffer_builder->buffer_length = suitable_capacity;
    }

    return 0;
}

void aeron_buffer_builder_delete(aeron_buffer_builder_t *buffer_builder)
{
    if (buffer_builder)
    {
        aeron_free(buffer_builder->buffer);
        aeron_free(buffer_builder);
    }
}

int aeron_image_fragment_assembler_create(
    aeron_image_fragment_assembler_t **assembler,
    aeron_fragment_handler_t delegate,
    void *delegate_clientd)
{
    aeron_image_fragment_assembler_t *_assembler;

    if (aeron_alloc((void **)&_assembler, sizeof(aeron_image_fragment_assembler_t)) < 0)
    {
        aeron_set_err_from_last_err_code("%s:%d", __FILE__, __LINE__);
        return -1;
    }

    if (aeron_buffer_builder_create(&_assembler->buffer_builder) < 0)
    {
        return -1;
    }

    _assembler->delegate = delegate;
    _assembler->delegate_clientd = delegate_clientd;

    *assembler = _assembler;
    return 0;
}

int aeron_image_fragment_assembler_delete(aeron_image_fragment_assembler_t *assembler)
{
    if (assembler)
    {
        aeron_buffer_builder_delete(assembler->buffer_builder);
        aeron_free(assembler);
    }

    return 0;
}

void aeron_image_fragment_assembler_handler(
    void *clientd, const uint8_t *buffer, size_t offset, size_t length, aeron_header_t *header)
{
    aeron_image_fragment_assembler_t *assembler = (aeron_image_fragment_assembler_t *)clientd;
    aeron_buffer_builder_t *buffer_builder = assembler->buffer_builder;
    uint8_t flags = header->frame->frame_header.flags;

    if ((flags & AERON_DATA_HEADER_UNFRAGMENTED) == AERON_DATA_HEADER_UNFRAGMENTED)
    {
        assembler->delegate(assembler->delegate_clientd, buffer, offset, length, header);
    }
    else
    {
        if (flags & AERON_DATA_HEADER_BEGIN_FLAG)
        {
            aeron_buffer_builder_reset(buffer_builder);
            aeron_buffer_builder_append(buffer_builder, buffer, offset, length);
        }
        else if (buffer_builder->limit > 0)
        {
            aeron_buffer_builder_append(buffer_builder, buffer, offset, length);

            if (flags & AERON_DATA_HEADER_END_FLAG)
            {
                assembler->delegate(
                    assembler->delegate_clientd, buffer_builder->buffer, 0, buffer_builder->limit, header);
                aeron_buffer_builder_reset(buffer_builder);
            }
        }
    }
}

int aeron_image_controlled_fragment_assembler_create(
    aeron_image_controlled_fragment_assembler_t **assembler,
    aeron_controlled_fragment_handler_t delegate,
    void *delegate_clientd)
{
    aeron_image_controlled_fragment_assembler_t *_assembler;

    if (aeron_alloc((void **)&_assembler, sizeof(aeron_image_controlled_fragment_assembler_t)) < 0)
    {
        aeron_set_err_from_last_err_code("%s:%d", __FILE__, __LINE__);
        return -1;
    }

    if (aeron_buffer_builder_create(&_assembler->buffer_builder) < 0)
    {
        return -1;
    }

    _assembler->delegate = delegate;
    _assembler->delegate_clientd = delegate_clientd;

    *assembler = _assembler;
    return 0;
}

int aeron_image_controlled_fragment_assembler_delete(aeron_image_fragment_assembler_t *assembler)
{
    if (assembler)
    {
        aeron_buffer_builder_delete(assembler->buffer_builder);
        aeron_free(assembler);
    }

    return 0;
}

aeron_controlled_fragment_handler_action_t aeron_controlled_image_fragment_assembler_handler(
    void *clientd, const uint8_t *buffer, size_t offset, size_t length, aeron_header_t *header)
{
    aeron_image_controlled_fragment_assembler_t *assembler = (aeron_image_controlled_fragment_assembler_t *)clientd;
    aeron_buffer_builder_t *buffer_builder = assembler->buffer_builder;
    uint8_t flags = header->frame->frame_header.flags;
    aeron_controlled_fragment_handler_action_t action = AERON_ACTION_CONTINUE;

    if ((flags & AERON_DATA_HEADER_UNFRAGMENTED) == AERON_DATA_HEADER_UNFRAGMENTED)
    {
        action = assembler->delegate(assembler->delegate_clientd, buffer, offset, length, header);
    }
    else
    {
        if (flags & AERON_DATA_HEADER_BEGIN_FLAG)
        {
            aeron_buffer_builder_reset(buffer_builder);
            aeron_buffer_builder_append(buffer_builder, buffer, offset, length);
        }
        else
        {
            size_t limit = buffer_builder->limit;
            if (limit > 0)
            {
                aeron_buffer_builder_append(buffer_builder, buffer, offset, length);

                if (flags & AERON_DATA_HEADER_END_FLAG)
                {
                    action = assembler->delegate(
                        assembler->delegate_clientd, buffer_builder->buffer, 0, buffer_builder->limit, header);

                    if (AERON_ACTION_ABORT == action)
                    {
                        buffer_builder->limit = limit;
                    }
                    else
                    {
                        aeron_buffer_builder_reset(buffer_builder);
                    }
                }
            }
        }
    }

    return action;
}

int aeron_fragment_assembler_create(
    aeron_fragment_assembler_t **assembler,
    aeron_fragment_handler_t delegate,
    void *delegate_clientd)
{
    aeron_fragment_assembler_t *_assembler;

    if (aeron_alloc((void **)&_assembler, sizeof(aeron_fragment_assembler_t)) < 0)
    {
        aeron_set_err_from_last_err_code("%s:%d", __FILE__, __LINE__);
        return -1;
    }

    if (aeron_int64_to_ptr_hash_map_init(
        &_assembler->builder_by_session_id_map, 8, AERON_MAP_DEFAULT_LOAD_FACTOR) < 0)
    {
        int errcode = errno;

        aeron_set_err(errcode, "aeron_fragment_assembler_create - builder_by_session_id_map: %s",
            strerror(errcode));
        return -1;
    }

    _assembler->delegate = delegate;
    _assembler->delegate_clientd = delegate_clientd;

    *assembler = _assembler;
    return 0;
}

int aeron_fragment_assembler_delete(aeron_fragment_assembler_t *assembler)
{
    if (assembler)
    {
        aeron_int64_to_ptr_hash_map_delete(&assembler->builder_by_session_id_map);
        aeron_free(assembler);
    }

    return 0;
}

void aeron_fragment_assembler_handler(
    void *clientd, const uint8_t *buffer, size_t offset, size_t length, aeron_header_t *header)
{
    aeron_fragment_assembler_t *assembler = (aeron_fragment_assembler_t *)clientd;
    uint8_t flags = header->frame->frame_header.flags;

    if ((flags & AERON_DATA_HEADER_UNFRAGMENTED) == AERON_DATA_HEADER_UNFRAGMENTED)
    {
        assembler->delegate(assembler->delegate_clientd, buffer, offset, length, header);
    }
    else
    {
        aeron_buffer_builder_t *buffer_builder = aeron_int64_to_ptr_hash_map_get(
            &assembler->builder_by_session_id_map, header->frame->session_id);

        if (flags & AERON_DATA_HEADER_BEGIN_FLAG)
        {
            if (NULL == buffer_builder)
            {
                if (aeron_buffer_builder_create(&buffer_builder) < 0 ||
                    aeron_int64_to_ptr_hash_map_put(
                        &assembler->builder_by_session_id_map, header->frame->session_id, buffer_builder) < 0)
                {
                    return;
                }
            }

            aeron_buffer_builder_reset(buffer_builder);
            aeron_buffer_builder_append(buffer_builder, buffer, offset, length);
        }
        else if (buffer_builder && buffer_builder->limit > 0)
        {
            aeron_buffer_builder_append(buffer_builder, buffer, offset, length);

            if (flags & AERON_DATA_HEADER_END_FLAG)
            {
                assembler->delegate(
                    assembler->delegate_clientd, buffer_builder->buffer, 0, buffer_builder->limit, header);
                aeron_buffer_builder_reset(buffer_builder);
            }
        }
    }
}

int aeron_controlled_fragment_assembler_create(
    aeron_controlled_fragment_assembler_t **assembler,
    aeron_controlled_fragment_handler_t delegate,
    void *delegate_clientd)
{
    aeron_controlled_fragment_assembler_t *_assembler;

    if (aeron_alloc((void **)&_assembler, sizeof(aeron_controlled_fragment_assembler_t)) < 0)
    {
        aeron_set_err_from_last_err_code("%s:%d", __FILE__, __LINE__);
        return -1;
    }

    if (aeron_int64_to_ptr_hash_map_init(
        &_assembler->builder_by_session_id_map, 8, AERON_MAP_DEFAULT_LOAD_FACTOR) < 0)
    {
        int errcode = errno;

        aeron_set_err(errcode, "aeron_controlled_fragment_assembler_create - builder_by_session_id_map: %s",
            strerror(errcode));
        return -1;
    }

    _assembler->delegate = delegate;
    _assembler->delegate_clientd = delegate_clientd;

    *assembler = _assembler;
    return 0;
}

int aeron_controlled_fragment_assembler_delete(aeron_controlled_fragment_assembler_t *assembler)
{
    if (assembler)
    {
        aeron_int64_to_ptr_hash_map_delete(&assembler->builder_by_session_id_map);
        aeron_free(assembler);
    }

    return 0;
}

aeron_controlled_fragment_handler_action_t aeron_controlled_fragment_assembler_handler(
    void *clientd, const uint8_t *buffer, size_t offset, size_t length, aeron_header_t *header)
{
    aeron_controlled_fragment_assembler_t *assembler = (aeron_controlled_fragment_assembler_t *)clientd;
    uint8_t flags = header->frame->frame_header.flags;
    aeron_controlled_fragment_handler_action_t action = AERON_ACTION_CONTINUE;

    if ((flags & AERON_DATA_HEADER_UNFRAGMENTED) == AERON_DATA_HEADER_UNFRAGMENTED)
    {
        action = assembler->delegate(assembler->delegate_clientd, buffer, offset, length, header);
    }
    else
    {
        aeron_buffer_builder_t *buffer_builder = aeron_int64_to_ptr_hash_map_get(
            &assembler->builder_by_session_id_map, header->frame->session_id);

        if (flags & AERON_DATA_HEADER_BEGIN_FLAG)
        {
            if (NULL == buffer_builder)
            {
                if (aeron_buffer_builder_create(&buffer_builder) < 0 ||
                    aeron_int64_to_ptr_hash_map_put(
                        &assembler->builder_by_session_id_map, header->frame->session_id, buffer_builder) < 0)
                {
                    return AERON_ACTION_ABORT;
                }
            }

            aeron_buffer_builder_reset(buffer_builder);
            aeron_buffer_builder_append(buffer_builder, buffer, offset, length);
        }
        else if (NULL != buffer_builder)
        {
            size_t limit = buffer_builder->limit;
            if (limit > 0)
            {
                aeron_buffer_builder_append(buffer_builder, buffer, offset, length);

                if (flags & AERON_DATA_HEADER_END_FLAG)
                {
                    action = assembler->delegate(
                        assembler->delegate_clientd, buffer_builder->buffer, 0, buffer_builder->limit, header);

                    if (AERON_ACTION_ABORT == action)
                    {
                        buffer_builder->limit = limit;
                    }
                    else
                    {
                        aeron_buffer_builder_reset(buffer_builder);
                    }
                }
            }
        }
    }

    return action;
}

extern void aeron_buffer_builder_reset(aeron_buffer_builder_t *buffer_builder);
extern int aeron_buffer_builder_append(
    aeron_buffer_builder_t *buffer_builder, const uint8_t *buffer, size_t offset, size_t length);