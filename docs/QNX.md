# Building and running qport on QNX OS 8.0

This is what was done on 2026-11-26 to build qport with the QNX SDP 8.0 toolchain and run it on a QNX OS 8.0 target. It needs no board: the target is a QEMU virtual machine.

| | |
|---|---|
| QNX version | QNX OS 8.0, QNX Software Development Platform 8.0 (free QNX Everywhere non-commercial licence) |
| Host | Linux x86_64, Ubuntu 24.04 (a cloud VM with `/dev/kvm`) |
| Toolchain | `qcc` 12.2.0, targets `gcc_ntox86_64` and `gcc_ntoaarch64le` |
| Target | QNX OS 8.0 `x86_64` image built with `mkqnximage --type=qemu`, run in QEMU 8.2.2 with KVM |
| Results | [`assets/qnx-build-x86_64.log`](../assets/qnx-build-x86_64.log), [`assets/qnx-build-aarch64le.log`](../assets/qnx-build-aarch64le.log), [`assets/qnx-header-check.log`](../assets/qnx-header-check.log), [`assets/qnx-sysroot-crosscheck.log`](../assets/qnx-sysroot-crosscheck.log), [`assets/qnx-target-run.log`](../assets/qnx-target-run.log) |

## 1. Install QNX SDP 8.0

1. Get a free non-commercial licence from QNX Everywhere: https://qnx.software/en/developers/get-started/qnx-everywhere
2. Install the QNX Software Center on a Linux x86_64 or Windows host (the QNX cross compiler runs on Linux and Windows hosts, not macOS).
3. In the Software Center, install **QNX Software Development Platform 8.0**. It lands in `~/qnx800` by default.
4. In every shell you build from:

   ```sh
   source ~/qnx800/qnxsdp-env.sh   # sets QNX_HOST, QNX_TARGET and PATH
   qcc -V                          # lists the gcc_nto* targets
   ```

## 2. Build

```sh
make qnx QNX_ARCH=x86_64         # QEMU target
make qnx QNX_ARCH=aarch64le      # Raspberry Pi 4/5 and other ARM boards
```

This runs `qcc -Vgcc_nto<arch> -D_QNX_SOURCE -O2 -g -Wall` and writes `build-qnx-<arch>/qport` and `build-qnx-<arch>/unit`. Both architectures build with no warnings. `file` reports QNX ELF executables with the interpreter `/usr/lib/ldqnx-64.so.2`.

The `aarch64le` binaries were built but have not been run on ARM hardware.

The repository also has a CMake build with toolchain files that `qport toolchain --out cmake src` wrote (`cmake -B build-qnx -DCMAKE_TOOLCHAIN_FILE=cmake/qnx-x86_64.cmake`). The run above used `make qnx`; the CMake route has not been tried against the SDP yet.

## 3. Check headers against the real SDP

```sh
make qnx-test
```

This runs the host `qport` with `--sysroot "$QNX_TARGET"`: every `#include <...>` in `src/` and in the example is looked up in the QNX 8.0 target headers (`header_in_sysroot()` in `src/scan.c`). qport's own source is clean. [`assets/qnx-sysroot-crosscheck.log`](../assets/qnx-sysroot-crosscheck.log) lists which of the example's headers exist in the SDP 8.0 sysroot next to the qport findings for them: each of the 8 missing headers is reported by a rule.

## 4. Make a QEMU target with mkqnximage

`mkqnximage` comes with SDP 8.0. With the SDP environment sourced:

```sh
mkdir ~/qnxvm && cd ~/qnxvm
mkqnximage --type=qemu --arch=x86_64 --hostname=qnx8 --ssh-ident=$HOME/.ssh/id_ed25519.pub
```

This writes `output/ifs.bin` (the boot image) and `output/disk-qemu.vmdk`, with your public key installed for `root` so ssh needs no password.

## 5. Boot it

`mkqnximage --run` starts QEMU with a bridged network, which needs a host bridge and root. On a cloud VM without one, start QEMU directly with user networking and forward a host port to the target's ssh:

```sh
cd ~/qnxvm
qemu-system-x86_64 -smp 2 -enable-kvm -cpu host -m 1G \
  -drive file=output/disk-qemu.vmdk,if=ide,id=drv0 \
  -netdev user,id=net0,hostfwd=tcp:127.0.0.1:10022-:22 -device virtio-net-pci,netdev=net0 \
  -object rng-random,filename=/dev/urandom,id=rng0 -device virtio-rng-pci,rng=rng0 \
  -kernel output/ifs.bin -display none -serial file:$HOME/qnxvm/serial.log \
  -pidfile output/qemu.pid -daemonize
ssh -p 10022 root@127.0.0.1 uname -a
# QNX qnx8 8.0.0 2026/10/30-09:10:05EDT x86pc x86_64
```

Stop it later with `kill $(cat output/qemu.pid)`.

## 6. Run qport on the target

Copy the binaries, the example and (for a self-scan) qport's source to the target, then run them there:

```sh
SSH="ssh -p 10022 root@127.0.0.1"
$SSH 'mkdir -p /data/home/root/qport'
tar cf - -C build-qnx-x86_64 qport unit | $SSH 'tar xf - -C /data/home/root/qport'
tar cf - examples src | $SSH 'tar xf - -C /data/home/root/qport'
$SSH
# on the target:
cd /data/home/root/qport
./unit                                        # unit: all checks passed
./qport scan examples/linux-telemetryd        # 17 errors, 16 warnings, 6 notes
./qport scan -f html -o report.html examples/linux-telemetryd
./qport scan src                              # 0 errors, 0 warnings, 0 notes
```

The full output is in [`assets/qnx-target-run.log`](../assets/qnx-target-run.log). `qport scan` exits with status 1 when it finds errors (the default `--fail-on error`), so the example scan ends with `exit=1` there. The `report.html` written on the target ([`assets/report-from-qnx.html`](../assets/report-from-qnx.html)) is byte-for-byte the same as the one the host build writes.
