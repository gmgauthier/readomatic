# Bug backlog

Reviewed 2026-10-01 against the 1.2.0 sources.

`meson test` runs `tests/test_place.cpp` (`place`) and `tests/test_confine.cpp` (`confine`). `place` checks history dedup, canonical last-read, sidecar save/load, and that a PocketBook CFI stored with `cpage` 0 reloads as a position, from the sidecar and from `explorer-3.db`. It checks that two unread books are not the same last page, while two saved places that match still are. `confine` checks that a relative href which leaves the cache is not opened, that a symlink to an outside file is not opened, that an absolute href stays under the cache, and that a chapter and an in-book parent path still load. `image` checks that a topic image stays under the extracted book, that a leading slash and a symlink to an outside file do not leave it, and that an image written beside the chapter still resolves. `links` checks that a content link in a subdirectory resolves beside that chapter, that an OPF-relative spine href still resolves, and that a percent-encoded manifest href opens the decoded zip entry. An encoded `..` does not leave the book. `%2520` is decoded once. `open` checks that a failed open keeps its reason: not a zip, not a MOBI, a missing or unreadable container, a missing OPF, and an empty spine. A later successful open clears that reason. `status` checks that a long title, including one that contains an em dash, keeps the full `N of M` suffix. `spine` checks that `recovered`, `coverage`, `unwrapping`, `titlepage-notes`, and a chapter that mentions `coverpage` stay reachable, while `wrap0000`, `cover`, `titlepage`, and an empty document stay skipped. `pages` checks that a leading `wrap0000` is left out of `N of M`, so the first readable chapter is `1 of 2`. `find` checks that a phrase split by a paragraph or a `br` is not a hit, and that the next real hit in that chapter is occurrence 0. A run of spaces, or a no-break space, still marks the original span. `jump` checks that a file which is not in the spine becomes the current topic, that Next and Prev return to the chapter that was open, and that Back's history entry is that chapter. `colour` checks that palette 2 uses the light link ink and every other palette keeps the dark blue. `catalog` checks that deleting a book drops its tags, so a new file on the same `group/name` path does not inherit them, and a sibling book's tags stay. `copy` checks that a transfer reads a copy of the book map taken before it starts, so a later highlight does not change that place, and a sidecar still wins over the copy.

Zip entry names containing `..` are rejected on extract (`ARCHIVE_EXTRACT_SECURE_NODOTDOT`). The holes below are the path used after extract.

## Open

None.

## Closed

### A squeezed find hit is not the text in the topic

- Severity: incorrect
- Confidence: high
- Where: `src/topic_view.cpp` `select_match`
- Trigger: Find a phrase that matches after spaces are collapsed, or that is split by a no-break space. The topic still shows those original characters.
- Outcome: Search counts the hit in the squeezed line. The highlight searches the raw topic for that squeezed phrase, so the mark lands on a later hit or on nothing.
- Fixed in v1.2.16: The highlight counts hits with the same squeeze and marks the original span.

### The copy thread reads `settings_.books` while the UI writes it

- Severity: crash
- Confidence: medium
- Where: `src/library_window.cpp` `on_transfer`, `run_copy`
- Trigger: Start a library→device transfer of a book that has no `.lastread` sidecar (so the settings scan runs) and, while it runs, highlight or move in the open book.
- Outcome: `resolve_lastread` is called with `&settings_` on `copy_thread_`. Highlight, bookmark, and `persist` mutate `settings_.books` on the UI thread. No mutex covers that map. The data race can crash or corrupt the in-memory bookmark and highlight map.
- Fixed in v1.2.15: The transfer copies the book map before the thread starts and reads that copy. A highlight made while the copy runs stays on the UI map. A sidecar on disk still wins over the copy.

### Delete does not drop catalog tags

- Severity: incorrect
- Confidence: medium
- Where: `src/library_window.cpp` `on_delete_books`, `src/library_catalog.cpp` `forget`
- Trigger: Delete a tagged book, then add another file that lands on the same `group/name` relative path.
- Outcome: The files and the `.lastread` sidecar are removed. `cat.save` is called with `tags_by_rel` unchanged. The new file shows the deleted book's tags.
- Fixed in v1.2.14: Delete drops that book's catalog tags before the catalog is saved. A new file on the same path has no tags. Another book's tags stay.

### The dark palette does not recolour links

- Severity: incorrect
- Confidence: high
- Where: `src/topic_view.cpp` `apply_appearance`, `walk`
- Trigger: Options → Appearance → "Light gray on near-black", then show a chapter that has links.
- Outcome: Each `<a>` gets a new tag with foreground `#0B3A96`. `apply_appearance` recolors only the shared `link` tag. The per-link tags are higher priority, so they win. Links stay dark blue on `#111111`.
- Fixed in v1.2.13: Palette 2 paints links `#8CB4E8`, including a chapter opened while that palette is already selected. Every other palette keeps `#0B3A96`. Changing palette recolours the links already on the page.

### A jump to a non-spine file leaves navigation on the old topic

- Severity: incorrect
- Confidence: high
- Where: `src/main_window.cpp` `on_jump`, `src/book.cpp` `follow_path`
- Trigger: A link or contents entry whose target exists in the extract tree but is not an `itemref` (a `toc.xhtml`, endnotes, a lofted footnote file).
- Outcome: The pane loads that file and the status becomes `title — (jump)`. `spine_index_` is not changed, history is not pushed, and `loaded_fragment_` is not set. Next/Prev steps from the previous chapter. Back does not return. The contents highlight stays on the old row.
- Fixed in v1.2.12: The file becomes the current topic. Status names that file. Next or Prev returns to the chapter that was open, and history can step back to it. A spine target still moves the spine index.

