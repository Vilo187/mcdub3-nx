# 4. Build for the Switch in devkitPro's pinned container

Date: 2026-10-16

## Context

`make setup` installed devkitPro with its own installer, from devkitpro.org.
From this network devkitpro.org answers 403 to every client (a Cloudflare
block), so the VM had no Switch toolchain. devkitPro's Docker image on
Docker Hub is reachable, but its binaries need glibc 2.36 (Debian 12) and
the VM is Ubuntu 22.04 (glibc 2.35): unpacked onto the host they do not
start. Making them start meant bundling the image's C library and patching
every program with patchelf, on every install. The same build has to run
later on GitHub Actions.

## Decision

`make switch` runs `switch/build.sh` inside `devkitpro/devkita64`, pinned by
tag and multi-arch index digest in `switch/devkitpro-image`. The host only
needs Docker. The repo is mounted at the same path; the build runs as the
calling user. `NATIVE=1` keeps a host-installed devkitPro as an option.

## Why

- One toolchain, byte for byte, in the VM and in CI (amd64 and arm64 from one
  digest); upgrades are one line, reviewed like code.
- Nothing patched or installed into the host; no dependency on devkitpro.org.
- The image already has the build tools the scripts use (cmake, ninja,
  python3, rsync), so there is no Dockerfile of our own to maintain.

## Consequences

- Docker in the VM (Ubuntu's docker.io); the user joins the docker group
  (or the script falls back to sudo).
- CI can build and test everything that does not need the game: the lifted C
  comes from the disc's XBE, which never goes into the repository or a public
  runner. A game NRO in CI needs a private runner with the disc.
- Vulkan builds still need the NVK SDK on the host (`work/deps`).
