# Architecture

## Why the first version uses Windows Graphics Capture

Better xCloud already has excellent access to xCloud's `HTMLVideoElement`, WebRTC session and presentation pipeline. A browser userscript, however, cannot hand a Chromium decoder's GPU texture directly to an arbitrary native D3D12 process without an additional browser/native transport layer.

For the first measurable build we therefore keep the two responsibilities separate:

1. Better xCloud / the browser owns xCloud networking, decode, audio and input.
2. `XCloudDLSS5Host.exe` captures the browser window with Windows Graphics Capture.
3. The capture queue is drained every iteration; only the newest frame is processed.
4. The upstream DLSS5 Video Player temporal-guide and D3D12 renderer code receives that frame.
5. A 1:1 DLAA carrier exposes the NGX evaluation contract to the upstream RenoDX Feature 18 add-on.
6. The output is presented in a topmost click-through window over the browser.

The native process is started from the upstream `neural-runtime` directory. This is intentional: the ReShade `dxgi.dll`, RenoDX add-on and neural DLLs remain local to the host process and are **not injected into Edge/Chrome**.

## Latency policy

Cloud gaming must not build a processing backlog. WGC is created with a two-frame pool, but `TryGetLatest` drains all currently available frames and discards every frame except the newest.

A discontinuity resets temporal history with `HistoryReset::Drop`. A render failure is not retried on the stale input; the next live frame starts with clean history.

The alpha path is:

```text
Chromium GPU surface
      |
      | WGC
      v
D3D11 capture texture
      |
      | staging readback   <-- temporary alpha cost
      v
CPU BGRA
      |
      | upstream RenderFrame upload
      v
D3D12 / DLAA carrier / Feature 18
      |
      v
overlay swapchain
```

The main 0.2 optimization is to remove the middle CPU staging hop and share GPU resources directly between the D3D11 capture device and D3D12 renderer.

## Better xCloud integration plan

The native alpha does not fork Better xCloud. This keeps installation and updates simple while we answer the first hard question: can the target GPU process xCloud frames fast enough to be useful?

The planned companion layer will use Better xCloud's existing stream lifecycle and `requestVideoFrameCallback` hooks to provide:

- exact stream-video rectangle instead of relying on app-mode geometry;
- exact frame cadence / start / stop events;
- automatic native-host activation;
- diagnostics that correlate xCloud frame delivery with neural presentation.

It will not replace xCloud's audio or controller path.

## Upstream pinning

The build pins:

- DLSS5 Video Player: `dlss5-video-player-v0.24.0`
- NVIDIA/DLSS SDK: `a291cc7d2cc642a51566f3dfd5376f635cd1b284`

The installer separately pins the upstream complete runtime ZIP by SHA-256. Runtime binaries are downloaded from the upstream project rather than redistributed here.
