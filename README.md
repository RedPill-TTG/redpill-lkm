# 💊 RedPill LKM

---

## THIS IS WORK IN PROGRESS
There's nothing to run/see here (yet ;)).

---

## What is this?
This is a major part of a tool which will be able to run a DSM instance for research purposes without
engaging your real DS machine and risking your data in the process (ask me how I know...).

## Target audience
This repository is target towards **developers** willing to learn and help with implementation of peculiarities of 
Synology's DSM Linux distribution.

Read about the quirk in a separate repo: https://github.com/RedPill-TTG/dsm-research/tree/master/quirks

## How to build with Linux sources?
1. You need Synology's GPL sources for the kernel. Check the [Makefile](Makefile) for details
2. `cd` to kernel sources
3. Depending on the version:  
   - **Linux v3**
      - `cp synoconfigs/bromolow .config`
   - **Linux v4**
      - `cp synoconfigs/apollolake .config`
      - `echo '+' > .scmversion` (otherwise it will error-out loading modules)
4. `make oldconfig ; make modules_prepare`
5. `cd` back to the module directory
6. `make LINUX_SRC=....` (path to linux sources, default: `../linux-3.10.x-bromolow-25426`)
7. You will get a `redpill.ko` module as the result, you can `insmod` it


## How to build with syno toolkit?
The procedure to build with the toolkit is **not recommended**. However, some versions lack the kernel sources 
(e.g. v7 now) and thus can only use this method.

1. Get the appropriate toolkit from the [official SF repo](https://sourceforge.net/projects/dsgpl/files/toolkit/)
    - You want to get the `.dev.txz` file for the corresponding platform (e.g. `ds.bromolow-7.0.dev.txz`)
    - You only need to unpack a part of it: `tar -xvf ds.bromolow-7.0.dev.txz usr/local/x86_64-pc-linux-gnu/x86_64-pc-linux-gnu/sys-root/usr/lib/modules/DSM-7.0/build`
    - If the path above changed you can use `tar -tvf ds.bromolow-7.0.dev.txz | grep kfifo.h` to find the correct one
2. `cd` to the module directory
3. `make LINUX_SRC=<toolkit-directory>/usr/local/x86_64-pc-linux-gnu/x86_64-pc-linux-gnu/sys-root/usr/lib/modules/DSM-7.0/build`
4. You will get a `redpill.ko` module as the result, you can `insmod` it


## Additional make options
While calling `make` you can also add these additional modifiers (e.g. `make FOO BAR`):
 - `DBG_EXECVE=y`: enabled debugging of every `execve()` call with arguments
 - `STEALTH_MODE=#`: controls the level of "stealthiness", see `STEALTH_MODE_*` in `internal/stealth.h`; it's 
   `STEALTH_MODE_BASIC` by default
 - `LINUX_SRC=...`: path to the linux kernel sources (`./linux-3.10.x-bromolow-25426` by default)

On Debian-based systems you will need `build-essential` and `libssl-dev` packages at minimum.

## Documentation split
The documentation regarding actual quirks/mechanisms/discoveries regarding DSM is present in a dedicated research repo 
at https://github.com/RedPill-TTG/dsm-research/. Documentation in this repository is solely aimed to explain 
implementation details of the kernel module. It will mostly be available in forms of long(ish) doc blocks.
