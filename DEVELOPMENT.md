# Read-O-Matic development plan

A gtkmm-3 **EPUB** reader for LCOS. The *window* is WinHelp 4 / WinHlp32 (and OS/2 `VIEW.EXE`); the *payload* is EPUB. Not a `.hlp` engine.

Reference window: `brand/ui-reference.svg`  
License: The Unlicense (`UNLICENSE`)  
Binary: `readomatic`  
Repos: https://gitea.scriptorium/gmgauthier/readomatic (origin), https://github.com/gmgauthier/readomatic

## Status (2026-09-09)

**M4 is in the tree.** Bookmarks (Define + menu), File → Open Recent, last topic/window restored from `~/.config/readomatic/readomatic.ini`. Toolbar **Library** is a stub (tooltip “coming soon”).

Next: **M5 — Print + font + keys.**

## 1. Locked decisions

| Decision | Choice |
|---|---|
| Product | Original app. Chrome is WinHelp / OS/2 VIEW. Format is EPUB, not `.hlp` / `.chm` |
| Name | Read-O-Matic. Binary `readomatic` |
| Toolkit | C++17, gtkmm-3.0, GTK3 CSS, Meson |
| Look | One decorated window. Left notebook (Contents, Index, Find). Right: topic. No URL bar, no browser tabs, no shelf of books |
| v1 format | EPUB 2 and 3 (zip + OPF + XHTML + nav or NCX). One file, one window |
| Later format | MOBI / AZW / AZW3 only if a small library (`libmobi`) is enough. Do not pull Calibre |
| Never | `.hlp`, `.chm` as a product goal. Those are the *look*, not the file type |
| Topic renderer | **`Gtk::TextView` for v1.** Map a small XHTML subset into text tags + pixbufs. Prefer the poorer renderer if it keeps the Help-file feeling. WebKitGTK is a one-time escape hatch **only if M1 proves a real EPUB unreadable** — not a browser chrome |
| EPUB parse | `libarchive` for the zip; `libxml2` for OPF / nav / NCX / XHTML. No Calibre, no Java |
| Open | File → **Open…** replaces the current book (WinHelp: one help file). Close clears. No New vs Add |
| Jumps | Underlined navy (or theme-selected) links. Click pushes Back stack and opens the target href in the topic pane |
| Back | History stack of (spine href, scroll). Not browser-style tabs |
| `<<` `>>` | Walk the **spine** (browse sequence), not the Back stack |
| Bookmarks | Local list per book (OPF identifier or file path), like WinHelp annotations. Not a cloud library |
| Network | None in the default build. No store, no sync, no “get books” |
| Init / session | No systemd. Config in `~/.config/readomatic/` |
| License | The Unlicense |
| Versioning | `meson.build` is the source of truth. Debian changelog tracks the same upstream version |

Why not Foliate / Calibre viewer / Okular / a browser: Adwaita, phone-book chrome, or an actual web browser. This product is the opposite.

## 2. Window

```
+------------------------------------------------------------------+
| File  Edit  Bookmark  Options  Help                              |
+------------------------------------------------------------------+
| [Contents] [Index] [Find]   [Back] [<<] [>>] [Print]             |
+------------------------+-----------------------------------------+
| Contents               |  topic pane                             |
|  Book title            |                                         |
|   Chapter              |  body, jumps underlined                 |
|    Topic               |  images inline                          |
+------------------------+-----------------------------------------+
| Chapter 3 of 12 — 42%                                            |
+------------------------------------------------------------------+
```

- **Contents** — EPUB `nav` / NCX tree (`Gtk::TreeStore`). Double-click or activate → that href
- **Index** — flattened list: spine titles + `h1`–`h3` from each document (`Gtk::ListStore`)
- **Find** — entry + hit list in this book only. Activate a hit → that href + select the match
- Toolbar Contents / Index / Find **switch the notebook page** (already in the stub). They are modes, not browser tabs
- Topic pane: `Gtk::TextView`, not editable, wrap on word. No location bar
- Status: `Title — n of m` while a book is open; `No book open.` otherwise
- Client `#E6E6E1`. Topic and nav lists `#FFFFFF`. Decorations stay Clearlooks / Phenix

File menu (WinHelp, not EarBlaster New/Add):

```
Open…
Close
────────
Print…
────────
Exit
```

Edit: Copy (selection in the topic). Bookmark: Define… / list of this book’s marks. Options: Font… (size). Help: About.

