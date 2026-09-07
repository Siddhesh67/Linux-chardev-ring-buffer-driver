# mychardev — a ring-buffer character device driver

A small Linux kernel module that implements a character device (`/dev/mychardev`)
backed by a fixed-size **ring buffer**. It is meant as a teaching / reference
example of doing I/O correctly in kernel space: safe concurrency, blocking
readers, and a clean ioctl interface.

## What the driver does

- Registers a character device with a dynamically allocated major number and
  auto-creates `/dev/mychardev` via a device class (with a `uevent` handler that
  sets the node mode to `0666`).
- **`write(2)`** copies bytes from userspace into a 256-byte ring buffer. Writes
  are truncated to the space currently available; the return value tells the
  caller how many bytes were actually stored.
- **`read(2)`** drains bytes from the ring buffer in FIFO order. If the buffer is
  empty the reader **blocks** on a wait queue until a writer adds data (unless the
  file was opened `O_NONBLOCK`, in which case it returns `-EAGAIN`).
- All shared state (`ring_buf`, `head`, `tail`, `count`) is protected by a
  **mutex**, so concurrent readers and writers cannot corrupt the buffer.
- **`ioctl(2)`** exposes two commands (see `mychardev_ioctl.h`):
  - `MYCHARDEV_GET_COUNT` — copies the current number of buffered bytes back to
    userspace as an `int`.
  - `MYCHARDEV_CLEAR` — resets the buffer to empty (`head = tail = count = 0`).

A writer calls `wake_up_interruptible()` after adding data so any blocked reader
is woken.

## Design decisions

### Ring buffer instead of a flat buffer

A flat buffer forces a choice between two bad options: either memmove the
remaining bytes down to the front on every read (O(n) copy, and it churns the
whole buffer under the lock), or keep advancing a read pointer and periodically
"compact" the buffer when it fills up. A ring buffer makes both `read` and
`write` O(bytes transferred) with no shuffling: `head` and `tail` chase each
other around the array modulo `RING_BUF_SIZE`, and `count` disambiguates the
full case from the empty case (both of which otherwise look like `head == tail`).
It models a producer/consumer byte stream directly, which is exactly what a
character device is.

### Mutex instead of a spinlock

The critical sections here can sleep, and that decides it. `read` may need to
wait for data, and while a spinlock holder must never sleep, a mutex holder can.
Even the copy paths matter: although this driver deliberately keeps
`copy_to_user` / `copy_from_user` outside the lock (see below), the general rule
for a device like this is that contention is low and the work under the lock is
short but not sub-microsecond, so the sleeping-friendly mutex is the right
primitive. A spinlock would also disable preemption on the holding CPU for no
benefit. There is no interrupt-context access to the buffer, so the classic
reason to reach for a spinlock (sharing data with an ISR) does not apply.

### `copy_to_user` / `copy_from_user` stay outside the lock

These helpers can **page-fault and sleep** (the user page may be swapped out or
not yet faulted in). Holding the mutex across a fault would mean sleeping on
disk I/O with the buffer locked, blocking every other reader and writer for
potentially milliseconds — and it invites lock-ordering problems with the mm
subsystem. The pattern used here:

1. Take the mutex.
2. Copy between the ring buffer and a small **on-stack** bounce buffer (`tmp`).
3. Drop the mutex.
4. `copy_to_user` / `copy_from_user` against the bounce buffer.

On the write side the copy-in happens *before* the lock is taken; on the read
side the copy-out happens *after* the lock is dropped. The lock only ever covers
plain memory moves that cannot sleep.

### `wait_event_interruptible` needs a `while`, not an `if`

The read path is:

```c
while (count == 0) {
    mutex_unlock(&ring_mutex);
    if (wait_event_interruptible(read_queue, count != 0))
        return -ERESTARTSYS;
    if (mutex_lock_interruptible(&ring_mutex))
        return -ERESTARTSYS;
}
```

Being woken is not a promise that the condition still holds:

- **Thundering herd / stolen wakeup:** `wake_up_interruptible` wakes *all*
  blocked readers. If two readers wake and the writer only added one byte, the
  first reader to re-acquire the mutex takes it and the second finds
  `count == 0` again.
- **Wakeup vs. re-lock race:** we drop the mutex before sleeping and re-take it
  after waking. Another thread can run a `CLEAR` ioctl or a competing read in
  that window.
- **Spurious wakeups** are permitted by the API in general.

So after waking we must **re-test `count` and go back to sleep if it is still
zero**. An `if` would fall through and read from an empty buffer. The condition
is also re-checked while holding the mutex, so the check and the subsequent
buffer access are atomic with respect to other threads.

## Files in this repo

| File | Purpose |
|------|---------|
| `chardev.c` | The character device driver: ring buffer, mutex, wait queue, `read`/`write`/`ioctl`, module init/exit and device-node creation. |
| `mychardev_ioctl.h` | Shared ioctl command definitions (`MYCHARDEV_GET_COUNT`, `MYCHARDEV_CLEAR`) and magic number. Included by both the driver and the userspace tests. |
| `Makefile` | Kbuild makefile — builds `chardev.ko` against the running kernel's headers. |
| `hello.c` | A minimal standalone "hello world" LKM, kept as the starting-point reference. Not built by the default `Makefile` target. |
| `test_chardev.c` | Userspace test: open, write a message, reopen, read it back, check EOF on the second read. |
| `test_block.c` | Userspace test: forks; the child does a blocking `read()` on an empty device, the parent writes 2 seconds later to unblock it. |
| `test_ioctl.c` | Userspace test: writes 10 bytes, then exercises `GET_COUNT` / `CLEAR` / `GET_COUNT` via `ioctl`. |
| `.gitignore` | Excludes kernel build artifacts and the compiled test binaries. |

## Build

Requires the matching kernel headers/build tree for your running kernel
(`/lib/modules/$(uname -r)/build`) and a toolchain — on Debian/Ubuntu:

```sh
sudo apt install build-essential linux-headers-$(uname -r)
```

Build the module:

```sh
make            # produces chardev.ko
```

Build the userspace tests:

```sh
cc -o test_chardev test_chardev.c
cc -o test_block   test_block.c
cc -o test_ioctl   test_ioctl.c
```

Clean kernel build artifacts:

```sh
make clean
```

## Test

Load the module (creates `/dev/mychardev`):

```sh
sudo insmod chardev.ko
dmesg | tail            # should show "registered with major number ..." and "device created at /dev/mychardev"
```

Run the tests (they open `/dev/mychardev`; the `uevent` handler makes the node
`0666`, so they should not need root — use `sudo` if your setup differs):

```sh
./test_chardev          # basic write / read / EOF
./test_ioctl            # GET_COUNT and CLEAR
./test_block            # blocking read unblocked by a later write
```

Watch kernel-side logging while testing:

```sh
dmesg -w
```

Unload when done:

```sh
sudo rmmod chardev
```
