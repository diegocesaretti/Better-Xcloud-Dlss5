# Third-party notices

This repository contains original bridge/installer code and builds against upstream projects. It does not claim ownership of those projects or their trademarks.

## Better xCloud

https://github.com/redphx/better-xcloud

Licensed under the MIT License. Better xCloud is not bundled into the native host; users are directed to the official project for installation.

## DLSS5 Video Player

https://github.com/2600th/dlss5-video-player

Licensed under the MIT License. The build consumes selected renderer source files from the pinned upstream tag `dlss5-video-player-v0.24.0`.

The upstream project's experimental neural runtime contains community-modified, unsigned components and documents unresolved redistribution questions for the combined runtime set. **This repository therefore does not publish those runtime DLLs.** The end-user installer downloads the pinned complete package directly from the upstream release and verifies its SHA-256.

## NVIDIA DLSS SDK

https://github.com/NVIDIA/DLSS

The build fetches the official SDK at the upstream DLSS5 Video Player's pinned commit. NVIDIA components remain subject to NVIDIA's applicable license terms.

## Microsoft Windows APIs

Windows Graphics Capture, Direct3D 11, Direct3D 12, DXGI and C++/WinRT are used through the Windows SDK.

Xbox, Xbox Cloud Gaming, Microsoft, NVIDIA, DLSS and other marks belong to their respective owners. This project is independent and unaffiliated.
