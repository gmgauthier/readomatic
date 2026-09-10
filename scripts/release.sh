#!/bin/sh
# Build release artifacts: tarball, .deb, optional AppImage.
# Usage: ./scripts/release.sh [tarball|deb|appimage|all]

set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"

VERSION=$(sed -n "s/^  version: '\\(.*\\)',/\\1/p" meson.build | head -1)
DISTDIR="${ROOT}/dist"
JOB=${1:-all}

mkdir -p "$DISTDIR"

need_build() {
  if [ ! -f "${ROOT}/build/build.ninja" ]; then
    meson setup "${ROOT}/build" "$ROOT"
  fi
}

do_tarball() {
  need_build
  meson dist -C "${ROOT}/build" --no-tests --allow-dirty
  src="${ROOT}/build/meson-dist/readomatic-${VERSION}.tar.xz"
  if [ -f "$src" ]; then
    cp -f "$src" "$DISTDIR/"
    echo "tarball: ${DISTDIR}/readomatic-${VERSION}.tar.xz"
  else
    echo "meson dist did not produce readomatic-${VERSION}.tar.xz" >&2
    ls -la "${ROOT}/build/meson-dist" >&2 || true
    exit 1
  fi
}

do_deb() {
  dpkg-buildpackage -us -uc -b --no-sign
  mkdir -p "$DISTDIR"
  for f in "${ROOT}/../readomatic_${VERSION}"-*.deb \
           "${ROOT}/../readomatic-dbgsym_${VERSION}"-*.deb; do
    [ -e "$f" ] || continue
    mv -f "$f" "$DISTDIR/"
    echo "deb: $DISTDIR/$(basename "$f")"
  done
  for f in "${ROOT}/../readomatic_${VERSION}"-*.buildinfo \
           "${ROOT}/../readomatic_${VERSION}"-*.changes; do
    [ -e "$f" ] || continue
    mv -f "$f" "$DISTDIR/"
  done
}

do_appimage() {
  if ! command -v linuxdeploy >/dev/null 2>&1; then
    echo "linuxdeploy not on PATH; skip AppImage." >&2
    echo "See INSTALL.md §3." >&2
    return 0
  fi
  APPDIR="${ROOT}/build/AppDir"
  rm -rf "$APPDIR"
  meson setup "${ROOT}/build-appimage" "$ROOT" --prefix=/usr
  meson compile -C "${ROOT}/build-appimage"
  DESTDIR="$APPDIR" meson install -C "${ROOT}/build-appimage"
  export LINUXDEPLOY_OUTPUT_VERSION="$VERSION"
  export APPIMAGE_EXTRACT_AND_RUN=1
  PLUGIN_ARGS=""
  if command -v linuxdeploy-plugin-gtk >/dev/null 2>&1 || \
     [ -x "${ROOT}/scripts/linuxdeploy-plugin-gtk.sh" ]; then
    PLUGIN_ARGS="--plugin gtk"
    if [ -x "${ROOT}/scripts/linuxdeploy-plugin-gtk.sh" ]; then
      export PATH="${ROOT}/scripts:${PATH}"
    fi
  fi
  # shellcheck disable=SC2086
  linuxdeploy --appdir "$APPDIR" \
    --executable "${APPDIR}/usr/bin/readomatic" \
    --desktop-file "${APPDIR}/usr/share/applications/readomatic.desktop" \
    --icon-file "${APPDIR}/usr/share/icons/hicolor/scalable/apps/readomatic.svg" \
    $PLUGIN_ARGS \
    --output appimage
  mkdir -p "$DISTDIR"
  for f in "${ROOT}/Read-O-Matic-${VERSION}"-*.AppImage \
           "${ROOT}/Readomatic-${VERSION}"-*.AppImage \
           "${ROOT}/readomatic-${VERSION}"-*.AppImage \
           "${ROOT}/build/Read-O-Matic-${VERSION}"-*.AppImage \
           "${ROOT}"/*.AppImage; do
    [ -e "$f" ] || continue
    mv -f "$f" "$DISTDIR/"
  done
  echo "appimage: $(ls -1 "$DISTDIR"/*.AppImage 2>/dev/null | tail -1)"
}

case "$JOB" in
  tarball) do_tarball ;;
  deb)     do_deb ;;
  appimage) do_appimage ;;
  all)
    do_tarball
    do_deb
    do_appimage
    ;;
  *)
    echo "Usage: $0 [tarball|deb|appimage|all]" >&2
    exit 2
    ;;
esac
