#ifndef REDPILL_SHIM_BASE_H
#define REDPILL_SHIM_BASE_H

#define shim_reg_in() pr_loc_dbg("Registering %s shim", SHIM_NAME);
#define shim_reg_ok() pr_loc_inf("Successfully registered %s shim", SHIM_NAME);
#define shim_ureg_in() pr_loc_dbg("Unregistering %s shim", SHIM_NAME);
#define shim_ureg_ok() pr_loc_inf("Successfully unregistered %s shim", SHIM_NAME);

#endif //REDPILL_SHIM_BASE_H
