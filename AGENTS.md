# FaceParallax — Agent Guide

Product docs: README.md

## Layout

- **Root `G:\tailedstories\paralax` is canonical** — the 19 `.h`/`.cpp` sources at the root are the single source of truth. `Tests\run_tests.ps1` syncs them into `SAMPLES\MyProject\Plugins\FaceParallax\Source\` before every UE build; never edit plugin copies directly.
- **Plugin**: `SAMPLES\MyProject\Plugins\FaceParallax\` — two modules:
  - `FaceParallax` (Runtime, `PostDefault`) — component, preset, preview actor, depth debug visualizer, types.
  - `FaceParallaxEditor` (Editor, `PostDefault`) — editor subsystem, editor widget, deploy pipeline. The widget is split across `FaceParallaxEditorWidget.cpp` (core API), `FaceParallaxEditorWidgetUI.cpp` (`RebuildWidget` — slim skeleton wiring panel builders + head/marker code), `FaceParallaxEditorWidgetInteractions.cpp`, `FaceParallaxEditorWidgetPanels.cpp` (panel construction builders `BuildPanel*` + `MakeSectionBox` + refresh/rebuild functions), plus `FaceParallaxEditorWidgetShared.h` (anonymous-namespace helpers + `SFaceLayerGizmo` used by all widget TUs).
- **Phase H design contract**: `FaceParallaxLayoutSpec.h` (root, pure C++17, synced into the editor module Private dir) is the layout manifest + P1–P15 validator for the widget tree. `RebuildWidget` self-checks it; `Tests\ParallaxMathTests.cpp::TestPhaseHUIDesign` asserts zero violations, the exact 5-rail scroll-viewport set (rails 180×560 clipped in nested horizontal+vertical SScrollBoxes, `PR-Scroll` mirroring the props pane's `PropScroll`), mirrored design constants (P14: props pane keeps `PropsRightGap` from the window edge; P15: `PR-Scroll` keeps `PropsScrollInsetR` before its scrollbar), negative controls for every principle, and section auto-stack (manifest sections are never Flex in a clipped viewport and sibling rects never overlap). The Python validator additionally flags any real-widget section slot (`->AddSlot()` feeding a `MakeSectionBox`) that lacks `.AutoHeight()`/`.FillHeight()` — the "View Override paints over Scale Y" defect class. Pool order is child-first (args evaluate eagerly) — never index a child via `parent + N` arithmetic; use `Children[...]` lookups (`Bod(sec) = Children[1]`).
- **Stub host module**: `SAMPLES\MyProject\Source\MyProject\` — `IMPLEMENT_PRIMARY_GAME_MODULE(FDefaultModuleImpl, MyProject, "MyProject")`, targets `MyProject.Target.cs`/`MyProjectEditor.Target.cs` (`BuildSettingsVersion.V7`, `Unreal5_8`). All functionality lives in the plugin.
- **Deployment**: `deploy.py` (repo root) is THE deployment script — the one and only mechanism that creates every binary asset (master material, layer MIs, `DA_FaceParallaxPreset`, `BP_FaceParallaxCharacter`, `WBP_FaceParallaxEditor`, render target, preview actor). It runs inside the editor (`py "G:\tailedstories\paralax\deploy.py"`). There is NO C++ deploy pipeline in the subsystem — never re-add one, never port deploy.py's functionality to C++.

## Rules

1. **`EFaceAngleState` lives in `FaceParallaxTypes.h`** — include that header, never redeclare.
2. **Module API macro** is `FACEPARALLAX_API` — required on every runtime `UCLASS`/`USTRUCT`/`UENUM` the editor module references across the DLL boundary.
3. **Never break the Python syntax validator** — all `.h`/`.cpp` files must parse cleanly.
4. **Never break the C++ math tests** — component logic must match test expectations.
5. **Each plugin module needs `IMPLEMENT_MODULE`** in one of its cpp files (`FaceParallax\Private\FaceParallaxModule.cpp` for runtime, `FaceParallaxEditorSubsystem.cpp` for editor). Without it, the DLL loads but never registers — "could not be initialized successfully after it was loaded", or a "Trying to recreate changed class" fatal.
6. **Plugin dependencies must be declared in `FaceParallax.uplugin`** (`"Plugins": [...]`) as well as in `Build.cs` — missing entries produce UBT warnings and can break module loading.
7. **Keep `README.md` in sync** with API changes.
8. **Prefer editing existing files** — no new files unless necessary.
9. **Widget code uses direct member pointers** (`SliderOrbitYaw`, `PreviewImageWidget`) — no string-based dispatch.
10. **UE5.8 compiler**: `H:\unreal\UE_5.8\Engine\Build\BatchFiles\Build.bat` (run_tests.ps1 finds it via PATH).
11. **NEVER delete or remove anything** — no file, script, asset, `.uasset`, `.py`, `.md`, `.bat`, or any other artifact in this repository (root, `Tests\`, or `SAMPLES\`) may be deleted, renamed, or moved without the user's explicit written approval in the current turn. This includes any "cleanup", "removing duplicates", or "deleting stale assets". `deploy.py` and all deployment scripts are permanent — never delete them, never "port" their functionality elsewhere.
12. **`SAMPLES\` is read-only** — it contains ONLY the sample project (`SAMPLES\MyProject\`). Never save, copy, or generate any codebase file inside `SAMPLES\` (no README copies, no helper scripts, no docs). The only writer is `Tests\run_tests.ps1` syncing the 19 root sources into the plugin `Source\` dirs before a UE build. All codebase deliverables live at the root or in `Tests\`.
13. **No trash files** — never create scratch, backup, or duplicate files (e.g. `fixslots.ps1`, `deploy_legacy_backup.py`, stray README copies). If scratch already exists, remove it only with explicit user approval, then run the full suite.
14. **Tests are law** — never modify, weaken, disable, delete, or re-scope `Tests\ParallaxMathTests.cpp`, `Tests\SyntaxValidator.py`, `Tests\run_tests.ps1`, or any test to make it pass. When a test fails, fix the product code and re-run the suite.
15. **Never delete to fix** — a red test, a failing build, or a broken asset is fixed by editing code, never by deleting the thing that fails.
16. **Never claim results without running them** — "tests pass" may only be stated after `.\Tests\run_tests.ps1 -IncludeUEBuild` ran green in this session; report real numbers from real runs.

## Verify

**After EVERY change — C++, Python, docs, or any user-approved file deletion — run the full suite before finishing:**

1. `.\Tests\run_tests.ps1 -IncludeUEBuild` — syncs SAMPLES from root, runs the Python syntax validator, compiles and runs the C++ math tests (all must pass), and builds both plugin modules with UBT (must pass; checks both DLLs).
2. If the editor subsystem/widget changed: run the headless probe and check `SAMPLES\MyProject\Saved\Logs\MyProject.log` for the `[FaceParallax]` markers (`EditorSubsystem initialized - DOCKED-TAB BUILD v3 (marker 0xV3)`, `==> Deploying FaceParallax editor assets (deploy.py)`, `[VERIFY] Deployment complete - all assets present.`). Note: the `LayoutSpec self-check` log (Phase H) only fires when `RebuildWidget` runs in an interactive session (headless probes skip Slate) — its logic is covered by `TestPhaseHUIDesign` in the math tests.
3. If the editor is open (Live Coding active), the UE build will fail — close it and re-run the suite before declaring success.
