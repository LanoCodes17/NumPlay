/* Bare-metal Cortex-M7 benchmark of the generator, run by bench.py in Unicorn.
 * phase is bumped after each step so the runner can count instructions per step. */
#include <stdint.h>
#include <string.h>
#include "../../../src/gen.h"

volatile uint32_t phase;
volatile uint32_t sums[64];
volatile int32_t spawn[3];
static uint8_t out[128 * 256];

#ifndef NSIDE
#define NSIDE 4
#endif

int main(void) {
    gen_init(12345);
    phase = 1;
    int k = 0;
    for (int cz = -2; cz < -2 + NSIDE; cz++)
        for (int cx = -2; cx < -2 + NSIDE; cx++) {
            gen_slab(cx, cz, 0, 128, out);
            uint32_t s = 0;
            for (unsigned i = 0; i < sizeof out; i++) s = s * 31 + out[i];
            sums[k++] = s;
            phase = phase + 1;
        }
#ifndef NO_SPAWN
    /* a 32-high slab as the engine asks */
    gen_slab(5, 5, 32, 32, out);
    phase = phase + 1;
    int x, y, z;
    gen_spawn(&x, &y, &z);
    spawn[0] = x, spawn[1] = y, spawn[2] = z;
    phase = phase + 1;
#endif
    return 0;
}

extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;
void Reset_Handler(void) {
    uint32_t *s = &_sidata, *d = &_sdata;
    while (d < &_edata) *d++ = *s++;
    for (d = &_sbss; d < &_ebss;) *d++ = 0;
    main();
    for (;;) __asm volatile("bkpt #0");
}

__attribute__((section(".isr_vector"), used)) const void *const vectors[2] = {&_estack, (void *)Reset_Handler};
