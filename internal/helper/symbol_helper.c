#include "symbol_helper.h"
#include <linux/module.h> //__symbol_get(), __symbol_put()
#include <linux/kallsyms.h> //kallsyms_lookup_name

bool kernel_has_symbol(const char *name) {
    if (__symbol_get(name)) { //search for public symbols
        __symbol_put(name);

        return true;
    }

    return kallsyms_lookup_name(name) != 0;
}