## 3. Architecture

```
readomatic
├── brand/                      icon-tile.svg, ui-reference.svg
├── data/
│   ├── readomatic.desktop
│   └── skin/lcos/lcos.css
├── src/
│   ├── main.cpp
│   ├── application.{hpp,cpp}    Gtk::Application + flock + uniqueness
│   ├── paths.{hpp,cpp}          SOURCE_ROOT / DATADIR / READOMATIC_DATA
│   ├── main_window.{hpp,cpp}
│   ├── about_dialog.{hpp,cpp}
│   ├── book.{hpp,cpp}           zip + OPF + spine
│   ├── topic_view.{hpp,cpp}     XHTML → TextView tags
│   ├── history.hpp              M2: Back stack
│   └── settings.{hpp,cpp}       M4: ini, recent, bookmarks
├── meson.build                 version 0.1.0
├── README.md
└── DEVELOPMENT.md
```

Uninstalled binary finds CSS via `SOURCE_ROOT`. Installed binary uses `DATADIR`. `READOMATIC_DATA` overrides both.

### `Book`

One open EPUB.

```cpp
bool open(const std::string& path);   // false → error string
void close();
const std::string& title() const;
int spine_count() const;
int spine_index() const;              // current item
std::string spine_href(int i) const;
std::string load_document(const std::string& href); // uncompressed XHTML
// nav: walk into a TreeStore
// search(query) → list of {href, excerpt}
```

Temp extract dir under `$XDG_CACHE_HOME/readomatic/books/<hash>/` or stream via libarchive without a full unpack if that stays simple. Prefer **extract once on open**, delete on close / process exit. Images in the topic pane load from that tree.

OPF: package identity, manifest, spine, optional `nav` (EPUB3) or NCX (EPUB2). Resolve hrefs against the OPF directory inside the zip.

### `TopicView`

Wraps the existing `Gtk::TextView`.

```cpp
void load_xhtml(const std::string& xhtml, const std::string& base_dir);
sigc::signal<void, Glib::ustring>& signal_jump(); // href
```

Map a **closed subset**: `p`, `div`, `br`, `h1`–`h6`, `em`/`i`, `strong`/`b`, `a href`, `img src`, `ul`/`ol`/`li`, `blockquote`, `pre`/`code`. Drop `script`, `style`, `svg`, `iframe`. Unknown tags: keep children.

- Headings: larger/bold tags
- `a`: underline + navy; button-release emits `signal_jump` with resolved href
- `img`: `Gdk::Pixbuf` inserted if the file exists under `base_dir`; otherwise skip
- Fragments (`href#id`): scroll to a mark named `id` if present

If a real book is soup after this mapping, **stop M1 and revisit WebKitGTK** (no chrome, our CSS, `decide-policy` to trap links). Do not sneak a location bar in.

### `History`

Vector of `{href, scroll}`. `push` on jump and on Contents activate. `back()` pops. `<<` `>>` do **not** use this; they change `Book::spine_index`.

### `Settings`

`~/.config/readomatic/readomatic.ini` (Glib::KeyFile): window geometry, restore-window, last book path, last href, font size, recent files (cap 8).

Bookmarks: `~/.config/readomatic/bookmarks.ini` sections keyed by OPF identifier (fallback: canonical path).

## 4. Skin (`data/skin/lcos/lcos.css`)

Stock GTK3 widgets. Clearlooks / Phenix keeps decorations.

- Window client `#E6E6E1`
- Topic / nav `#FFFFFF`, text `#000000`
- Selected nav row: steel blue, not Adwaita orange
- Statusbar `#D4D0C8`
- No custom title bar

Dev machine: `GTK_THEME=Clearlooks-Phenix ./build/readomatic` if the engine is installed. Full title-bar fidelity is XFCE + that theme (LCOS VM).

## 5. Milestones

**v1.0 is M0 through M6.** Same shape as EarBlaster: window, payload, navigation, find, remember, polish, package.

### M0 — Window — **stub 2026-09-09**

Meson + gtkmm-3. `MainWindow`: menus, toolbar, paned notebook, empty trees, placeholder `TextView`, About, Open chooser (does not parse). Single-instance lock. `.desktop` + icon tile.

Done when: window matches the ASCII mock; Contents / Index / Find switch pages; Open… reports a path; second process focuses the first.

### M1 — Open EPUB — **in tree 2026-09-09**

