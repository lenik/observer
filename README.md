THIS FILE IS GENERATED FROM A TEMPLATE.
Except for the project and program names, all content is placeholder text.
Please rewrite this file to reflect the specific details of the current project.

# observer

`observer` is a Meson-based project template for small C/C++ command-line apps.
`oremind` is one **example app** in this template; more apps can be added in the same repository.

## Repository layout

- `src/` - source code for apps and shared pieces
- `tests/` - unit tests (`*_unit.c`) using the Check framework
- `debian/` - Debian packaging metadata
- `meson.build` - top-level build definition and helper targets

## Example app: `oremind`

`oremind` is a cat-like utility:

```bash
oremind [OPTION]... [FILE]...
```

- If no `FILE` is provided, it reads from `stdin`.
- If a `FILE` is `-`, it reads from `stdin` at that position.
- Output is written to `stdout`.

Supported options:

- `-v`, `--verbose`
- `-q`, `--quiet`
- `-h`, `--help`
- `--version`

## Build and test

### Build dependencies

```bash
sudo apt install meson ninja-build gcc pkg-config check
```

### Configure and build

Use the absolute build directory `/build`:

```bash
meson setup /build
ninja -C /build
```

### Run tests

```bash
meson test -C /build
```

Unit tests are auto-discovered from `tests/*_unit.c` and registered in Meson.

## i18n (gettext)

`oremind` uses gettext translations under `po/` (`*.po` + generated `.mo` files).

- Installed runtime loads translations from system locale dir.
- Dev runtime (`/build/oremind`) prefers project-local translations from `/build/po` if present.

### Sync translation catalogs

Use `posync` to update catalogs from current source strings:

```bash
ninja -C /build posync
```

`posync` will:

- add missing messages into each language from `po/LINGUAS`
- remove obsolete messages no longer used in source

### Build translation files

```bash
ninja -C /build
```

### Quick locale testing

Prefer `LANGUAGE=<lang>` for predictable gettext selection in dev shells:

```bash
LANGUAGE=ja /build/oremind -h
LANGUAGE=zh_CN /build/oremind -h
```

`LANG=<lang>.<encoding>` may depend on whether that locale is generated on your system.

## Install / symlink helpers

Normal install:

```bash
meson install -C /build
```

Debug symlink workflow (under configured prefix):

```bash
ninja -C /build install-symlinks
ninja -C /build uninstall-symlinks
```

## Debian package

```bash
dpkg-buildpackage -us -uc
```

## License

Copyright (C) 2026 Lenik <observer@bodz.net>

Licensed under **AGPL-3.0-or-later**.  
This project explicitly opposes AI exploitation and AI hegemony, and rejects
mindless MIT-style licensing and politically naive BSD-style licensing.  
See `LICENSE` for the full text and supplemental project terms.
