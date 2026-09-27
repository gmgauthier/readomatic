# Read-O-Matic backlog

Current release: **v1.0.0**. Last updated: 2026-09-27.

WinHelp 4 / OS/2 VIEW chrome; payload is EPUB 2/3 and MOBI/AZW/AZW3. Binary `readomatic`. Suite catalog: `lcos-projects/PRODUCT-BACKLOG.md`. Plan: [DEVELOPMENT.md](DEVELOPMENT.md). How to land work: [DEVELOPMENT.md](DEVELOPMENT.md#process) — `feature/` / `fix/` branches, PRs to `master`, lint gate, semver on shipped PRs.

## High Priority

- Annotation highlighter

## Low Priority

- Contents tree lines and remember expand/collapse
- Dictionary lookup
- TTS
- Two books at once

## Out of Scope

- `.hlp` / `.chm` as a product. Those are the *look*, not the file type
- WebKit / a location bar / browser tabs. `Gtk::TextView` stays the renderer unless a real EPUB is unreadable
- Calibre, a store, sync, “get books”
- Network in the default build
- Custom title bar; Bryan’s seal
- Foliate / Okular re-theme

## Shipped

**v1.0.0** — MOBI / AZW / AZW3 via libmobi. WinHelp object is complete; highlighter stays High.

**v0.4.0** — Edit → Copy (Ctrl+C). Library organize: groups, tags, add, delete.

**v0.1.0 (M0–M6)** — Open EPUB 2/3 (`libarchive` + `libxml2`); Contents from nav/NCX; Index; Find; Back history; spine `<<` `>>`; bookmarks; Open Recent; last-topic restore; Appearance; print current topic; `.deb` / tarball / AppImage. Config: `~/.config/readomatic/`. Samples stay git-only.

**v0.1.1** — Contents/Find hover/sticky match Partyline (`#C5D4E8` / `#8AADC8`).

**v0.2.0** — Thunar Open with for EPUB; MOBI MIME listed.

**v0.3.0** — Library window: dual-pane Gio copy to USB/MTP, last-page-read sidecar, Open ticked book, persist library folder and last device directory.
