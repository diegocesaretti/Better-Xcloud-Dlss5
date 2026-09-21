# Better Xcloud DLSS5

Experimental Windows bridge that applies the **DLSS 5 neural-rendering pipeline used by DLSS5 Video Player to Xbox Cloud Gaming**, while leaving xCloud networking, audio and controller input in the browser.

> **Status: early alpha.** The first milestone is intentionally conservative: capture the xCloud browser window with Windows Graphics Capture, process only the newest frame, and present the neural result in a click-through overlay. This lets us measure real latency on hardware such as the GTX 1660 Super before investing in a deeper browser/native zero-copy path.

## Design goals

- One-click installation for non-technical Windows users.
- No compiler, Git, Python or manual DLL copying for end users.
- Keep Better xCloud responsible for xCloud/WebRTC/input features.
- Keep the DLSS5 runtime isolated from the browser.
- Never queue old cloud frames: newest frame wins.
- Insert enters/leaves OptiScaler setup mode; F7 toggles the processed overlay; F9 exits the host.
- No redistribution of the experimental neural runtime from this repository.

## How it works

```text
Xbox Cloud Gaming (WebRTC)
          |
          v
Edge / Chrome + Better xCloud
          |
          | Windows Graphics Capture
          v
XCloudDLSS5Host.exe
          |
          | temporal guides + DLAA carrier
          v
DLSS5 Video Player renderer / RenoDX Feature 18
          |
          v
click-through overlay
```

The browser remains focused during normal play, so controller, keyboard/mouse, audio, login and the xCloud session continue to work normally. Pressing **Insert** temporarily makes the render overlay interactive and foreground so OptiScaler can receive its menu hotkey and mouse input; pressing Insert again closes setup and returns focus/input to xCloud.

## Installation target

The release package will contain a single **Install.cmd**. It installs per-user under:

```text
%LOCALAPPDATA%\BetterXcloudDLSS5
```

The installer downloads the upstream DLSS5 Video Player runtime directly from its publisher, verifies its SHA-256, places this project's host inside the upstream `neural-runtime` directory, and creates a Start Menu shortcut. Administrator rights are not required.

Better xCloud itself is intentionally obtained from its official project instead of being silently repackaged.

### GTX / Turing compatibility pack

For the GTX 1660 test path, the installer can overlay a **known-good compatibility ZIP supplied by the user** without redistributing those proprietary/community runtime binaries from this repository.

Two beginner-friendly options are supported:

1. Put a single ZIP named like `GTX1660*.zip`, `compat*.zip` or `drive-download*.zip` beside `Install.cmd`, then double-click `Install.cmd`.
2. Drag the known-good compatibility ZIP onto `Install.cmd`.

The importer only stages an allowlist of local runtime files. It supports both a direct local NGX override and the known-good GTX/Turing **version.dll + Streamline** layout, including `version.dll`, `nvngx_dlss*.dll`, `sl.*.dll`, `renodx-dlss5.addon64`, `dxgi.dll`, and `ReShade.ini`. It never copies driver/system DLLs such as `nvapi64.dll` or `nvofapi64.dll`.

A full inventory and SHA-256 report is written to:

```text
%LOCALAPPDATA%\BetterXcloudDLSS5\COMPATIBILITY_PACK_INFO.txt
```

For the currently validated GTX 1660 path, the compatibility shim is loaded locally as `version.dll` before the carrier initializes. The host itself still uses raw NGX; the Streamline files are compatibility-pack companions rather than the host's rendering API.

## Hardware

The upstream DLSS5 Video Player officially documents RTX-class hardware. **GTX 1660 / 1660 Super support in this project is experimental** and depends on the same community compatibility method being tested by the project owner. We do not hard-block non-RTX Turing cards; the host attempts initialization and reports the actual runtime result.

## Upstreams

- Better xCloud: https://github.com/redphx/better-xcloud
- DLSS5 Video Player: https://github.com/2600th/dlss5-video-player
- NVIDIA DLSS SDK: https://github.com/NVIDIA/DLSS

See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) before distributing binaries.

## Roadmap

**0.1 — measurable live prototype**
- Windows Graphics Capture from Edge/Chrome.
- Latest-frame-only policy.
- 1:1 DLAA carrier for Feature 18.
- Click-through overlay and hotkeys.
- Automated Windows build.
- Beginner-friendly installer.

**0.2 — lower latency**
- GPU shared-texture path (remove GPU -> CPU -> GPU staging).
- Better resize/fullscreen handling.
- On-screen latency/FPS diagnostics.

**0.3 — Better xCloud companion**
- Optional companion userscript/native bridge for exact stream-video geometry and lifecycle.
- Automatic start/stop with xCloud sessions.

## Safety / provenance

The experimental neural runtime used by DLSS5 Video Player contains community-modified, unsigned components. This repository does **not** embed or redistribute those runtime binaries. The installer fetches a pinned upstream package and verifies it before use.

This project is not affiliated with Microsoft, Xbox, NVIDIA, Better xCloud or the DLSS5 Video Player maintainers.
