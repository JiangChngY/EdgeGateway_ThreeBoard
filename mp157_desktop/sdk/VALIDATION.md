# ARM SDK / desktop validation — 2026-09-06

**Result: both production applications compile as ARM hard-float, pass recursive versioned-import checks against the unchanged NFS runtime, and pass their ARM/QEMU software-rendered UI regressions.** No board deployment was performed by the SDK builder.

## Production artifacts

| File | Bytes | SHA-256 |
|---|---:|---|
| `artifacts/edge-desktop` | 111476 | `c8df5da7acdbbc946c185578291dd17745bb4a38a8003011b022720ac93509ea` |
| `artifacts/systemui` | 935764 | `c4d73fd1ad7ea6821468d625d6f7c6cf619f26844031221fd56dc5ffd05ec186` |

Both include the UTF-16 integration identity `edgegateway.backend.v1`. The production `systemui` is stripped by the original project's post-link rule; the instrumented UI-test executable is separate and is **not** in the deployment artifacts.

VM locations: SDK `$HOME/edgegateway_desktop_sdk`; qmake `bin/qmake`; production binaries `build-desktop/edge-desktop` and `build-systemui/systemui`. The final archive was copied to `local staging directory: edge-sdk-artifacts-final.tar.gz` with top-level `artifacts/`.

## Evidence

- Toolchain: verified official Arm GCC **9.2.1**; Qt **5.12.9**, Cortex-A7 / NEON / VFPv4 / hard-float. Generated target configuration records `qreal=double`, GLES2, large-file support and disabled reduce-relocations. Provenance and the two supplemental public module declarations are described in `README.md` and `artifacts/sdk-manifest.json`.
- ELF: both are ELF32 ARM EABI hard-float with interpreter `/lib/ld-linux-armhf.so.3`. The independent application's direct requirements are GLIBC **2.4** and GLIBCXX **3.4**; full systemui's highest direct requirements are GLIBC **2.29** and GLIBCXX **3.4.21**. The target supplies glibc **2.31** and libstdc++ **6.0.28** / GLIBCXX **3.4.28**.
- `verify_abi.py` passes against the original `/srv/nfs/edgegateway/mp157`, not only the SDK copy: **23** recursively resolved dependencies for edge-desktop and **47** for systemui. Version tags and actual strong versioned imports are checked, including Pulse's private RPATH directory. See `artifacts/abi/*.log`.
- Link maps show Qt, CRT, libc, `libc_nonshared`, libstdc++ and `libgcc_s` from the SDK's target-copy libraries. Compiler-owned `crtbegin/crtend` and support archives remain compiler inputs. See `artifacts/abi/*.map`.
- QEMU invoked the existing target loader with the copied runtime, and tests used eager binding (`LD_BIND_NOW=1`), target xcb/QML/SVG/SQLite plugins, software Qt Quick and private Xvfb. Independent screenshot smoke passed. The initial `offscreen` platform loaded but returned an empty `grabWindow()`; screenshot acceptance uses xcb/Xvfb instead.
- Independent ARM UI regression: simulated PTY input, automatic serial opening, `25.5` fixture rendering, tab taps, asynchronous history, stable port selection, stale-data hiding and offline timeout all passed. These readings are **synthetic fixtures**, not hardware measurements.
- Full vendor ARM UI regression: new SVG desktop icon, opening the gateway Activity, returning to the desktop and reopening all passed. Runtime logs contain no checked missing QML-type/module/resource errors. Screenshots were also visually inspected: Chinese glyphs and the new icon render correctly.
- The four recorded source hashes (QtCore, QtQuick, libc and libstdc++) still match the NFS runtime. SDK construction and tests never installed libraries or replaced files there.

## Remaining hardware acceptance

Real board boot, linuxfb rendering, touch coordinates, physical serial wiring/sensors, GPU, audio and camera remain untested. Passing software-rendered QEMU tests does not establish those results. Only the main task may perform the separately authorized, backed-up offline NFS installation.
