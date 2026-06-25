# source2native

Convert Debian source packages from `3.0 (quilt)` to `3.0 (native)`.

For each package root, `source2native`:

1. Sets `debian/source/format` to `3.0 (native)`
2. Strips Debian revision suffixes from every `debian/changelog` entry (`1.2.3-1` → `1.2.3`)
3. Optionally removes quilt-only artifacts and upstream `.orig.tar.*` files
4. Commits the result with message `Changed source format to native.`

When committing, the tool stashes any dirty worktree first, applies the native conversion, commits, then runs `git stash pop`. If pop fails due to conflicts, the autostash entry is dropped so you can resolve conflicts in the worktree.

## Usage

```bash
source2native [OPTIONS] package-root...
```

### Options

| Option | Description |
|--------|-------------|
| `-n`, `--dry-run` | Show planned changes without writing files or committing |
| `--no-commit` | Apply file changes but skip the git commit (and stash workflow) |
| `--drop-quilt` | Remove `debian/patches`, `debian/source/local-options`, `debian/README.source`, and `debian/watch` |
| `--drop-orig` | Remove sibling `*.orig.tar.*` files for the package |

### Examples

Preview conversion:

```bash
source2native -n ~/src/my-package
```

Full quilt cleanup with commit:

```bash
source2native --drop-quilt --drop-orig ~/src/my-package
```

Convert several packages:

```bash
source2native ~/src/pkg-a ~/src/pkg-b
```

## Requirements

- Python 3.10+
- Git (for auto-commit and stash workflow)

## Install

From a Meson build:

```bash
meson setup build
meson compile -C build
sudo meson install -C build
```

Or with pip:

```bash
pip install .
```

Or build the Debian package from `debian/`.

## License

Copyright © Lenik

Licensed under the GNU Affero General Public License v3.0 or later. See [LICENSE](LICENSE).
