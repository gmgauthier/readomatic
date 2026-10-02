# Bug backlog

Reviewed 2026-10-01 against the 1.2.0 sources.

`meson test` runs `tests/test_place.cpp` (`place`) and `tests/test_confine.cpp` (`confine`). `place` checks history dedup, canonical last-read, and sidecar save/load when an href is present. It does not treat a `pbr`-only sidecar as absent, and it does not treat two empty positions as "the same last page." `confine` checks that a relative href which leaves the cache is not opened, that a symlink to an outside file is not opened, that an absolute href stays under the cache, and that a chapter and an in-book parent path still load. `image` checks that a topic image stays under the extracted book, that a leading slash and a symlink to an outside file do not leave it, and that an image written beside the chapter still resolves. `links` checks that a content link in a subdirectory resolves beside that chapter, that an OPF-relative spine href still resolves, and that a percent-encoded manifest href opens the decoded zip entry. An encoded `..` does not leave the book. `%2520` is decoded once.

Zip entry names containing `..` are rejected on extract (`ARCHIVE_EXTRACT_SECURE_NODOTDOT`). The holes below are the path used after extract.

## Open

### A failed open wipes the reason

- Severity: incorrect
- Confidence: high
- Where: `src/book.cpp:377`, `src/book.cpp:724`
- Trigger: Encrypted MOBI, a file that is not a zip, a missing container, or an empty spine.
- Outcome: `extract_mobi`, `extract_zip`, and `parse_opf` call `set_error`, then `open` does `close(); return false`. `close()` ends with `error_.clear()`. Status is always "Could not open EPUB.", including for MOBI. The specific error never reaches the UI.

### The status buffer chops the page numbers off a long title

- Severity: incorrect
- Confidence: high
- Where: `src/main_window.cpp:627`
- Trigger: A title of about 150 bytes or more. The dash in the format is the three-byte em dash.
- Outcome: `snprintf` into 160 bytes stores a clipped title and a wrong page fragment (`12 o`, `12`, or a split em dash) instead of `12 of 340`.

### Spine skip treats "cover", "wrap", and "titlepage" as substrings

- Severity: incorrect
- Confidence: high
- Where: `src/book.cpp:841`
- Trigger: A real spine document named `recovered.xhtml`, `coverage.xhtml`, `unwrapping.xhtml`, or `titlepage-notes.xhtml`. Any HTML containing the literal `coverpage` is skipped the same way (`src/book.cpp:846`).
- Outcome: `select_href`, Next/Prev, Find, Index, and `href_for_id` all use this helper. Next/Prev never lands on that chapter. Find and Index omit it. Restoring a saved position there opens the start of the book. `wrap0000.xhtml` covers are skipped on purpose. The match is wider than that.

### "N of M" counts spine slots Next cannot reach

- Severity: incorrect
- Confidence: high
- Where: `src/main_window.cpp:628`, `src/book.cpp:863`
- Trigger: Open a book whose first spine item is a cover `start_href` skips. The sample `data/samples/kafka-metamorphosis.epub` is one: spine item 0 is `wrap0000.xhtml`, and the first topic is the next item.
- Outcome: Status uses `spine_index() + 1` and `spine_count()`. The skipped cover stays in the count, so the first topic is shown as page 2. Prev says "Start of book." There is no page 1.

### A PocketBook position with cpage 0 is dropped on reload

- Severity: data-loss
- Confidence: high
- Where: `src/lastread.cpp:41`, `src/lastread.cpp:81`
- Trigger: A sidecar (or catalog row) with `pbr` set and `cpage` 0. The stock reader rewrites a CFI in that shape.
- Outcome: `canonical_lastread` returns empty while `present` is still false, so the `pbr` cannot make the record present. `load_lastread` then sets `present` only from a non-empty canonical string, a non-empty href, or `cpage > 0`. The device position is treated as unread.

### Transfer treats "no position" as "same last page"

- Severity: incorrect
- Confidence: high
- Where: `src/library_window.cpp:1109`
- Trigger: The destination file already exists, and neither side has a real reading position.
- Outcome: Both canonical strings are empty, so they compare equal. The file is not overwritten. Status says "Skipped … (same last page)" for two books that have no last page. A replaced EPUB with the same filename stays stale.

### Find counts matches in squeezed text, then highlights the widget

- Severity: incorrect
- Confidence: high
- Where: `src/book.cpp:953`, `src/topic_view.cpp:483`
- Trigger: A phrase that is one match after whitespace is squeezed, but split by a paragraph break in the topic (`hello` at the end of one `<p>`, `world` at the start of the next). Or any earlier cross-boundary hit that shifts the occurrence index.
- Outcome: Search lowercases the squeezed plain text and records `occurrence` in that string. Activating the hit searches the text buffer, which has the paragraph newlines `walk` inserted. The highlight lands on a different match, or on none.

