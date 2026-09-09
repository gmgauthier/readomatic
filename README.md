# Read-O-Matic

![Read-O-Matic window mock](brand/ui-reference.svg)

An **EPUB reader** for The Lunduke Computer Operating System (LCOS). The window is WinHelp 4 / OS/2 `VIEW.EXE`, not a web browser.

Binary: `readomatic`. Unlicense.

LCOS itself: [https://github.com/BryanLunduke/LCOS](https://github.com/BryanLunduke/LCOS)

## Status

**M2 in tree.** Open an EPUB: Contents fills from nav/NCX, `<<` `>>` walk the spine, Back goes to the previous topic. Index / Find still empty (M3). Samples in `data/samples/` (git-only).

Next: **M3 — Index + Find** (see [DEVELOPMENT.md](DEVELOPMENT.md)).

| Doc | What |
|---|---|
| [DEVELOPMENT.md](DEVELOPMENT.md) | Locked decisions, architecture, milestones M0–M6 |

## Build

```
sudo apt install build-essential meson ninja-build pkg-config g++ libgtkmm-3.0-dev
meson setup build
meson compile -C build
./build/readomatic
```

## License

[The Unlicense](https://unlicense.org). See [UNLICENSE](UNLICENSE).
