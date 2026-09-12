# Installing Read-O-Matic

Four ways to get a binary, in the order LCOS cares about:

| Artifact | Who it is for |
|---|---|
| **`.deb`** | LCOS, Devuan Excalibur, Debian Trixie. Preferred. |
| **Source tarball** | Distro packagers and `meson setup && ninja install`. |
| **AppImage** | Fallback for distros that do not install `.deb` files. gtkmm only; no codec bundle. Published on the GitHub/Gitea release. |
| **Git build** | Developers. See below. |

Version comes from `meson.build` (currently `0.1.1`).

## Runtime needs

- GTK 3 / gtkmm-3.0
- libarchive
- libxml2
- fontconfig

On Debian / Devuan / LCOS:

```
sudo apt install libgtkmm-3.0-1t64 libarchive13 libxml2 fontconfig
```

(Package names on older Debian may be `libgtkmm-3.0-1v5`.)

## 1. Debian package (preferred)

From a release `.deb`:

```
sudo apt install ./dist/readomatic_0.1.1-1_amd64.deb
```

Or, from this tree:

```
./scripts/release.sh deb
sudo apt install ./dist/readomatic_0.1.1-1_amd64.deb
```

That installs:

- `/usr/bin/readomatic`
- `/usr/share/applications/readomatic.desktop`
- `/usr/share/icons/hicolor/scalable/apps/readomatic.svg`
- `/usr/share/readomatic/skin/lcos/lcos.css`
- `/usr/share/readomatic/brand/icon-tile.svg`

Launch from the menu or `readomatic`. Config is `~/.config/readomatic/readomatic.ini`.

Uninstall: `sudo apt remove readomatic`.

## 2. Source tarball

`meson dist` produces `build/meson-dist/readomatic-VERSION.tar.xz` (demo EPUBs under `data/samples/` are git-only, not in the tarball).

```
tar -xf readomatic-0.1.1.tar.xz
cd readomatic-0.1.1
sudo apt install build-essential meson ninja-build pkg-config \
  libgtkmm-3.0-dev libarchive-dev libxml2-dev libfontconfig1-dev
meson setup build --prefix=/usr
meson compile -C build
sudo meson install -C build
```

`./scripts/release.sh tarball` runs `meson dist` for you.

## 3. AppImage (fallback)

LCOS 0.3 already runs AppImages. The image bundles gtkmm from the build host. There is no GStreamer/codec story here.

```
./scripts/release.sh appimage
```

Requires `linuxdeploy` on `$PATH` (see <https://github.com/linuxdeploy/linuxdeploy>). Output: `dist/Read-O-Matic-VERSION-x86_64.AppImage` (name may vary with linuxdeploy).

```
chmod +x Read-O-Matic-*.AppImage
./Read-O-Matic-*.AppImage
```

The AppImage runtime sets `APPDIR`; Read-O-Matic looks for skin and brand under `$APPDIR/usr/share/readomatic`. Leave `APPDIR` unset for `.deb` and `meson install` builds.

## 4. Developer build (no install)

```
meson setup build
meson compile -C build
./build/readomatic
```

The binary finds CSS via `SOURCE_ROOT` in the build tree. `READOMATIC_DATA` overrides that.

## One command for every artifact

```
./scripts/release.sh all
```

Writes tarball, `.deb`, and AppImage (if `linuxdeploy` is present) under `dist/`. The GitHub/Gitea release includes the AppImage as the non-deb fallback.

## What this project will not ship

- A WebKit / browser engine
- A systemd unit
- Vendored Clearlooks / xfwm themes (Recommends the desktop theme)
- Demo EPUBs in the tarball (they stay git-only)
