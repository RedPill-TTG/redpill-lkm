#ifndef REDPILL_MATH_HELPER_H
#define REDPILL_MATH_HELPER_H

/**
 * Generates pseudo-random integer in a range specified
 *
 * @param min Lower boundary integer
 * @param max Higher boundary integer
 *
 * @return pseudorandom integer up to 32 bits in length
 */
#define prandom_int_range(min, max) ({                         \
    int _rand = (prandom_u32() % ((max) + 1 - (min)) + (min)); \
    _rand;                                                     \
})

/**
 * Generates temporally stable pseudo-random integer in a range specified
 *
 * @param cur_val Pointer to store/read current value; set its value to 0 initially to generate setpoint automatically
 * @param dev Max deviation from the current value
 * @param min Lower boundary integer
 * @param max Higher boundary integer
 *
 * @return pseudorandom integer up to 32 bits in length
 */
int prandom_int_range_stable(int *cur_val, int dev, int min, int max);

#endif //REDPILL_MATH_HELPER_H
