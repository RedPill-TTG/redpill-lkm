/*
 *  linux/lib/string.c
 *  Modified to take care of kernel versions
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */
#include "string_compat.h"
#include <linux/errno.h> //E2BIG

#if LINUX_VERSION_CODE <= KERNEL_VERSION(4,3,0)
#include <asm/byteorder.h>
#include <asm/word-at-a-time.h>
#include <asm/page.h> //PAGE_SIZE

/**
  * strscpy - Copy a C-string into a sized buffer
  * @dest: Where to copy the string to
  * @src: Where to copy the string from
  * @count: Size of destination buffer
  *
  * Copy the string, or as much of it as fits, into the dest buffer.
  * The routine returns the number of characters copied (not including
  * the trailing NUL) or -E2BIG if the destination buffer wasn't big enough.
  * The behavior is undefined if the string buffers overlap.
  * The destination buffer is always NUL terminated, unless it's zero-sized.
  *
  * Preferred to strlcpy() since the API doesn't require reading memory
  * from the src string beyond the specified "count" bytes, and since
  * the return value is easier to error-check than strlcpy()'s.
  * In addition, the implementation is robust to the string changing out
  * from underneath it, unlike the current strlcpy() implementation.
  *
  * Preferred to strncpy() since it always returns a valid string, and
  * doesn't unnecessarily force the tail of the destination buffer to be
  * zeroed.  If the zeroing is desired, it's likely cleaner to use strscpy()
  * with an overflow test, then just memset() the tail of the dest buffer.
  */
ssize_t strscpy(char *dest, const char *src, size_t count)
{
    const struct word_at_a_time constants = WORD_AT_A_TIME_CONSTANTS;
    size_t max = count;
    long res = 0;

    if (count == 0)
        return -E2BIG;

#ifdef CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS
    /*
 	 * If src is unaligned, don't cross a page boundary,
 	 * since we don't know if the next page is mapped.
 	 */
 	if ((long)src & (sizeof(long) - 1)) {
 		size_t limit = PAGE_SIZE - ((long)src & (PAGE_SIZE - 1));
 		if (limit < max)
 			max = limit;
 	}
#else
    /* If src or dest is unaligned, don't do word-at-a-time. */
    if (((long) dest | (long) src) & (sizeof(long) - 1))
        max = 0;
#endif

    while (max >= sizeof(unsigned long)) {
        unsigned long c, data;

        c = *(unsigned long *)(src+res);
        *(unsigned long *)(dest+res) = c;
        if (has_zero(c, &data, &constants)) {
            data = prep_zero_mask(c, data, &constants);
            data = create_zero_mask(data);
            return res + find_zero(data);
        }
        res += sizeof(unsigned long);
        count -= sizeof(unsigned long);
        max -= sizeof(unsigned long);
    }

    while (count) {
        char c;

        c = src[res];
        dest[res] = c;
        if (!c)
            return res;
        res++;
        count--;
    }

    /* Hit buffer length without finding a NUL; force NUL-termination. */
    if (res)
        dest[res-1] = '\0';

    return -E2BIG;
}
#endif //kernel 4.3.0