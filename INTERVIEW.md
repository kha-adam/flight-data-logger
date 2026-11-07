# Interview bank

Adam's teach-back answers, then the version to defend. Say these out loud before the internship interview.

## Step 0

### What happens on `insmod`?

**Adam.** "insmod loads the hello module into the kernel."

**Grade.** Partial. The outcome is right. The mechanism is missing. Adam asked to continue and revisit this before the interview.

**Defend this.** `insmod` is a normal program. It opens `hello.ko` and makes the `finit_module` system call. The kernel checks vermagic against the running kernel, copies the relocatable ELF into kernel memory, fixes up addresses, resolves symbols such as the logging function, then calls `hello_init` in the process context of `insmod`. Return `0` and the module stays loaded. A negative errno and the kernel drops it. `hello_exit` is not called on that failure path.

### Why `MODULE_LICENSE("GPL")`, and what were those two taint lines?

**Adam.** Did not know yet.

**Grade.** Gap.

**Defend this.** Some kernel functions are exported as `EXPORT_SYMBOL_GPL`. The loader lets a module with `MODULE_LICENSE("GPL")` link against them. Any other license taints the kernel as proprietary (`P`) and those symbols stay unresolved.

The two lines from this boot are a different pair of taints, and they happen with a GPL module:

- `loading out-of-tree module taints kernel` sets flag `O`. Ubuntu did not build this `.ko`.
- `module verification failed: signature and/or required key missing` sets flag `E`. The `.ko` is unsigned. The load still finished. A refused load would have printed `Key was rejected by service` from `insmod` itself.

Taint sticks until reboot. `cat /proc/sys/kernel/tainted` prints the bitmask. `12288` is bit 12 (`O`) plus bit 13 (`E`).

### `init` allocates, then fails before `return 0`. What runs?

**Adam.** Did not know yet.

**Grade.** Not restated yet. Revisit with question 1.

**Defend this.** Only `hello_init` runs. `hello_exit` does not, because the module never finished loading. Free every allocation from this attempt, then `return` a negative errno such as `-ENOMEM`. The module is not present in `lsmod`.

### Why is `printf` or a `float` unavailable?

**Adam.** `printf` would loop, because that library calls the kernel. The kernel cannot use floating-point registers, so it cannot process `float` variables.

**Grade.** Pass, with one correction on the FPU.

**Defend this.** `printf` lives in glibc, in user space. glibc implements it by making system calls into the kernel. The kernel is not linked with glibc, and calling it would be circular. Logging is `pr_info`. Allocation, later, is `kmalloc`.

The CPU does have floating-point registers. Kernel entry paths do not save them. A `float` instruction in the module would overwrite the registers of whatever process was interrupted. The kernel is built so that normal C does not emit those instructions. Sensor values in this project stay integers.
