#ifndef MCUBOOT_ASSERT_H
#define MCUBOOT_ASSERT_H

#define ASSERT(expression)                                                     \
  do {                                                                         \
    if (!(expression)) {                                                       \
      __asm volatile("bkpt #0");                                            \
      for (;;) {                                                               \
      }                                                                        \
    }                                                                          \
  } while (0)
#define assert(expression) ASSERT(expression)

#endif