### Find counts matches in squeezed text, then highlights the widget

- Severity: incorrect
- Confidence: high
- Where: `src/book.cpp` `Book::search`, `src/topic_view.cpp` `select_match`
- Trigger: A phrase that is one match after whitespace is squeezed, but split by a paragraph break in the topic (`hello` at the end of one `<p>`, `world` at the start of the next). Or any earlier cross-boundary hit that shifts the occurrence index.
- Outcome: Search lowercases the squeezed plain text and records `occurrence` in that string. Activating the hit searches the text buffer, which has the paragraph newlines `walk` inserted. The highlight lands on a different match, or on none.
- Fixed in v1.2.11: A paragraph break or `br` ends a line before the query is matched, so a phrase split across paragraphs is not a hit. The next match inside one paragraph is occurrence 0, which is the hit `select_match` highlights. A leading cover is still omitted.

### Transfer treats "no position" as "same last page"

- Severity: incorrect
- Confidence: high
- Where: `src/library_window.cpp` `run_copy`, `src/lastread.cpp` `same_last_page`
- Trigger: The destination file already exists, and neither side has a real reading position.
- Outcome: Both canonical strings are empty, so they compare equal. The file is not overwritten. Status says "Skipped … (same last page)" for two books that have no last page. A replaced EPUB with the same filename stays stale.
- Fixed in v1.2.10: A transfer skips only when both sides have a saved place and that place matches. Two unread books are copied. Two matching places, including a CFI with `cpage` 0, still skip.

### A PocketBook position with cpage 0 is dropped on reload

- Severity: data-loss
- Confidence: high
- Where: `src/lastread.cpp` `canonical_lastread`, `load_lastread`
- Trigger: A sidecar (or catalog row) with `pbr` set and `cpage` 0. The stock reader rewrites a CFI in that shape.
- Outcome: `canonical_lastread` returns empty while `present` is still false, so the `pbr` cannot make the record present. `load_lastread` then sets `present` only from a non-empty canonical string, a non-empty href, or `cpage > 0`. The device position is treated as unread.
- Fixed in v1.2.9: A non-empty CFI is a position before `present` is set, and reload keeps it when `cpage` is 0. An empty sidecar, and a device row with no position and `cpage` 0, stay unread.

### "N of M" counts spine slots Next cannot reach

- Severity: incorrect
- Confidence: high
- Where: `src/main_window.cpp` `show_current`, `src/book.cpp` `readable_span`
- Trigger: Open a book whose first spine item is a cover `start_href` skips. The sample `data/samples/kafka-metamorphosis.epub` is one: spine item 0 is `wrap0000.xhtml`, and the first topic is the next item.
- Outcome: Status uses `spine_index() + 1` and `spine_count()`. The skipped cover stays in the count, so the first topic is shown as page 2. Prev says "Start of book." There is no page 1.
- Fixed in v1.2.8: `N of M` counts spine items Next and Prev can reach. A leading `wrap0000` is not a page, so the first chapter is `1 of 2` when two chapters follow it.

### Spine skip treats "cover", "wrap", and "titlepage" as substrings

- Severity: incorrect
- Confidence: high
- Where: `src/book.cpp` `skip_spine_href`
- Trigger: A real spine document named `recovered.xhtml`, `coverage.xhtml`, `unwrapping.xhtml`, or `titlepage-notes.xhtml`. Any HTML containing the literal `coverpage` is skipped the same way.
- Outcome: `select_href`, Next/Prev, Find, Index, and `href_for_id` all use this helper. Next/Prev never lands on that chapter. Find and Index omit it. Restoring a saved position there opens the start of the book. `wrap0000.xhtml` covers are skipped on purpose. The match is wider than that.
- Fixed in v1.2.7: A spine name is skipped when its stem is `cover`, `titlepage`, or `wrap` plus digits, as in `wrap0000`. `recovered`, `coverage`, `unwrapping`, `titlepage-notes`, and `wrapper-notes` stay. A chapter that merely contains the word `coverpage` stays. An empty document is still skipped. The status count still includes the skipped slots.

### The status buffer chops the page numbers off a long title

- Severity: incorrect
- Confidence: high
- Where: `src/main_window.cpp` `show_current`
- Trigger: A title of about 150 bytes or more. The dash in the format is the three-byte em dash.
- Outcome: `snprintf` into 160 bytes stores a clipped title and a wrong page fragment (`12 o`, `12`, or a split em dash) instead of `12 of 340`.
- Fixed in v1.2.6: The status line keeps the whole title and the `N of M` suffix. The count is still the spine length, including items Next skips.

### A failed open wipes the reason

- Severity: incorrect
- Confidence: high
- Where: `src/book.cpp` `close`, `Book::open`
- Trigger: Encrypted MOBI, a file that is not a zip, a missing container, or an empty spine.
- Outcome: `extract_mobi`, `extract_zip`, and `parse_opf` call `set_error`, then `open` does `close(); return false`. `close()` ends with `error_.clear()`. Status is always "Could not open EPUB.", including for MOBI. The specific error never reaches the UI.
- Fixed in v1.2.5: A failed open still clears the extract directory and reports the book closed, and it keeps the reason. A later successful open, and an ordinary close, clear that reason.

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


