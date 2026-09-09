# Read-O-Matic development plan

A gtkmm-3 **EPUB** reader for LCOS. WinHelp 4 / WinHlp32 chrome. Not a `.hlp` engine.

License: The Unlicense (`UNLICENSE`)  
Binary: `readomatic`

## Status (2026-09-09)

**M0 stub** compiles. Empty Contents / Index / Find, placeholder topic pane, Open… chooser.

## 1. Locked decisions

| Decision | Choice |
|---|---|
| Product | Original app. Window is WinHelp / OS/2 VIEW, payload is EPUB |
| Name | Read-O-Matic. Binary `readomatic` |
| Toolkit | C++17, gtkmm-3.0, GTK3 CSS, Meson |
| Look | One decorated window. Left notebook: Contents, Index, Find. Right: topic |
| v1 format | EPUB 2/3 (zip + OPF + XHTML + nav). MOBI later. Never `.hlp` / `.chm` as a goal |
| Topic renderer | **`Gtk::TextView` for the stub.** Prefer the poorer renderer if it keeps the Help-file feeling. WebKitGTK only if XHTML forces it |
| One book, one window | No library shelf (that would be Calibre) |
| License | The Unlicense |

## 2. Window

```
File  Edit  Bookmark  Options  Help
[Contents] [Index] [Find]   [Back] [<<] [>>] [Print]
+------------------------+-----------------------------------------+
| Contents / Index / Find|  topic pane                             |
+------------------------+-----------------------------------------+
| Chapter 3 of 12 — 42%                                            |
```

- Contents: EPUB nav/NCX tree
- Index: spine + headings flattened
- Find: search in the open book
- Back: history stack. `<<` `>>`: spine browse sequence
- Jumps: underlined, like WinHelp. No URL bar, no browser tabs

## 3. Suggested v1

Open EPUB, populate Contents from nav, render spine documents, Back, `<<` `>>`, Find, print current topic, remember last topic in `~/.config/readomatic/` or a sidecar. Bookmarks local per file. No store, no sync.

## 4. Milestones

### M0 — Window — **stub 2026-09-09**

Menus, toolbar, paned notebook, empty topic `TextView`, About, Open chooser.

### M1 — Open EPUB

Unzip, parse OPF, list spine, show first document in the topic pane (strip/map XHTML into TextView or decide WebKit here).

### M2 — Contents + browse

Nav tree, `<<` `>>`, Back stack, status “Chapter n of m”.

### M3 — Find + Index + bookmarks + last topic

### M4 — Print + prefs + package
