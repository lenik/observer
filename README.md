# observer

`observer` is a Linux desktop self-observation app built with C++17,
wxWidgets, SQLite, and Meson.  The shipped executable is `oremind`.

`oremind` stays in the background, shows a compact observation prompt
immediately after startup, and then repeats at the configured interval.  The
prompt is meant for a quick state check: one activity sentence plus optional
energy, mood, and grounding scores.

## Features

- Starts hidden and runs as a background desktop app.
- Shows a tray icon; left-click wakes the prompt, and the right-click menu can
  wake, open statistics/history, or quit.
- Detects an already running instance at startup, wakes it, prints a warning,
  and exits instead of launching a second background process.
- Shows a wxWidgets dialog immediately, then repeats with `wxTimer`.
- Uses a dark prompt UI with quote canvas, emoji score controls, keyboard
  shortcuts, and slide/fade transitions.
- Lets the interval be edited from the prompt; the interval unit label toggles
  between minutes and seconds.
- Provides a clickable `Quit` label in the prompt footer.
- Wakes from a desktop app launcher shortcut while waiting; single-instance
  handling keeps one background process. Launch again while the prompt is
  open to show statistics/history.
- Saves every submitted prompt, including empty activity notes after trimming.
- Treats default `energy`, `mood`, and `grounding` scores as unrecorded.
- Stores observations in SQLite by default, or daily log files when the storage
  path is a directory.
- Exits after five consecutive Escape cancels.

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
oremind --cancel 5
oremind --interval 0.5
oremind --interval 0
oremind --database ~/.observer/observer.sqlite3
oremind --database ~/.observer/logs/
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
- `-c`, `--cancel NUM`: exit after `NUM` consecutive cancels. The default is
  `5`.
- `-i`, `--interval NUM`: set the normal prompt interval in minutes. Fractional
  values are accepted. `0` makes the startup prompt a one-shot run.
- `-w`, `--weekstart MmSs`: set the calendar week start. Use `M`/`m` for
  Monday, or `S`/`s` for Sunday. The default is Monday.
- `-d`, `--database PATH`: use `PATH` as the SQLite database file, or as a log
  directory when it is an existing directory or ends with `/`.
- `--version`: print version and license information.

## Keyboard

- `Enter`: submit. Empty activity text is recorded as an empty note.
- `Escape`: cancel without recording. By default, five consecutive cancels
  exit the app; change this with `--cancel`.
- `F8`: snooze for 30 seconds.
- `Ctrl+Q`: quit from the prompt.
- `F1`: open the statistics window.
- `F4` / `F5`: decrease / increase energy by half a step.
- `F3` / `F6`: decrease / increase mood by half a step.
- `F2` / `F7`: decrease / increase grounding by half a step.
- Hold `Shift` with `F2`-`F7` to reverse the direction.
- Hold `Ctrl` for a `1.0` step, or `Alt` for a `5.0` step.

## Tray And Single Instance

While `oremind` is running, it keeps a tray icon available:

- Left-click the tray icon to show the prompt immediately.
- Right-click the tray icon for `Wake`, `Statistics / History`, and `Quit`.

Configure a desktop app launcher shortcut to run `oremind` when you want to
wake the prompt while waiting.  Only one background process is kept per user
session, so repeated launches contact the running instance instead of starting
another copy.  Launch `oremind` again while the prompt is open to show
statistics/history instead of waking a second prompt.

Tray and window icons are loaded from bundled `assets/icon-256.png`.  On
GTK/Linux, the icon is trimmed and converted to a 32-bit alpha bitmap before
being shown in the status notifier, so the panel background shows through
correctly instead of rendering black bars above and below the icon.

Starting `oremind` again does not create another background process.  The new
process contacts the running instance, asks it to wake the prompt, prints this
warning to standard error, and exits:

```text
warning: oremind is already running; waking existing instance.
```

## Statistics

Open statistics/history from a prompt with `F1`, or from the tray icon
right-click menu.

The statistics window has a toolbar for calendar, day, week, month, year,
today, previous period, next period, and close actions.  The calendar view is a
custom month grid with lunar day labels, common holiday highlighting, today and
selected-day markers, and record dots with non-empty record counts.  Selecting a
date shows the day's record count, empty-note count, total prompt duration, and
average energy, mood, and grounding values beside the calendar.

Day, week, month, and year views summarize records for the selected period.
They show records, empty notes, total prompt duration, average energy, average
mood, and average grounding.  A combined chart displays record count and prompt
duration as bars, with energy, mood, and grounding lines drawn above them.  The
record table below the chart reuses the same sortable history table used by the
calendar view and includes prompted time, submitted time, duration, energy,
mood, grounding, average score, activity, and quote.  Double-click a row or use
the context menu to edit a record in the same dialog used for new prompts.
`Delete` removes all selected rows immediately.  Changes are saved when the
statistics window closes.

Statistics keyboard shortcuts:

- `F1`: calendar view.
- `F5` / `F6` / `F7` / `F8`: day / week / month / year statistics.
- `Left` / `Right`: in calendar view, select the previous or next day; in
  day/week/month/year views, cycle through those four statistic views.
- `Up` / `Down`: in calendar view, select the previous or next week when the
  record table has no selection; in day/week/month/year views, move to the
  previous or next period when the table has no selection.  When rows are
  selected, move the selection within the table.
- `PageUp` / `PageDown`: move to the previous or next period.
- `Delete`: delete all selected records in the table.
- `Ctrl+T` or `Home`: return to today.
- `Esc`: close the statistics window.

## Storage

The default SQLite database is:

```text
~/.observer/observer.sqlite3
```

The app creates the parent directory and database table automatically.  Default
scores are stored as `NULL`.

When `--database` points to a directory, `oremind` writes daily logs:

```text
<datadir>/<yyyy-mm-dd>.log
```

Record format.  The leading time is the prompt time; the submitted time follows
the duration field.

```text
hh:mm:ss e2.5 m3.5 g4.0 duration 1:23 submitted hh:mm:ss
    activity text
```

Any score still at the default value is omitted.

## Build

### Dependencies

On Debian-like systems, install the usual build tools plus wxWidgets, SQLite,
gettext, and the local `bas-cpp` / `bas-ui` dependencies used by this project:

```bash
sudo apt install meson ninja-build pkg-config gettext sqlite3 libsqlite3-dev \
    libwxgtk3.2-dev check
```

Then make sure `bas-cpp` and `bas-ui` development packages are available to
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
