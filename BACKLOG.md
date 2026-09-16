# Read-O-Matic backlog

Current release: **v0.1.2**. Last updated: 2026-09-16.

WinHelp 4 / OS/2 VIEW chrome; payload is EPUB 2/3. Binary `readomatic`. Suite catalog: `lcos-projects/PRODUCT-BACKLOG.md`. Plan: [DEVELOPMENT.md](DEVELOPMENT.md). How to land work: [DEVELOPMENT.md](DEVELOPMENT.md#process) — `feature/` / `fix/` branches, PRs to `master`, lint gate, semver on shipped PRs.

## High Priority

- **Library window.** Toolbar **Library** (far right) is a stub: tooltip and status say “coming soon”. Separate UI, not a notebook tab. Reads `[library] dir` from the ini, cover thumbnails, select a book to open it. Organize: groups, tags, add, delete.
- **Edit → Copy.** Menu item is wired to `on_not_yet`. Copy the topic selection to the clipboard.

## Low Priority

- Contents tree lines and remember expand/collapse
- MOBI / AZW / AZW3 only if a small library (`libmobi`) is enough. Do not pull Calibre
- Annotation highlighter
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

**v0.1.0 (M0–M6)** — Open EPUB 2/3 (`libarchive` + `libxml2`); Contents from nav/NCX; Index; Find; Back history; spine `<<` `>>`; bookmarks; Open Recent; last-topic restore; Appearance; print current topic; `.deb` / tarball / AppImage. Config: `~/.config/readomatic/`. Samples stay git-only.

**v0.1.1** — Contents/Find hover/sticky match Partyline (`#C5D4E8` / `#8AADC8`).
