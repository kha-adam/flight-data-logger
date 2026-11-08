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

## ADR-003: Timer, spinlock, drop-newest ring

**Context.** Samples have to appear on their own, at a steady rate, while a slower reader is in `read`. The producer and the reader share one buffer. The producer runs from a timer interrupt. The reader runs from a system call.

**Options.**

1. **Timer.** `hrtimer` fires in hard interrupt context with a nanosecond period. `timer_list` is coarser (jiffies, typically 1 ms or 4 ms). `delayed_work` runs in process context, may sleep, and has more jitter. A kthread is a process-context loop and needs its own sleep logic.
2. **Lock.** A spinlock taken with `spin_lock_irqsave` works in both the timer and the reader. A mutex can sleep, so the timer must not take one. A lock-free ring is a later stretch goal.
3. **Overrun.** When the ring is full, drop the new sample, count it, and still advance `seq`. Overwriting the oldest sample would keep the freshest data and would also make `seq` jump, but the reader would lose the sample it was about to consume.
4. **When the timer runs.** Start it in `open`, stop it with `hrtimer_cancel` in `release`. Running from module init would produce samples nobody is reading.
5. **Buffer.** 1024 entries, a power of two, free-running `head` and `tail`. Index with `head & 1023`. The full condition is `head - tail == 1024`, so every slot is usable. A "leave one slot empty" ring wastes a slot to tell full from empty.

**Decision.** `hrtimer`, `spin_lock_irqsave`, drop-newest, timer tied to open/release, 1024-sample ring, default rate 100 Hz. One opener: a second `open` returns `-EBUSY`. `read` on an empty ring returns `-EAGAIN` until the blocking wait exists.

**Consequences.**

- `seq` counts every sample the timer creates, including ones that do not fit. After the ring has been full, a slow reader sees jumps in `seq`. Those jumps are the dropped samples.
- The first samples can still be contiguous. Gaps show up only after the ring fills and the reader then frees a slot.
- `copy_to_user` stays outside the spinlock. The lock is held only around the pop.
- `hrtimer_cancel` runs without that spinlock held, and it runs before the buffer is freed. The callback and `kfree` must not overlap.
- The ring is allocated with `kcalloc` and `GFP_KERNEL` from `telemetry_init`, which is process context. The timer does not allocate.

## ADR-004: Block in `read`, wake from the timer

**Context.** A reader that retries on `-EAGAIN` burns a CPU while it waits for the next sample. Programs also need `poll` so they can wait on this device together with other files. `O_NONBLOCK` must keep the old "try once" behaviour.

**Options.**

1. **Waiting.** `wait_event_interruptible` puts the process to sleep and checks the condition again after every wake, including wakes that arrive just as it is about to sleep. A userspace retry loop stays runnable the whole time. `msleep` in a loop would also sleep, and it would wake even when no sample exists.
2. **How many samples leave the lock.** Pop up to 16 samples into a local array, drop the spinlock, then `copy_to_user` the array. Popping one is simpler and takes the lock more often. Copying while the lock is held is not available: `copy_to_user` can sleep.
3. **A short `copy_to_user`.** The samples already left the ring. Count every sample that did not reach the program as `dropped`, and return the bytes that did. Returning `-EFAULT` for the whole call is what we do when zero bytes landed.

**Decision.** Sleep in `read` on `telem.wq`. The timer calls `wake_up_interruptible` after it pushes. `O_NONBLOCK` still returns `-EAGAIN`. `poll` uses the same queue. Batch size is 16. A failed photocopy counts the missing samples as dropped.

**Consequences.**

- A blocked `read_one` should sit near 0% CPU in `top`. Ctrl-C wakes the sleep through the signal path.
- `wait_event_interruptible` can return `-ERESTARTSYS`. `read` returns that value unchanged.
- `poll` on an empty device returns 0 when the timeout expires. It returns `EPOLLIN | EPOLLRDNORM` when a sample is waiting.
- Sixteen samples are 384 bytes, which fits on the kernel stack.

## ADR-005: ioctl for control, `/proc` for a text snapshot

**Context.** The rate and the counters are compiled into the module. Changing the rate means rebuilding. A test has no way to read `dropped` without adding printks.

**Options.**

1. **Control path.** `ioctl` is one system call with a command number and a fixed-size argument. sysfs is one text file per value, easy to `cat` and clumsy for a whole stats struct. Writing text commands into the device mixes those commands with the binary sample stream.
2. **Human-readable stats.** `/proc` through `seq_file` is a text file any user can `cat`. debugfs is a developer tree and is sometimes not mounted.

**Decision.** Three ioctls already numbered in `telemetry_uapi.h`: set rate, get stats, reset counters. The same stats are printed from `/proc/telemetry_stats`. The rate may also be set at `insmod` time with a module parameter. The buffer length is chosen at load and then stays fixed, because the array is already allocated.

**Consequences.**

- A rate outside 1..10000 Hz returns `-EINVAL`. An unknown command returns `-ENOTTY`.
- `copy_from_user` and `copy_to_user` stay outside the spinlock. The new rate is stored under the lock. The timer copies it into a local variable while the lock is held and uses that local for the next delay.
- Reset clears `dropped`, `consumed`, and `high_watermark`. It does not clear `seq` or the slots, so a reader does not see ticket numbers jump backward.
- `produced` reported to userspace is the current `seq`: every sample the timer created, including dropped ones.
- Counters stay still while nobody holds `/dev/telemetry` open, because `open` starts the timer and `release` stops it.

## ADR-006: Two userspace threads and a bounded queue

**Context.** Writing each sample to a file is slow. If the same thread reads `/dev/telemetry` and writes the file, a slow disk stall stops the reads. The kernel ring fills, new samples are dropped, and `seq` jumps.

**Options.**

1. **One thread.** Simple. A slow `write` blocks the next `read`, so the kernel drops samples during every disk stall.
2. **Two threads and a bounded queue.** The reader thread only calls `read`. The writer thread only writes CSV. A full queue makes the reader wait, which is backpressure: the kernel ring then fills and drops are visible in `seq`.
3. **An unbounded queue.** The reader never waits. A slow writer lets the queue grow without limit and the process runs out of memory.

**Decision.** One reader thread and one writer thread, connected by a fixed-capacity queue of samples. The queue uses a `pthread_mutex_t` and two condition variables. Both sides are ordinary threads, so they are allowed to sleep. The kernel spinlock stays in the driver because the timer cannot sleep.

**Consequences.**

- `--device PATH` lets the same reader run against `/dev/telemetry` or against a FIFO, so the queue can be tested without loading the module.
- Closing the queue wakes both sides. A pop on a closed empty queue returns an end marker so the writer can finish the file and exit.
- Gap detection compares `seq` with the previous sample's `seq + 1`. A jump is a loss, and it is the same loss the kernel counted in `dropped` once the ring has overflowed.
