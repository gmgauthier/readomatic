# Read-O-Matic backlog

Current release: **v1.2.8**. Last updated: 2026-10-02.

WinHelp 4 / OS/2 VIEW chrome; payload is EPUB 2/3 and MOBI/AZW/AZW3. Binary `readomatic`. Suite catalog: `lcos-projects/PRODUCT-BACKLOG.md`. Plan: [DEVELOPMENT.md](DEVELOPMENT.md). How to land work: [DEVELOPMENT.md](DEVELOPMENT.md#process) — `feature/` / `fix/` branches, PRs to `master`, lint gate, semver on shipped PRs.

## High Priority

None.

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

**v1.2.8** — N of M counts chapters Next can reach. A leading wrap0000 is not a page.

**v1.2.7** — Spine names are skipped only for cover, titlepage, and wrap plus digits. A chapter that mentions coverpage stays.

**v1.2.6** — A long title keeps its page numbers. The count is still the spine length.

**v1.2.5** — A failed open keeps its reason. A later successful open clears that reason.

**v1.2.4** — A content link resolves from its chapter and is percent-decoded once. Spine and manifest hrefs stay relative to the OPF.

**v1.2.3** — A topic image stays inside the extracted book. A leading slash is rooted there.

**v1.2.2** — A document href stays inside the extracted book. A symlink to a file outside it is not opened.

**v1.2.1** — Headless meson test suite, and known defects recorded in BUG-BACKLOG.md.

**v1.2.0** — Bookmark → Bookmarks… (Jump / Delete) and restore scroll.

**v1.1.0** — Topic highlighter: yellow, light green, light blue, pink.

**v1.0.0** — MOBI / AZW / AZW3 via libmobi. WinHelp object is complete.

**v0.4.0** — Edit → Copy (Ctrl+C). Library organize: groups, tags, add, delete.

**v0.1.0 (M0–M6)** — Open EPUB 2/3 (`libarchive` + `libxml2`); Contents from nav/NCX; Index; Find; Back history; spine `<<` `>>`; bookmarks; Open Recent; last-topic restore; Appearance; print current topic; `.deb` / tarball / AppImage. Config: `~/.config/readomatic/`. Samples stay git-only.

**v0.1.1** — Contents/Find hover/sticky match Partyline (`#C5D4E8` / `#8AADC8`).

**v0.2.0** — Thunar Open with for EPUB; MOBI MIME listed.

**v0.3.0** — Library window: dual-pane Gio copy to USB/MTP, last-page-read sidecar, Open ticked book, persist library folder and last device directory.
