#ifndef REDPILL_SHIM_BASE_H
#define REDPILL_SHIM_BASE_H

#define shim_reg_in() pr_loc_dbg("Registering %s shim", SHIM_NAME);
#define shim_reg_ok() pr_loc_inf("Successfully registered %s shim", SHIM_NAME);
#define shim_reg_already() do {                                                  \
    pr_loc_bug("Called %s while %s() shim is already", __FUNCTION__, SHIM_NAME); \
    return -EALREADY;                                                            \
} while(0)
#define shim_ureg_in() pr_loc_dbg("Unregistering %s shim", SHIM_NAME);
#define shim_ureg_ok() pr_loc_inf("Successfully unregistered %s shim", SHIM_NAME);
#define shim_ureg_nreg() do {                                                                  \
    pr_loc_bug("Called %s() while %s shim is not registered (yet?)", __FUNCTION__, SHIM_NAME); \
    return -ENXIO;                                                                             \
} while(0)
#define shim_reset_in() pr_loc_inf("Forcefully resetting %s shim", SHIM_NAME);
#define shim_reset_ok() pr_loc_inf("Successfully reset %s", SHIM_NAME);


#endif //REDPILL_SHIM_BASE_H
