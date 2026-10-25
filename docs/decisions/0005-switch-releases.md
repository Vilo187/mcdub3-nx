# 5. Switch releases: built on a self-hosted runner, released after approval

Date: 2026-10-21

## Context

Official builds should be versioned and published as GitHub releases, with
release notes, in two stages like an Azure DevOps pipeline: tests and build
first, then a separate release that waits for approval. Only the Switch is
released; Linux and macOS players build their own (`make build`,
`make mac-app`).

Every build of the game needs the lifted C, and the lifted C comes from the
disc's `default.xbe`, which never goes into the repository or onto GitHub's
runners (ADR 0004). The lifted code also fits one XBE byte for byte: other
editions of the game (PAL languages, NTSC-J) need their own lift and their
own NRO, as nfsmw-nx does (one `nfsmw-nx-<edition>.nro` per edition).

## Decision

- **Version**: `APP_VERSION` in `switch/app.env` (since ADR 0008
  `config/dub/app.env`), the one place (NACP, log
  banner, artifact, tag). Release notes: the `## v<version>` section of
  `RELEASE_NOTES.md` at the repo root.
- **Tests** (`.github/workflows/ci.yml`, GitHub-hosted): toolkit lifter and
  disassembler tests, script syntax, version format. On every push and pull
  request, and as the first stage of a build.
- **Build** (`switch-build.yml`, started by hand): after the tests, a job per
  edition on a self-hosted runner with the discs (label `mcdub3-disc`):
  `make lift` (the committed `config/xbe_facts.h` must come out unchanged),
  `make switch`, and an artifact `mcdub3-nx-<version>-<edition>` with
  `mcdub3-nx-<edition>.nro`, its ELF and a `.buildinfo` (version, edition,
  build ID, commit, SHA-256).
- **Release** (`switch-release.yml`, started by hand with a build's run ID):
  its first job runs in the `release` environment, whose required reviewers
  approve it, and checks the build succeeded, the tag is new, the notes
  exist and the NROs match their build info. A job on a self-hosted runner
  with the console keys (label `mcdub3-keys`) then builds an NSP forwarder
  per edition (`make nsp`). The last job tags `v<version>` on the build's
  commit and publishes NROs and NSPs with the notes, together or not at all.
- **Keys** (decided 2026-10-21; superseded 2026-10-25 by ADR 0008: NSPs are
  built locally only, no job reads keys): NSPs need the console's `prod.keys`. They
  stay on the owner's runner and are read from its disk (`MC3_KEYS`); never
  a GitHub secret, artifact or hosted runner. The NSPs contain no keys
  (hacBrewPack only uses them to encrypt the package).
- **Editions**: `tools/first_lift.sh` records the XBE's size, FNV-1a 64,
  region and edition (`usa`, `jpn`, `pal` from the certificate, or
  `MC3_EDITION`) in `config/xbe_facts.h`; the NRO refuses any other XBE at
  boot with a message. One edition today; another one adds a disc folder on
  the runner, a matrix entry, and its own lift configuration.

## Why

- The disc and the lifted C stay on one machine the owner controls; GitHub
  only ever sees the finished NRO.
- Approval and release notes are reviewed like code: the notes are in the
  commit that was built, the gate is GitHub's own environment protection.
- Only `workflow_dispatch` reaches the self-hosted runner: on a public
  repository a pull request must never run code there.

## Consequences

- Releasing needs the runners online: the disc runner (the Linux VM, with
  Docker) to build, the keys runner (the VM or the Mac, with Docker) for
  the NSPs.
- Per-edition configuration (facts, seeds) is one set today; the second
  edition moves it into `config/<edition>/`.
- The released NRO contains the game's recompiled code (no data); that is the
  owner's call, as for the fan ports this project follows.
- 11 toolkit tests fail since vendoring (stale C preludes); CI skips them by
  name (`ci/known-test-failures.txt`) until they are fixed.
