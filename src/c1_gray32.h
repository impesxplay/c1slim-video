#ifndef C1_GRAY32_H
#define C1_GRAY32_H
#include <stdint.h>
#include <stddef.h>
#define C1_WIDTH 296
#define C1_HEIGHT 152
#define C1_PIXELS (C1_WIDTH*C1_HEIGHT)
typedef struct {
    void *ctx;
    /* Command + data; 0=success. */
    int (*write)(void *ctx, uint8_t command, const uint8_t *data, size_t n);
    void (*reset)(void *ctx); /* RST low 2ms, high 2ms; CS high, SCK low. */
    int (*busy)(void *ctx); /* Active high; negative on error. */
    uint64_t (*time_us)(void *ctx); /* Monotonic. */
    void (*sleep_us)(void *ctx, unsigned us);
} c1_bus;
/* 0=success (asleep), -1=error (caller resets). Exclusive use; ~12KB stack. */
int c1_gray32_display(const c1_bus *bus, const uint8_t ranks[C1_PIXELS]);
#endif
