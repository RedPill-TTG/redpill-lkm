#include "math_helper.h"
#include <linux/random.h> //prandom_u32()

int prandom_int_range_stable(int *cur_val, int dev, int min, int max)
{
    if (likely(*cur_val != 0)) {
        int new_min = (*cur_val) - dev;
        int new_max = (*cur_val) + dev;
        min = new_min < min ? min : new_min;
        max = new_max > max ? max : new_max;
    }

    *cur_val = prandom_int_range(min, max);

    return *cur_val;
}