# Keil & IDE Integration

> I **don't use traditional Keil 5 (classic µVision / MDK)**, and this repo **does not provide or follow up on** its projects and scripts. I've tried **Keil Studio (i.e. Keil 6) with this component (mini-tree) and it works**, and its debugging is no different from Keil 5's — just more modern (VS Code core + native CMake, not the same thing as µVision). **Traditional Keil 5 is simply not a path in this repo; if you must, you can only generate the project yourself via the §4 degradation path.** If you want Keil, just go with Keil 6 — see §1.
>
> The main path for this repo is **CMake + clangd + modern IDEs** (Cursor / VS Code / CLion / Qoder, etc.).

| Item | Content |
| :--- | :--- |
| **Audience** | IDE decision-makers; teams forced to deliver with Keil by customers |
| **Related** | [design_decisions.md](design_decisions.md) (toolchain preference) · [getting_started.md](getting_started.md) · [faq.md](faq.md) |

---

## Contents

1. [What I Use](#1-what-i-use)
2. [Why Classic µVision / Traditional Keil 5 Is Not Supported](#2-why-classic-µvision--traditional-keil-5-is-not-supported)
3. [Recommended Daily Workflow](#3-recommended-daily-workflow)
4. [Degradation Path (If You Must Ship a µVision Project)](#4-degradation-path-if-you-must-ship-a-µvision-project)

---

## 1. What I Use

| Tool | Stance |
| :--- | :---: |
| **Cursor** / **VS Code** + clangd | **Recommended** |
| **CLion** | **Recommended** |
| **Zed** | **Recommended (coding only)**: great clangd-based editing; not for debug/flash |
| **Qoder** / modern AI IDEs | **Recommended** |
| **Keil Studio** (VS Code-based) | **Recommended (verified)**: VS Code core + first-class CMake, verified to work with this component (see §2.1); recommended to use Keil Studio with mini_tree |
| **EIDE plugin (VS Code)** | **If you must use classic µVision, prefer this path** — advisory only |
| CMake + Ninja/Make + GCC/Clang | **Recommended** (for building) |
| **Traditional Keil 5**: Keil µVision (MDK) + ARMCLANG (AC6) | **Not provided, not followed up** — no project/scripts in this repo; **not a path in this repo; if needed, generate it yourself via the §4 degradation path** |
| ARMCC v5 | don't use |

See the "Toolchain / IDE" section of [design_decisions.md](design_decisions.md) for the full trade-off.

> **Keil Studio (recommended, I've tried it)**: ARM's next-gen VS Code-based environment. Not like classic µVision — native CMake builds (CMSIS-Toolbox / cbuild), a clangd extension for editing consistent with this repo's `compile_flags.txt` / `ide/stubs`, plus ARM official debug (DAP-Link / ULINK) and cloud build. **The Keil Studio Pack plugin consumes this repo's CMake flow directly and works with this component**; if you want Keil, use it with mini_tree.
>
> **If you really must use classic µVision**: go with the **EIDE plugin (VS Code)**. If you adopt µVision with this component, **write your own py porting script** to generate the project (see §4.1). **Raw µVision for coding, flashing, or debugging is not followed up in this repo** — because I've already tried Keil Studio (Keil 6) with this component and it works, and its debugging is no different from Keil 5's, just more modern (VS Code core + native CMake); so just pick Keil 6 and use the CMake + clangd + modern-IDE flow, it's much nicer.

---

## 2. Why Classic µVision / Traditional Keil 5 Is Not Supported

> The pain points below apply **only to classic Keil µVision (MDK)**; **Keil Studio is a different story**, see §2.1.

For a repo structured as "CMake-generated headers + multi-directory middleware + optional C++/AI collaboration", Keil µVision has typical pain points:

| Problem | Details |
| :--- | :--- |
| **Almost impossible to integrate cleanly** | No first-class CMake; the `BOARD_DTS` / genconfig / dtc-lite / `ide/stubs` workflow has to be moved in by hand or via side scripts |
| **Weak navigation & indexing** | Weaker than clangd / CLion; include/file lists drift easily without automation |
| **Poor C++ support** | Weak experience with `SYSTEM_CPP`, ETL, and modern dialects |
| **Poor AI integration** | Incompatible with the multi-file understanding workflow of Cursor / Qoder |
| **Hard-to-collaborate project files** | `.uvprojx` conflicts easily and diffs poorly |

So: **the repo doesn't ship or maintain an official `.uvprojx`, and the legacy "generate Keil project" script is no longer kept as a feature.**

A legacy approach once used "a Python script scanning sources to generate a µVision project"; it is historical reference only. The current stance is to move to modern tooling.

### 2.1 Keil Studio Fit

Architecturally **Keil Studio is fundamentally different from classic µVision** (VS Code core + first-class CMake), and **verified to work with this component**:

| Aspect | Classic µVision (MDK) | Keil Studio |
| :--- | :--- | :--- |
| Core | legacy proprietary IDE | **VS Code** (incl. cloud variant) |
| Project format | `.uvprojx` (private, diff-hostile) | **CMake / CMSIS-Toolbox (`cbuild`)** / standard formats |
| CMake support | no first-class support | **first-class**: builds this repo's CMake flow directly |
| Indexing | weak | **clangd extension**: consistent with `compile_flags.txt` / `ide/stubs` |
| Debug | official debug | **keeps official debug** (DAP-Link / ULINK) + cloud build |
| My take | **not recommended, I don't maintain it** | **recommended (tried it, works with mini_tree)** |

Fit notes:

1. **Build**: Keil Studio's CMake support consumes this repo's `add_subdirectory(mini_tree)` flow directly; generated headers (`genconfig` / `dtc-lite`) are produced during CMake configure.
2. **Editing**: with the clangd extension, navigation/completion/diagnostics match this repo's `compile_flags.txt` / `ide/stubs`; coding feels like VS Code.
3. **Debug**: ARM official debugger with breakpoints/registers/memory views is retained; recommended as the **debug environment** (the logging/monitoring flow in `debug_monitor.md` is unchanged).
4. **Cloud build**: use Keil Studio Cloud when no local toolchain is available; artifacts still align with `.config` (`CONFIG_OSAL_*` / `CONFIG_SYSTEM_*`, etc.).

> Note: Keil Studio is part of the **general VS Code ecosystem**, so the existing VS Code advice in §3 applies to it too; the extra benefit is the integration of ARM official debug/flash and cloud build.
>
> **Integration status**: works in practice — `add_subdirectory(mini_tree)` + the Keil Studio Pack plugin run the CMake flow, generated headers produced during configure; **if you want Keil, use it with mini_tree**. I don't use traditional Keil 5 (classic µVision / MDK), just go with Keil Studio.

---

## 3. Recommended Daily Workflow

```text
Cursor / VS Code / CLion / Qoder
  → open the mini_tree repo root
  → clangd reads compile_flags.txt (or the compile_commands.json exported by CMake)
  → the platform project CMake handles real-hardware linking and flashing
  → (optional) any debugger front-end; no need to lock into Keil
```

See [getting_started.md](getting_started.md) §7 and [debug_monitor.md](debug_monitor.md) for IDE acceptance.

---

## 4. Degradation Path (If You Must Ship a µVision Project)

> The steps below target **classic Keil µVision (MDK)**; if you use them it's on you. **This repo does not accept any traditional Keil 5 project-related issues / PRs, and not even branches** (unless they incidentally fix a tool-unrelated middleware bug). **Keil Studio is not covered here** (see §2.1 — tried it, works, just use it).

### 4.1 Simplest Approach: Generate `.uvprojx` with Python

The least-effort degradation path (per the legacy docs/toolchain) is:

1. Still run **CMake** (or invoke standalone) through `genconfig` + `dtc-lite` to produce `config.h`, `board_*`, etc.
2. Write (or copy-and-modify from the legacy one) a **Python script** that scans middleware + platform sources, `IncludePath`, preprocessor macros (aligned with `.config`), and linker-script intent (including `ERR_SECTION_BASE`), then **generates / refreshes** `.uvprojx` (plus `.uvoptx` if needed).
3. Compile with **ARMCLANG (AC6)**; **do not use ARMCC v5**.
4. **Re-run the generator** after source-tree or Kconfig changes; do not hand-edit `.uvprojx` long-term.

Key points:

| Item | Details |
| :--- | :--- |
| Artifact directory | Must be in Include; match the CMake output paths |
| Macros | Match `.config` (`CONFIG_OSAL_*` / `CONFIG_SYSTEM_*` / `CONFIG_SYS_LOG_*` etc.) |
| File list | Generated by the script from directory rules; avoid clicking hundreds of files by hand |
| Repo status | **No generator shipped**; if the legacy script can still be found, treat it only as a template — I don't maintain it |

> **Compatibility tools for Keil 5 are welcome**: as long as they don't pollute the repo's public headers or the main build path, a separate repo or a `tools/` submodule is fine; if it's good I can list it in [docs/ecosystem.md](ecosystem.md). But **the repo itself does not accept traditional Keil 5 project PRs / issues / branches** (see above and [CONTRIBUTING.md](../../CONTRIBUTING.md)).

Hand-adding files one by one into µVision is **worse**; not recommended.

### 4.2 Recommended Workflow Split

Even if the deliverable is a Keil project:

- **Coding / Review / AI**: keep using Cursor / VS Code / CLion / Qoder (Keil Studio works too, as it is the VS Code ecosystem).
- **µVision**: **not recommended for debugging or writing code** (prefer Keil Studio / a modern IDE even for the debug adapter); build, flashing and daily development stay on the CMake flow — moving the whole workflow to the modern toolchain is just nicer.

Do not make µVision your only editor.

---

## Related Document

- [design_decisions.md](design_decisions.md) · [getting_started.md](getting_started.md) · [CONTRIBUTING.md](../CONTRIBUTING.md)
