/* A minimal setjmp/longjmp for the calculator: newlib's pulls in the C++
 * unwinder, and the Rust builds link no C library. Saves the callee-saved
 * core registers, the stack pointer, the return address and s16-s31.
 * On other platforms, the C library's. */
#ifndef NP_JUMP_H
#define NP_JUMP_H
#include <stdint.h>

#if PLATFORM_DEVICE && !defined(HOST) && defined(__arm__)
typedef uint32_t np_jump_t[26];
__attribute__((naked, returns_twice, noinline, unused)) static int np_save_jump(__attribute__((unused)) np_jump_t j) {
  __asm__ volatile(
      "mov r12, sp\n"
      "stmia r0!, {r4-r11, r12, lr}\n"
      "vstmia r0!, {s16-s31}\n"
      "movs r0, #0\n"
      "bx lr\n");
}
__attribute__((naked, noreturn, noinline, unused)) static void np_jump(__attribute__((unused)) np_jump_t j) {
  __asm__ volatile(
      "ldmia r0!, {r4-r11, r12, lr}\n"
      "mov sp, r12\n"
      "vldmia r0!, {s16-s31}\n"
      "movs r0, #1\n"
      "bx lr\n");
}
#else
#include <setjmp.h>
typedef jmp_buf np_jump_t;
#define np_save_jump(j) setjmp(j)
#define np_jump(j) longjmp(j, 1)
#endif
#endif