`Book` + `TopicView`. File → Open… extracts, reads OPF, loads the first readable spine XHTML into the topic pane (skips SVG cover wrappers). Close clears. Errors (not a zip, no OPF) go to the status bar, not a crash. Internal `http(s)` links are ignored; in-book hrefs load if they resolve.

Done when: Kafka and *Astounding* samples show readable text. If TextView mapping is hopeless, stop and decide WebKit **before** M2.

### M2 — Contents + browse — **in tree 2026-09-09**

Fill Contents from EPUB3 `nav` or NCX. Activate a row → load that href, push Back. `<<` `>>` walk the spine. Back walks history (and restores scroll). Status `Title — n of m`. Highlight the current Contents row when possible.

Done when: you can open a book, jump via Contents, step `>>` through chapters, and Back returns to the previous topic.

### M3 — Index + Find — **in tree 2026-09-09**

Index: spine titles plus headings. Find: case-insensitive search over uncompressed spine documents; hit list with short excerpt; activate jumps and selects.

Done when: Find “whale” in a Gutenberg book lists hits and a click lands in the topic.

### M4 — Bookmarks + last topic — **in tree 2026-09-10**

Bookmark → Define stores href + label for this book. Bookmark menu lists them. On open, restore last href (and window) from ini if that file still exists. File → Open Recent.

Toolbar **Library** (far right): stub. Tooltip and status “coming soon”. The real Library is a later window (see Later).

Done when: quit mid-chapter, reopen the same EPUB, land on that topic.

### M5 — Print + font + keys

Print current topic (`Gtk::PrintOperation`). Options → Font… (topic size, persist). Keyboard: BackSpace = Back, `Alt+Left`/`Alt+Right` or `[`/`]` = `<<`/`>>`, Ctrl+F focuses Find, Ctrl+O Open, Escape clears Find.

Done when: a topic prints, font size sticks, keys work with the topic focused.

### M6 — Package

Same pipeline as EarBlaster: in-tree `debian/`, `scripts/release.sh` → `.deb`, `meson dist` tarball, AppImage fallback (gtkmm only; no GStreamer). `/usr/bin/readomatic`, hicolor icon, skin + brand under `/usr/share/readomatic/`.

Done when: `dpkg-buildpackage` produces an installable `.deb` on Trixie.

### Later (not v1)

- **Library window** — separate UI, not a notebook tab. Reads `[library] dir` from the ini (a default books folder), shows cover thumbnails. Select a book → open it in the reader. Organize: groups, tags, add, delete. M4 only ships the toolbar button.
- Contents tree lines and remember expand/collapse
- MOBI/AZW, annotation highlighter, dictionary lookup, TTS, two books at once

## 6. Tooling (Devuan Excalibur / Debian Trixie)

M0:

```
sudo apt install build-essential meson ninja-build pkg-config g++ libgtkmm-3.0-dev
```

From M1:

```
sudo apt install libarchive-dev libxml2-dev
```

Build:

```
meson setup build
meson compile -C build
./build/readomatic
```

`.clangd` points at `build/compile_commands.json`.

## 7. Packaging notes for LCOS

- Match EarBlaster / `lcos-updates`: Meson + small `debian/`. Unlicense
- Binary never runs as root
- Depends: GTK3, libarchive, libxml2 (plus WebKit **only** if M1 switched)
- Recommends nothing that pulls a browser
- Prefer `.deb`. AppImage is fallback (no codec bundle problem here)
- Public clone: GitHub. Origin: Gitea

## 8. Test matrix (v1)

- EPUB 2 (NCX) and EPUB 3 (`nav`) from Gutenberg or Standard Ebooks
- Book with images; book with only XHTML
- Broken zip / missing OPF → status error, window stays up
- Contents jump, `>>` to last spine item, Back, Close, Open another book (replaces)
- Find with 0 hits and with many hits
- Bookmark, quit, reopen same file
- Second `readomatic` process focuses the first
- XFCE + Clearlooks: decorations vs CSS
- Print a short topic

## 9. First code to write

M0 is in the tree.

Do not add WebKit yet. Next is M1: `Book::open` (libarchive + libxml2 OPF), `TopicView::load_xhtml` for the subset above, wire File → Open… / Close.

Fixtures (git-only, excluded from `meson dist`): `data/samples/`

- Fiction: `kafka-metamorphosis.epub`
- Tech: `williams-free_as_in_freedom.epub`
- Periodical: `astounding-1933-03-jack-williamson.epub`
