#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>

#include <stdio.h>

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
#define NET_PER_SIM_TICKS 2

#define ARRLEN(arr) (sizeof(arr)/sizeof(arr[0]))

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

//
// Time
//

#define NANOSECS_PER_SEC (1000000000ull)

static inline void time_current(struct timespec *t) {
    assert(clock_gettime(CLOCK_MONOTONIC, t) == 0);
}

static inline void time_subtract(struct timespec *res, struct timespec *a, struct timespec *b) {
    assert(a->tv_sec >= b->tv_sec);
    if (a->tv_nsec >= b->tv_nsec) {
        res->tv_sec = a->tv_sec - b->tv_sec;
        res->tv_nsec = a->tv_nsec - b->tv_nsec;
    } else {
        res->tv_sec = a->tv_sec - b->tv_sec - 1;
        res->tv_nsec = NANOSECS_PER_SEC - (b->tv_nsec - a->tv_nsec);
    }
}

static inline u64 time_nanoseconds(struct timespec *t) {
    return t->tv_sec * NANOSECS_PER_SEC + t->tv_nsec;
}

static inline bool time_less_than(struct timespec *a, struct timespec *b) {
    return time_nanoseconds(a) < time_nanoseconds(b);
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
        (buf)->used--;                                                          \
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
