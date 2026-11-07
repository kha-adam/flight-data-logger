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

## ADR-002: One misc device, fixed 24-byte records

**Context.** Userspace has to read sensor samples through a device node. The driver needs a registration method, a record layout, a read rule, and permissions that let the VM user run the test program.

**Options.**

1. **Registration.** `miscdevice` uses major 10 and a dynamic minor, and devtmpfs creates `/dev/telemetry` inside `misc_register`. `alloc_chrdev_region` plus `cdev_add`, a class, and `device_create` is the full character-device path: you pick the minor layout yourself, and you write more init and cleanup. That path earns its cost when one driver serves many device nodes.
2. **Record layout.** A fixed 24-byte binary struct matches the ABI in one `copy_to_user`. Text lines are readable with `cat`, and they split across reads, need a parser, and are slower.
3. **Read rule.** Whole records only: `count < sizeof(sample)` returns `-EINVAL`. A byte stream would let `read` return half a struct, so the driver or the user would have to remember a leftover fragment.
4. **Permissions.** Leave `miscdevice.mode` at 0 and the node is created `0600` (root only). Set `0644` and any user on the VM can read. Writes stay restricted to root.

**Decision.** `miscdevice`, binary 24-byte records, whole-record reads, mode `0644`. One sample per `read` until the ring buffer exists. Exclusive open and the timer arrive in the next step.

**Consequences.**

- `/proc/misc` lists the name and the assigned minor. Major is 10.
- `.owner = THIS_MODULE` makes `rmmod` fail while a file descriptor is open.
- `read` returns `24`, or `-EINVAL`, or `-EFAULT`. It returns `0` only for end-of-file, which this device does not have.
- A second process can open the device at the same time. The sequence counter is a plain increment until the lock arrives. Test with one reader.
- Changing the struct layout breaks every already-compiled reader. The size is checked with a static assertion.
