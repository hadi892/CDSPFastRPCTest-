# Qualcomm CDSP FastRPC Architecture & PoC Documentation

## Target Physical Device Profile
- **Device**: Samsung Galaxy Tab A9+ 5G (SM-X216B)
- **SoC**: Qualcomm Snapdragon 695 5G (SM6375 / Blair)
- **Architecture**: ARM64-v8a (Application Processor) + Qualcomm Hexagon Compute DSP (CDSP)
- **Android Version**: Android 16 (API Level 36)
- **Security Mode**: SELinux Enforcing, Non-Rooted, Stock Samsung Retail Firmware, Locked Bootloader

---

## Qualcomm FastRPC Call Architecture

In a standard Qualcomm FastRPC deployment, remote execution follows this pipeline:

```
+-------------------------------------------------------------+
| Android Kotlin Application (com.cdsfastrpctest)            |
+-------------------------------------------------------------+
                              | JNI
+-------------------------------------------------------------+
| ARM64 Native JNI Loader (libcdsp_loader.so)                |
+-------------------------------------------------------------+
                              | Dynamic Linking (dlopen/dlsym)
+-------------------------------------------------------------+
| Qualcomm FastRPC Userspace API (libcdsprpc.so)              |
+-------------------------------------------------------------+
                              | ioctl / mmap / open
+-------------------------------------------------------------+
| Linux Kernel FastRPC Driver (/dev/fastrpc-cdsp)             |
+-------------------------------------------------------------+
                              | Shared Memory / IPC (SMD/GLINK)
+-------------------------------------------------------------+
| Qualcomm Hexagon CDSP (Compute DSP Domain 3)                |
|  - FastRPC Skeleton (skel.so)                               |
|  - Remote Implementation (test_add)                         |
+-------------------------------------------------------------+
```

---

## FastRPC Technical Interfaces

### 1. FastRPC Scalar Descriptor Packing (`remote_handle_invoke`)
Qualcomm FastRPC encodes remote function arguments into scalar integers:
```c
#define REMOTE_SCALARS_MAKEX(attr, method, in, out, rout, oin) \
    ((((uint32_t)(attr))   & 0x7)   << 29 | \
     (((uint32_t)(method)) & 0x1fff)<< 16 | \
     (((uint32_t)(in))     & 0xff)  << 8  | \
     (((uint32_t)(out))    & 0xff))
```
For `test_add(int a, int b, int *result)`:
- Method Index: `0`
- Input Arguments: `2` (scalar integers `a` and `b`)
- Output Arguments: `1` (scalar integer `result`)
- Scalar: `REMOTE_SCALARS_MAKEX(0, 0, 2, 1, 0, 0)`

### 2. FastRPC Domain Selection
Qualcomm defines DSP domains as:
- Domain `0`: ADSP (Audio DSP)
- Domain `1`: MDSP (Modem DSP)
- Domain `2`: SDSP (Sensors DSP)
- Domain `3`: CDSP (Compute DSP) -> Target for SM6375 Blair

---

## Security & Runtime Boundaries on Stock Samsung Android 16

1. **Android Linker Namespace Restriction (`classloader-namespace`)**:
   Since Android 8.0 Oreo and reinforced in modern Android versions (Android 14/15/16), apps running in the standard application sandbox cannot load arbitrary shared libraries from `/vendor/lib64/` unless they are explicitly listed in `/vendor/etc/public.libraries.txt`. If `libcdsprpc.so` is not public on the device, `dlopen()` fails with namespace isolation error.

2. **SELinux Policy (`untrusted_app` -> `qdsp_device`)**:
   Under SELinux Enforcing, device nodes like `/dev/fastrpc-cdsp` and `/dev/adsprpc-smd` are labeled with `qdsp_device` or `fastrpc_device`. The SELinux policy for untrusted applications typically forbids `open` and `ioctl` operations on DSP character devices.

3. **Hexagon Shared Objects (`.so` / `.skel`)**:
   Real CDSP execution requires the presence of Hexagon ELF binaries compiled for Hexagon V66/V68 ISA using the Qualcomm Hexagon SDK (cross-compiler `hexagon-clang`). On retail devices, user-space unprivileged applications cannot dynamically push unsigned `.so` files into the DSP DSP search path (`/dsp/`, `/vendor/dsp/`, or `ADSP_LIBRARY_PATH`) without signature validation (DSP test signatures or OEM root keys).

---

## Strict Hardware Truth Policy

This project strictly adheres to honest diagnostic reporting:
- No CPU-side emulation or mock results are substituted for DSP execution.
- If Android 16 or Samsung SELinux policy blocks access to `libcdsprpc.so` or `/dev/fastrpc-cdsp`, the exact stage, return code, errno, and system error are recorded and displayed in the UI.
- `REAL CDSP EXECUTION = PASS` is recorded **ONLY** if the Qualcomm CDSP physically executes `test_add` and returns `3`.
