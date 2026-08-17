# Third-Party Notices

MinecraftHeadTracking itself is MIT licensed (see `LICENSE`). The components
below are third-party and carry their own licenses.

## MinHook

- **Version:** 1.3.3 (commit `9fbd087432700d73fc571118d6a9697a36443d88`)
- **License:** BSD-2-Clause
- **Upstream:** https://github.com/TsudaKageyu/minhook
- **Usage:** Function hooking for the render-phase camera hook and discovery
  mode.
- **Bundled:** yes. Fetched at build time and statically linked into
  `MinecraftHeadTracking.dll`, which ships in the release ZIP.

Copyright (c) 2009-2017 Tsuda Kageyu. All rights reserved.

---

## CameraUnlock Core

- **Version:** commit `904f2f4`
- **License:** MIT
- **Upstream:** https://github.com/itsloopyo/cameraunlock-core
- **Usage:** Shared tracking pipeline, smoothing and hook utilities.
- **Bundled:** yes. Included as a git submodule and statically linked into
  `MinecraftHeadTracking.dll`.

Copyright (c) 2026 CameraUnlock

---

## OpenTrack

- **Version:** protocol only, no pinned version
- **License:** ISC
- **Upstream:** https://github.com/opentrack/opentrack
- **Usage:** The mod reads OpenTrack's UDP packet format. No OpenTrack code is
  included, copied, or redistributed.
- **Bundled:** no.

---

## No bundled loader

This mod vendors no third-party mod loader. Minecraft: Bedrock Edition cannot
host one, because its install directory is not writable, so the launcher in
this repo loads the mod instead. Nothing is downloaded at install time.

Minecraft is a trademark of Mojang Synergies AB. No Minecraft code, assets, or
binaries are included in or redistributed by this repository.
