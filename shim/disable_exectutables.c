#include "disable_exectutables.h"
#include "../internal/intercept_execve.h"

#define PSTORE_PATH "/usr/syno/bin/syno_pstore_collect"

int disable_common_executables(void)
{
    //Linux pstore capabilities aren't common on normal PCs
    return add_blocked_execve_filename(PSTORE_PATH);
}