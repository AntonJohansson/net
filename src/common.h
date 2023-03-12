#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>

typedef int8_t   i8;
typedef uint8_t  u8;
typedef int16_t  i16;
typedef uint16_t u16;
typedef int32_t  i32;
typedef uint32_t u32;
typedef int64_t  i64;
typedef uint64_t u64;
typedef float    f32;
typedef double   f64;

#define MAX_CLIENTS 128
#define FPS 60
#define NET_PER_SIM_TICKS 1

#define ARRLEN(arr) (sizeof(arr)/sizeof(arr[0]))

#define ArrayPtrToIndex(arr, ptr) \
    (((intptr_t) ptr - (intptr_t) &arr[0])/sizeof(arr[0]))

//
// Float stuff
//

#define EPSILON (1e-4)

static inline f32 f32_abs(f32 f) {
    return (f < 0.0f) ? -f : f;
}

static inline bool f32_equal(f32 a, f32 b) {
    return f32_abs(a - b) < EPSILON;
}

static inline f32 f32_min(f32 a, f32 b) {
    return (a < b) ? a : b;
}

static inline f32 f32_max(f32 a, f32 b) {
    return (a > b) ? a : b;
}

static inline f32 f32_clamp(f32 x, f32 a, f32 b) {
    return f32_min(f32_max(x, a), b);
}

//
// Time
//

#define NANOSECONDS(n) (1000000000ull*(n))

static inline u64 time_current() {
    struct timespec t;
    assert(clock_gettime(CLOCK_MONOTONIC, &t) == 0);
    return t.tv_sec * NANOSECONDS(1) + t.tv_nsec;
}

static inline void time_nanosleep(u64 t) {
    struct timespec ts = {
        .tv_nsec = t,
    };
    while(nanosleep(&ts, NULL) == -1) {}
}

//
// Ciruclar buffer
//

#define CIRCULAR_BUFFER_APPEND(buf, element)                                    \
    do {                                                                        \
        assert((buf)->used < ARRLEN((buf)->data));                              \
        const size_t top = ((buf)->bottom + (buf)->used) % ARRLEN((buf)->data); \
        (buf)->data[top] = element;                                             \
        (buf)->used++;                                                          \
    } while (0)

#define CIRCULAR_BUFFER_POP(buf)                                                \
    do {                                                                        \
        assert((buf)->used > 0);                                                \
        (buf)->bottom = ((buf)->bottom + 1) % ARRLEN((buf)->data);              \
        --(buf)->used;                                                          \
    } while (0)

//
// ByteBuffer
//

struct byte_buffer {
    u8 *base;
    u8 *top;
    size_t size;
};

static inline struct byte_buffer byte_buffer_init(void *ptr, size_t size) {
    return (struct byte_buffer) {
        .base = ptr,
        .size = size,
        .top = ptr,
    };
}

static inline struct byte_buffer byte_buffer_alloc(size_t size) {
    void *ptr = malloc(size);
    assert(ptr);
    return byte_buffer_init(ptr, size);
}

static inline void byte_buffer_free(struct byte_buffer *b) {
    free(b->base);
    b->size = 0;
    b->top = NULL;
}

#define APPEND(buffer, data) \
    append(buffer, data, sizeof(*data))

static inline void append(struct byte_buffer *buffer, void *data, size_t size) {
    assert(buffer->top + size <= buffer->base + buffer->size);
    memcpy(buffer->top, data, size);
    buffer->top += size;
}

#define POP(buffer, data) \
    pop(buffer, (void **) data, sizeof(**data))

static inline void pop(struct byte_buffer *buffer, void **data, size_t size) {
    assert(buffer->top + size <= buffer->base + buffer->size);
    *data = buffer->top;
    buffer->top += size;
}

//
// Static unsorted list
//

#define List(type, size) \
    struct {                \
        type items[size];   \
        bool occupied[size]; \
        u32 num_items;      \
    }

#define ListInsert(list, value)                         \
    do {                                                \
        assert(list.num_items < ARRLEN(list.items));    \
        list.items[list.num_items] = value;             \
        list.occupied[list.num_items] = true;           \
        ++list.num_items;                               \
    } while(0)

#define ListTagRemoveIndex(list, index)         \
    do {                                        \
        assert(index < list.num_items);         \
        list.occupied[index] = false;           \
    } while (0)

#define ListTagRemovePtr(list, ptr) \
    ListTagRemoveIndex(list, ArrayPtrToIndex(list.items, ptr))

#define ListRemoveTaggedItems(list)                                                                                      \
    do {                                                                                                        \
        for (u32 i = 0; i < list.num_items;) {                                                                  \
            if (!list.occupied[i]) {                                                                            \
                if (i+1 < list.num_items) {                                                                     \
                    memmove(&list.items[i], &list.items[i+1], sizeof(list.items[0])*(list.num_items - (i+1)));  \
                    memmove(&list.occupied[i], &list.occupied[i+1], sizeof(bool)*(list.num_items - (i+1)));     \
                }                                                                                               \
                --list.num_items;                                                                               \
            } else {                                                                                            \
                ++i;                                                                                            \
            }                                                                                                   \
        }                                                                                                       \
    } while (0)

#define ListClear(list) \
    list.num_items = 0

#define ForEachList(list, type, iter) \
    for (type *iter = &list.items[0], *_top = &list.items[list.num_items]; iter < _top; ++iter)
