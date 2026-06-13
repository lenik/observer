# observer

`observer` is a Linux desktop self-observation app built with C++17,
wxWidgets, SQLite, and Meson.  The shipped executable is `oremind`.

`oremind` stays in the background, shows a compact observation prompt
immediately after startup, and then repeats at the configured interval.  The
prompt is meant for a quick state check: one activity sentence plus optional
energy, mood, and grounding scores.

## Features

- Starts hidden and runs as a background desktop app.
- Shows a wxWidgets dialog immediately, then repeats with `wxTimer`.
- Uses a dark prompt UI with quote canvas, emoji score controls, keyboard
  shortcuts, and slide/fade transitions.
- Lets the interval be edited from the prompt; the interval unit label toggles
  between minutes and seconds.
- Provides a clickable `Quit` label in the prompt footer.
- Registers `Win+Alt+G` on X11 to show the prompt immediately while waiting.
- Saves non-empty, trimmed activity notes only.
- Treats default `energy`, `mood`, and `grounding` scores as unrecorded.
- Stores observations in SQLite by default, or daily log files when the storage
  path is a directory.
- Exits after three consecutive Escape cancels.

## Usage

```bash
oremind [OPTION]...
```

Common examples:

```bash
oremind
oremind --locale zh_CN
oremind --theme light
oremind --opacity 85
oremind --interval 0.5
oremind --interval 0
oremind --sqlite-db ~/.observer/observer.sqlite3
oremind --sqlite-db ~/.observer/logs/
```

Options:

- `-v`, `--verbose`: increase logging verbosity.
- `-q`, `--quiet`: decrease logging verbosity.
- `-h`, `--help`: show command-line help.
- `-l`, `--locale LOCALE`: set the UI locale before translations are loaded,
  for example `en`, `zh_CN`, `zh_TW`, `ja`, `ko`, `fr`, or `de`.
- `-t`, `--theme light|dark`: select the prompt theme.
- `-o`, `--opacity NUM`: set the final dialog opacity from `0` to `100`.
  The default is `75`.
- `-i`, `--interval NUM`: set the normal prompt interval in minutes. Fractional
  values are accepted. `0` makes the startup prompt a one-shot run.
- `-d`, `--sqlite-db PATH`: use `PATH` as the SQLite database file, or as a log
  directory when it is an existing directory or ends with `/`.
- `--version`: print version and license information.

## Keyboard

- `Enter`: submit. Empty activity text is not recorded.
- `Escape`: cancel without recording. Three consecutive cancels exit the app.
- `Ctrl+S`: snooze for 30 seconds.
- `Ctrl+Q`: quit from the prompt.
- `Win+Alt+G`: show the prompt immediately while the app is waiting. This uses
  an X11 global hotkey on Linux.
- `F1` / `F2`: decrease / increase energy by half a step.
- `F3` / `F4`: decrease / increase mood by half a step.
- `F5` / `F6`: decrease / increase grounding by half a step.

## Storage

The default SQLite database is:

```text
~/.observer/observer.sqlite3
```

The app creates the parent directory and database table automatically.  Default
scores are stored as `NULL`.

When `--sqlite-db` points to a directory, `oremind` writes daily logs:

```text
<datadir>/<yyyy-mm-dd>.log
```

Record format:

```text
hh:mm:ss e2.5 m3.5 g4.0
    activity text
```

Any score still at the default value is omitted.

## Build

### Dependencies

On Debian-like systems, install the usual build tools plus wxWidgets, SQLite,
gettext, and the local `bas-c` / `bas-cpp` dependencies used by this project:

```bash
sudo apt install meson ninja-build pkg-config gettext sqlite3 libsqlite3-dev \
    libwxgtk3.2-dev libx11-dev check
```

Then make sure `bas-c` and `bas-cpp` development packages are available to
`pkg-config`.

### Configure and compile

```bash
meson setup _build
meson compile -C _build
```

Run the app from the build tree:

```bash
_build/oremind
```

### Install

```bash
meson install -C _build
```

For local development against the configured prefix:

```bash
meson compile -C _build install-symlinks
meson compile -C _build uninstall-symlinks
```

## Translations

Translations live under `po/`.  Build files generate `.mo` files as part of the
normal Meson build.

Quick language checks:

```bash
LANGUAGE=zh_CN _build/oremind -h
LANGUAGE=ja _build/oremind -h
LANGUAGE=ko _build/oremind -h
```

## Debian Package

```bash
dpkg-buildpackage -us -uc
```

## License

Copyright (C) 2026 Lenik <observer@bodz.net>

Licensed under **AGPL-3.0-or-later**.  See `LICENSE` for the full text and
supplemental project terms.
