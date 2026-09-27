# Read-O-Matic

**Vended by Grok Build**

![Read-O-Matic on LCOS](brand/screenshot-read.png)

An **EPUB / MOBI reader** for The Lunduke Computer Operating System (LCOS). The window is WinHelp 4 / OS/2 `VIEW.EXE`, not a web browser.

Binary: `readomatic`. Unlicense.

LCOS itself: [https://github.com/BryanLunduke/LCOS](https://github.com/BryanLunduke/LCOS)

![Find](brand/screenshot-find.png)

![About](brand/screenshot-about.png)

![Library transfer](brand/screenshot-library-transfer.png)

![Library open](brand/screenshot-library-open.png)

## Status

**v1.0.0.** EPUB 2/3 and MOBI/AZW/AZW3 (libmobi). Library, Copy, Contents/Index/Find. This tree also highlights a selection in yellow, light green, light blue, or pink (next tagged release). `.deb` / tarball / AppImage via `./scripts/release.sh`. See [INSTALL.md](INSTALL.md). Samples in `data/samples/` (git-only).

| Doc | What |
|---|---|
| [DEVELOPMENT.md](DEVELOPMENT.md) | Locked decisions, architecture, milestones M0–M6, branching, semver, lint |

Thunar Open with: EPUB, MOBI, AZW, AZW3. `Exec=readomatic %F`. Single-instance via flock + Unix socket (no D-Bus).

## Build

```
sudo apt install build-essential meson ninja-build pkg-config g++ libgtkmm-3.0-dev libarchive-dev libxml2-dev libfontconfig1-dev libmobi-dev clang-format cppcheck
meson setup build
meson compile -C build
./build/readomatic
```

PR lint gate: `./scripts/lint.sh` (CI runs this; no `--fix`). Format `src/` locally with `./scripts/lint.sh --fix`.

## Install

Preferred: `./scripts/release.sh deb` then `sudo apt install ./dist/readomatic_1.0.0-1_amd64.deb`. Details in [INSTALL.md](INSTALL.md).

## License

[The Unlicense](https://unlicense.org). See [UNLICENSE](UNLICENSE).
