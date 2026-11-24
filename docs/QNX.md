# Building and running qport on QNX OS 8.0

These steps use the free QNX Everywhere licence and a QEMU target, so no board is needed.

## 1. Install QNX SDP 8.0

1. Get a free non-commercial licence from QNX Everywhere: https://qnx.software/en/developers/get-started/qnx-everywhere
2. Install the QNX Software Center on a Linux x86_64 or Windows host (the QNX cross compiler runs on Linux and Windows hosts).
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

This runs `qcc -Vgcc_nto<arch> -D_QNX_SOURCE` and writes `build-qnx-<arch>/qport` and `build-qnx-<arch>/unit`.

With CMake instead (the toolchain files in `cmake/` were written by `qport toolchain --out cmake src`):

```sh
cmake -B build-qnx -DCMAKE_TOOLCHAIN_FILE=cmake/qnx-x86_64.cmake
cmake --build build-qnx
```

## 3. Check headers against the real SDP

```sh
make qnx-test
```

This runs the host `qport` with `--sysroot "$QNX_TARGET"`: every `#include <...>` in `src/` and in the example is looked up in the QNX 8.0 target headers.

## 4. Run on a QEMU target

1. Set up the QNX 8.0 Quick Start Target Image for QEMU (QSTI for QEMU guide in the QNX Everywhere documentation) and boot it.
2. Copy the binary and the example to the target, for example with `scp`:

   ```sh
   scp build-qnx-x86_64/qport build-qnx-x86_64/unit root@<target-ip>:/tmp/
   scp -r examples root@<target-ip>:/tmp/
   ```

3. On the target:

   ```sh
   cd /tmp
   ./unit
   ./qport scan examples/linux-telemetryd
   ./qport scan -f html -o report.html examples/linux-telemetryd
   ```
