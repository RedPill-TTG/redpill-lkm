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

## How to build?
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
6. `make LINUX_SRC=....` (path to linux sources, default: `./linux-3.10.x-bromolow-25426`)
7. You will get a `redpill.ko` module as the result, you can `insmod` it

While calling `make` you can also add these additional modifiers (e.g. `make FOO BAR`):
 - `DBG_EXECVE`: enabled debugging of every `execve()` call with arguments
 - `STEALTH_MODE=#`: controls the level of "stealthiness", see `STEALTH_MODE_*` in `internal/stealth.h`; it's 
   `STEALTH_MODE_BASIC` by default
 - `LINUX_SRC=...`: path to the linux kernel sources (`./linux-3.10.x-bromolow-25426` by default)

On Debian-based systems you will need `build-essential` and `libssl-dev` packages at minimum.

## Documentation split
The documentation regarding actual quirks/mechanisms/discoveries regarding DSM is present in a dedicated research repo 
at https://github.com/RedPill-TTG/dsm-research/. Documentation in this repository is solely aimed to explain 
implementation details of the kernel module. It will mostly be available in forms of long(ish) doc blocks.