### A jump to a non-spine file leaves navigation on the old topic

- Severity: incorrect
- Confidence: high
- Where: `src/main_window.cpp:950`
- Trigger: A link or contents entry whose target exists in the extract tree but is not an `itemref` (a `toc.xhtml`, endnotes, a lofted footnote file).
- Outcome: The pane loads that file and the status becomes `title — (jump)`. `spine_index_` is not changed, history is not pushed, and `loaded_fragment_` is not set. Next/Prev steps from the previous chapter. Back does not return. The contents highlight stays on the old row.

### The dark palette does not recolour links

- Severity: incorrect
- Confidence: high
- Where: `src/topic_view.cpp:422`
- Trigger: Options → Appearance → "Light gray on near-black", then show a chapter that has links.
- Outcome: Each `<a>` gets a new tag with foreground `#0B3A96`. `apply_appearance` recolors only the shared `link` tag. The per-link tags are higher priority, so they win. Links stay dark blue on `#111111`.

### Delete does not drop catalog tags

- Severity: incorrect
- Confidence: medium
- Where: `src/library_window.cpp:756`
- Trigger: Delete a tagged book, then add another file that lands on the same `group/name` relative path.
- Outcome: The files and the `.lastread` sidecar are removed. `cat.save` is called with `tags_by_rel` unchanged. The new file shows the deleted book's tags.

### The copy thread reads `settings_.books` while the UI writes it

- Severity: crash
- Confidence: medium
- Where: `src/library_window.cpp:1103`
- Trigger: Start a library→device transfer of a book that has no `.lastread` sidecar (so the settings scan runs) and, while it runs, highlight or move in the open book.
- Outcome: `resolve_lastread` is called with `&settings_` on `copy_thread_`. Highlight, bookmark, and `persist` mutate `settings_.books` on the UI thread. No mutex covers that map. The data race can crash or corrupt the in-memory bookmark and highlight map.

## Closed

### Content links are resolved against the OPF directory and never decoded

- Severity: incorrect
- Confidence: high
- Where: `src/book.cpp:801`, `src/main_window.cpp:945`
- Trigger: A spine item `text/ch1.xhtml` with `<a href="ch2.xhtml">` (normal EPUB: relative to the document). Or a manifest href `Chapter%201.xhtml` when the zip entry is `Chapter 1.xhtml`.
- Outcome: `resolve` always joins a relative href to `opf_dir_`, not to the document's directory, and nothing percent-decodes. Jump reports "Jump not in this book" or opens the wrong file. The fragment never scrolls. Books whose documents sit beside the OPF hide it. `show_current` passes the document directory only to the image walker.
- Fixed in v1.2.4: A content link is resolved from the current document's directory and percent-decoded once. An encoded `..` that would leave the book is rejected. A spine or manifest href stays relative to the OPF, and an encoded manifest name opens the decoded zip entry. When the document-relative file is absent, the OPF-relative path is still used.

### Topic images are not confined to the book

- Severity: security
- Confidence: high
- Where: `src/topic_view.cpp` `topic_image_path`
- Trigger: An XHTML `<img src="/home/.../secret.png">` or `src="../../../../secret.png"` while the topic is shown.
- Outcome: A leading `/` is returned unchanged. Any other path is joined to the document directory with no check that the result stays under `extract_dir_`. Gdk loads and displays that image. `Book::resolve` does confine a leading `/`. This path does not.
- Fixed in v1.2.3: An image path has to stay under the extracted book, after symlinks are followed. A leading slash is rooted there. A link to a file outside that directory is not loaded. An image beside the chapter still loads.

### A document href can be read from outside the book cache

- Severity: security
- Confidence: high
- Where: `src/book.cpp` `resolve`, `load_document`
- Trigger: Open an EPUB whose spine or link href is `../../../../etc/passwd`, or whose zip contains a symlink to an outside regular file and a spine href of that name.
- Outcome: `resolve` joins a relative href to `opf_dir_` and only calls `lexically_normal()`. It does not require the result to stay under `extract_dir_`. `load_document` then opens that path. `Glib::file_test` follows a symlink and reports it regular, so a link left in the extract tree is read too. An absolute href is prefixed with the extract directory and stays inside.
- Fixed in v1.2.2: A resolved path has to stay under the extracted book, after symlinks are followed. A link to a file outside that directory is not opened. A chapter in the book, and a parent path that stays in the book, still load.


