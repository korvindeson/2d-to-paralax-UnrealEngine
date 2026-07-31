# FaceParallax — Agent Guide

Product docs: README.md

## Rules

1. **`EFaceAngleState` lives in `FaceParallaxTypes.h`** — include that header, never redeclare.
2. **Module API macro** is `FACEPARALLAX_API`.
3. **Never break the Python syntax validator** — all `.h`/`.cpp` files must parse cleanly.
4. **Never break the C++ math tests** — component logic must match test expectations.
5. **After editing C++ files, run `python _gen_embed.py`** — re-encodes sources into `deploy.py`'s `EMBEDDED_SOURCES`.
6. **Keep `README.md` in sync** with API changes.
7. **Prefer editing existing files** — no new files unless necessary.
8. **Widget code uses direct member pointers** (`SliderOrbitYaw`, `PreviewImageWidget`) — no string-based dispatch.
9. **UE5.8 compiler**: `H:\unreal\UE_5.8\Engine\Build\BatchFiles\Build.bat` (run_tests.ps1 finds it via PATH).

## Verify

**After EVERY change — C++, Python, docs, or files deleted — run the full suite before finishing:**

1. `python _gen_embed.py` — re-encode sources into `deploy.py`'s `EMBEDDED_SOURCES` (must say `[OK] Patched`).
2. `.\Tests\run_tests.ps1 -IncludeUEBuild` — checks embed staleness, syncs SAMPLES, runs the Python syntax validator, compiles and runs the C++ math tests (all must pass), and builds the UE5.8 module with UBT (must pass).
3. If the editor subsystem/widget changed: run the headless probe and check `SAMPLES\MyProject\Saved\Logs\MyProject.log` for the `[FaceParallax]` markers.
4. If the editor is open (Live Coding active), the UE build will fail — close it and re-run the suite before declaring success.
