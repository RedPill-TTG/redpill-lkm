# This CMakeLists file is for usage with CLion (and maybe other) IDEs ONLY. Do NOT attempt to build the project with
# CMake as it will fail (kernel build process is tailored for Makefile while CLion's support for Makefile is... meh)

cmake_minimum_required(VERSION 3.0)
project(redpill C)

set(CMAKE_C_STANDARD 11)
include_directories(./linux-3.10.x-bromolow-25426/include)
include_directories(./linux-3.10.x-bromolow-25426/arch/x86/include)


add_definitions(-D__KERNEL__)
add_definitions(-DMODULE)
add_definitions(-DCONFIG_MODULES)
add_definitions(-DCONFIG_MODULE_SIG)
add_definitions(-DCONFIG_KALLSYMS)
add_definitions(-DCONFIG_SMP)
add_definitions(-DCONFIG_MODULE_UNLOAD)
add_definitions(-DCONFIG_X86_64)
add_definitions(-DCONFIG_64BIT)
add_definitions(-DCONFIG_COMPAT)

add_executable(redpill
        redpill_main.c redpill_main.h internal/call_protected.c internal/call_protected.h common.h config/cmdline_delegate.c config/cmdline_delegate.h shim/boot_device_shim.c shim/boot_device_shim.h internal/stealth.c internal/stealth.h config/runtime_config.c config/runtime_config.h test.c shim/bios_shim.c shim/bios_shim.h internal/override_symbol.c internal/override_symbol.h shim/bios/bios_shims_collection.c shim/bios/bios_shims_collection.h shim/block_fw_update_shim.c shim/block_fw_update_shim.h internal/intercept_execve.c internal/intercept_execve.h shim/disable_exectutables.c shim/disable_exectutables.h debug/debug_execve.c debug/debug_execve.h)
