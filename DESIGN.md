# Design log

Short architecture decision records for `telemetry-lkm`. Each entry is something Adam must be able to defend.

## ADR-001: Loadable out-of-tree module, developed only in a VM

**Context.** The project needs a character driver that Adam can build, load, break, and unload many times in a weekend. He has never written a kernel module. The host is a Mac; the target is Ubuntu 24.04 with kernel headers.

**Options.**

1. **Loadable kernel module (LKM), out of tree.** Built against `/lib/modules/$(uname -r)/build`, inserted with `insmod`, removed with `rmmod`. Init failure returns an errno and the module stays unloaded. Exit must undo everything init did.
2. **Built-in driver.** Compiled into vmlinux. Every change is a kernel rebuild and reboot. Right for a board bring-up that must exist before userspace; wrong for a weekend iteration loop.
3. **Userspace driver** (UIO, `/dev/mem`, or libgpiod). A normal process talks to hardware through a kernel-provided window. Safer and easier to debug, and it cannot teach module lifecycle, `file_operations`, atomic context, or `copy_to_user`.

**Decision.** Option 1, and only inside an Ubuntu 24.04 VM (UTM on Apple Silicon is the expected setup) with at least 2 vCPUs and 4 GB RAM. Never `insmod` on the Mac host. Snapshot the VM before any experiment that can panic.

Step 0 uses a throwaway `hello/` module to prove the toolchain. It is deleted once the checkpoint passes; it is not part of the telemetry driver.

**Consequences.**

- The module must match the running kernel (`vermagic`). Headers come from `linux-headers-$(uname -r)`.
- `MODULE_LICENSE("GPL")` is required to use GPL-only kernel symbols and to avoid a proprietary taint.
- A failed `init` must free anything it already allocated and return a negative errno. `exit` cannot fail.
- Secure Boot in the VM rejects unsigned modules ("Key was rejected by service"). Disable it in the VM firmware, or sign the module.
- WSL2 is out of scope: module loading is not the default path there.
