#ifndef REDPILL_DISABLE_EXECTUTABLES_H
#define REDPILL_DISABLE_EXECTUTABLES_H

int disable_common_executables(void);

//there's no method to re-enable them as intercept_execve doesn't have a method to remove entries [performance reasons]

#endif //REDPILL_DISABLE_EXECTUTABLES_H
