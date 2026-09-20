# MP157 Qt 5.12.9 application SDK

This is an isolated, **public-API application SDK**, reconstructed for this desktop and the integrated ALIENTEK `systemui`. It is not an original vendor SDK and must not be used to rebuild Qt, private-API plugins, or the board image. Nothing in these scripts upgrades the board's libc or writes to the NFS runtime. Installation is a separate, explicitly controlled step.

## Provenance

- Compiler: Arm GNU-A **9.2-2019.12**, GCC **9.2.1 20191025**, `arm-none-linux-gnueabihf`. [Official Arm download](https://developer.arm.com/-/media/Files/downloads/gnu-a/9.2-2019.12/binrel/gcc-arm-9.2-2019.12-x86_64-arm-none-linux-gnueabihf.tar.xz) was verified to redirect to Arm's `armkeil.blob.core.windows.net` storage. Official `.asc` MD5: `ae539d09dadacf7f22fcd6f54870e5ad`; SHA-256: `51bbaf22a4d3e7a393264c4ef1e45566701c516274dde19c4892c911caa85617`.
- C/C++ development headers: that compiler's own libc/glibc **2.30** sysroot and C++ **9.2.1** headers. No Ubuntu GCC13 or Ubuntu libc headers are used.
- Qt public headers, host qmake/moc/rcc, mkspec implementation and EGL/GLES registry headers: installed official Qt **5.12.9** package at `/opt/Qt5.12.9/5.12.9/{gcc_64,Src}`.
- Target libraries, CRT/libc link support, platform/QML plugins and fonts: independent copies from `/srv/nfs/edgegateway/mp157`. Target is OpenSTLinux Dunfell **3.1-snapshot-20210728**, Cortex-A7/NEON/VFPv4, ARM hard-float, Qt **5.12.9**, glibc **2.31**, libstdc++ **6.0.28**, originally built using GCC **9.3.0**.
- `qconfig.h` and module feature headers are regenerated from the **target** `qconfig.pri`, `qmodule.pri` and `modules/*.pri`. Official same-version `configure.json` declarations supply legacy aliases such as `QT_OPENGL_ES_2` and selected `QT_NO_*`; unresolved dynamic outputs are recorded in `sdk-manifest.json`. Desktop x86 `qconfig.h`, SSE capabilities and desktop `reduce_relocations` are not reused. Target metadata and exported symbols confirm `qreal=double`; target metadata confirms large-file support, NEON, visibility, and disabled `reduce_relocations`.
- The runtime lacks the public `multimedia` and `virtualkeyboard` qmake declarations and unversioned linker aliases. SDK-only declarations are derived from the same-version official public module fields after asserting no configurable public features/system-library uses; all host `QT_CONFIG` backend tokens are removed. Their exact target DSOs are retained. No private multimedia/keyboard backend metadata is fabricated.

The generated manifest records library source hashes and these exceptions. Firmware, kernel modules and Python packages are not application SDK dependencies and are excluded from the runtime copy. The SDK-only `libgcc_s.so` linker alias ensures the existing target runtime is selected instead of the compiler package's copy.

## Build and test

The current bootstrap intentionally accepts only `$HOME/edgegateway_desktop_sdk` for the invoking user; inspect and adapt it before using a different machine. Place this `sdk` directory together in the VM, then run:

```sh
bash sdk/bootstrap.sh
bash sdk/build_app.sh $HOME/edgegateway_desktop
python3 sdk/verify_abi.py /path/to/edge-desktop /srv/nfs/edgegateway/mp157
bash sdk/smoke.sh
python3 sdk/test_arm.py desktop
python3 sdk/test_arm.py vendor
```

`verify_abi.py BINARY [TARGET_ROOT]` is portable independently of the SDK: it uses system `readelf`, or `READELF` when set, and forces English tool output. Absolute target symlinks are resolved relative to `TARGET_ROOT`, never the host filesystem. The checker follows dependency RPATH/RUNPATH (including `$ORIGIN`), enforces containment in the target root, checks ELF32 ARM hard-float/interpreter, and compares required version tags and strong versioned imports recursively. It does not replace a C++ class-layout audit, unversioned/plugin testing, or hardware acceptance.

QEMU tests use the frozen target loader/libraries with eager binding, target xcb/QML/SVG/SQLite plugins, software Qt Quick rendering and a private Xvfb server. They do not occupy the interactive display. The independent UI test uses synthetic PTY frames; its displayed readings are **test fixtures**, not physical sensor measurements. A direct `offscreen` attempt loaded successfully but `grabWindow()` returned an empty image; screenshot acceptance therefore uses xcb/Xvfb.

## Acceptance boundary

Link maps must show target-copy Qt, CRT, libc, libc_nonshared, libstdc++ and libgcc_s. GCC's own `crtbegin/crtend` and compiler support archives are expected. Successful ARM/QEMU UI tests establish loading and tested software-rendered UI flows—not physical serial wiring, linuxfb, touchscreen, GPU, audio playback/recording, camera, or actual board boot. Those remain board-side checks. Production and test-instrumented binaries are kept in separate directories; only the production `systemui` is a deployment candidate.
