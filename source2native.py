#!/usr/bin/env python3
"""Convert Debian package trees from quilt (3.0 quilt) to native (3.0 native)."""

from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path

VERSION = "@MESON_PROJECT_VERSION@"
if VERSION.startswith("@") and VERSION.endswith("@"):
    VERSION = "1.0.0-dev"

NATIVE_FORMAT = "3.0 (native)\n"
COMMIT_MESSAGE = "Changed source format to native."
STASH_MESSAGE = "source2native: autostash before native conversion"
CHANGELOG_HEADER = re.compile(
    r"^(\S+) \(([^)]+)\) ([^;]+); urgency=(\S+)\s*$"
)

QUILT_ARTIFACTS = (
    "debian/patches",
    "debian/source/local-options",
    "debian/README.source",
)


@dataclass
class ChangeReport:
    path: Path
    actions: list[str] = field(default_factory=list)


def strip_debian_revision(version: str) -> str:
    """Return upstream portion of a Debian version (drop debian_revision)."""
    epoch = ""
    rest = version
    if ":" in rest:
        epoch, rest = rest.split(":", 1)
        epoch = f"{epoch}:"

    if "-" not in rest:
        return version

    upstream, revision = rest.rsplit("-", 1)
    if revision and revision[0].isdigit():
        return f"{epoch}{upstream}"
    return version


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def write_text(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")


def remove_path(path: Path) -> None:
    if path.is_dir():
        shutil.rmtree(path)
    elif path.is_file():
        path.unlink()


def normalize_changelog(content: str) -> tuple[str, int]:
    changed = 0
    lines: list[str] = []

    for line in content.splitlines(keepends=True):
        match = CHANGELOG_HEADER.match(line.rstrip("\n"))
        if not match:
            lines.append(line)
            continue

        package, version, dist, urgency = match.groups()
        native_version = strip_debian_revision(version)
        if native_version == version:
            lines.append(line)
            continue

        changed += 1
        lines.append(
            f"{package} ({native_version}) {dist}; urgency={urgency}\n"
        )

    return "".join(lines), changed


def format_needs_update(format_path: Path) -> bool:
    if not format_path.is_file():
        return True
    return read_text(format_path).replace("\r\n", "\n") != NATIVE_FORMAT


def find_git_root(start: Path) -> Path | None:
    try:
        out = subprocess.run(
            ["git", "-C", str(start), "rev-parse", "--show-toplevel"],
            check=True,
            capture_output=True,
            text=True,
        )
    except (subprocess.CalledProcessError, FileNotFoundError):
        return None
    return Path(out.stdout.strip())


def git_has_local_changes(git_root: Path) -> bool:
    result = subprocess.run(
        ["git", "-C", str(git_root), "status", "--porcelain"],
        check=True,
        capture_output=True,
        text=True,
    )
    return bool(result.stdout.strip())


def git_stash_push(git_root: Path, dry_run: bool) -> bool:
    """Stash dirty worktree; return True if a stash entry was created."""
    if not git_has_local_changes(git_root):
        return False
    if dry_run:
        print(f"{git_root}: would git stash", flush=True)
        return True
    print(f"{git_root}: git stash", flush=True)
    subprocess.run(
        ["git", "-C", str(git_root), "stash", "push", "-m", STASH_MESSAGE],
        check=True,
    )
    return True


def find_autostash_ref(git_root: Path) -> str | None:
    result = subprocess.run(
        ["git", "-C", str(git_root), "stash", "list"],
        check=True,
        capture_output=True,
        text=True,
    )
    for line in result.stdout.splitlines():
        if STASH_MESSAGE in line:
            return line.split(":", 1)[0]
    return None


def git_stash_pop(git_root: Path, dry_run: bool) -> None:
    if dry_run:
        print(f"{git_root}: would git stash pop", flush=True)
        return
    result = subprocess.run(
        ["git", "-C", str(git_root), "stash", "pop"],
        capture_output=True,
        text=True,
    )
    if result.returncode == 0:
        print(f"{git_root}: git stash pop", flush=True)
        return

    msg = result.stderr.strip() or result.stdout.strip() or "unknown error"
    print(f"{git_root}: git stash pop failed: {msg}", file=sys.stderr)

    stash_ref = find_autostash_ref(git_root)
    if stash_ref is not None:
        drop = subprocess.run(
            ["git", "-C", str(git_root), "stash", "drop", stash_ref],
            capture_output=True,
            text=True,
        )
        if drop.returncode == 0:
            print(
                f"{git_root}: git stash drop ({stash_ref}, after pop conflict)",
                flush=True,
            )
        else:
            drop_msg = drop.stderr.strip() or drop.stdout.strip() or "unknown error"
            print(f"{git_root}: git stash drop failed: {drop_msg}", file=sys.stderr)

    raise subprocess.CalledProcessError(
        result.returncode, result.args, result.stdout, result.stderr
    )


def git_commit(root: Path, changed_paths: list[Path], message: str, dry_run: bool) -> bool:
    git_root = find_git_root(root)
    if git_root is None:
        print(f"{root}: not a git repository, skipping commit", file=sys.stderr)
        return False

    rel_paths = sorted(str(path.relative_to(git_root)) for path in changed_paths)
    if not rel_paths:
        return False

    if dry_run:
        print(f"{root}: would commit: {', '.join(rel_paths)}")
        return True

    subprocess.run(
        ["git", "-C", str(git_root), "add", "-A", "--"] + rel_paths,
        check=True,
    )
    status = subprocess.run(
        ["git", "-C", str(git_root), "diff", "--cached", "--quiet"],
        capture_output=True,
    )
    if status.returncode == 0:
        print(f"{root}: nothing staged, skipping commit")
        return False

    subprocess.run(
        ["git", "-C", str(git_root), "commit", "-m", message],
        check=True,
    )
    print(f"{root}: committed ({message})")
    return True


def discover_orig_tarballs(package_root: Path) -> list[Path]:
    parent = package_root.parent
    name = package_root.name
    patterns = (f"{name}_*.orig.tar.*", f"{name}*.orig.tar.*")
    found: list[Path] = []
    for pattern in patterns:
        found.extend(parent.glob(pattern))
    return sorted(set(found))


def apply_changes(
    package_root: Path,
    *,
    format_path: Path,
    changelog_path: Path,
    changed_paths: list[Path],
) -> None:
    if format_path in changed_paths:
        write_text(format_path, NATIVE_FORMAT)
    if changelog_path in changed_paths and changelog_path.is_file():
        updated, _ = normalize_changelog(read_text(changelog_path))
        write_text(changelog_path, updated)
    for path in changed_paths:
        if path in (format_path, changelog_path):
            continue
        remove_path(path)


def process_package(
    package_root: Path,
    *,
    dry_run: bool,
    no_commit: bool,
    drop_quilt: bool,
    drop_orig: bool,
) -> int:
    package_root = package_root.resolve()
    debian = package_root / "debian"
    if not debian.is_dir():
        print(f"{package_root}: no debian/ directory, skipping", file=sys.stderr)
        return 1

    report = ChangeReport(path=package_root)
    changed_paths: list[Path] = []

    format_path = debian / "source" / "format"
    if format_needs_update(format_path):
        report.actions.append(f"set {format_path.relative_to(package_root)} to native")
        changed_paths.append(format_path)

    changelog_path = debian / "changelog"
    if changelog_path.is_file():
        _, count = normalize_changelog(read_text(changelog_path))
        if count:
            report.actions.append(
                f"strip debian revision from {count} changelog "
                f"entr{'y' if count == 1 else 'ies'}"
            )
            changed_paths.append(changelog_path)

    quilt_paths = [package_root / rel for rel in QUILT_ARTIFACTS]
    present_quilt = [path for path in quilt_paths if path.exists()]
    if present_quilt:
        if drop_quilt:
            for path in present_quilt:
                report.actions.append(f"remove {path.relative_to(package_root)}")
                changed_paths.append(path)
        else:
            rel = ", ".join(str(p.relative_to(package_root)) for p in present_quilt)
            print(
                f"{package_root}: quilt artifacts remain ({rel}); "
                "pass --drop-quilt to remove",
                file=sys.stderr,
            )

    orig_tarballs = discover_orig_tarballs(package_root)
    if orig_tarballs:
        if drop_orig:
            for path in orig_tarballs:
                report.actions.append(f"remove {path}")
                changed_paths.append(path)
        else:
            rel = ", ".join(str(p) for p in orig_tarballs)
            print(
                f"{package_root}: upstream orig tarballs found ({rel}); "
                "pass --drop-orig to remove",
                file=sys.stderr,
            )

    watch_path = debian / "watch"
    if watch_path.is_file() and drop_quilt:
        report.actions.append(f"remove {watch_path.relative_to(package_root)}")
        changed_paths.append(watch_path)

    if not report.actions:
        print(f"{package_root}: already native, no changes")
        return 0

    prefix = "would " if dry_run else ""
    print(f"{package_root}: {prefix}apply:", flush=True)
    for action in report.actions:
        print(f"  - {action}", flush=True)

    git_root = find_git_root(package_root)
    use_stash = not no_commit and git_root is not None
    stashed = False
    if use_stash:
        stashed = git_stash_push(git_root, dry_run)

    try:
        if not dry_run:
            apply_changes(
                package_root,
                format_path=format_path,
                changelog_path=changelog_path,
                changed_paths=changed_paths,
            )

        if not no_commit:
            git_commit(package_root, changed_paths, COMMIT_MESSAGE, dry_run)
    finally:
        if stashed and git_root is not None:
            git_stash_pop(git_root, dry_run)

    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="source2native",
        description="Ensure Debian packages use 3.0 (native) source format.",
    )
    parser.add_argument(
        "package_roots",
        nargs="+",
        type=Path,
        metavar="package-root",
        help="Root directory of a Debian source package",
    )
    parser.add_argument(
        "-n",
        "--dry-run",
        action="store_true",
        help="Show planned changes without writing files or committing",
    )
    parser.add_argument(
        "--no-commit",
        action="store_true",
        help="Apply changes but do not create a git commit",
    )
    parser.add_argument(
        "--drop-quilt",
        action="store_true",
        help="Remove quilt-only paths (debian/patches, local-options, README.source, watch)",
    )
    parser.add_argument(
        "--drop-orig",
        action="store_true",
        help="Remove sibling .orig.tar.* files for this package",
    )
    parser.add_argument(
        "--version",
        action="version",
        version=f"%(prog)s {VERSION}",
    )
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)

    rc = 0
    for root in args.package_roots:
        if not root.exists():
            print(f"{root}: path does not exist", file=sys.stderr)
            rc = 1
            continue
        try:
            result = process_package(
                root,
                dry_run=args.dry_run,
                no_commit=args.no_commit,
                drop_quilt=args.drop_quilt,
                drop_orig=args.drop_orig,
            )
        except subprocess.CalledProcessError:
            rc = 1
            continue
        rc = rc or result
    return rc


if __name__ == "__main__":
    raise SystemExit(main())
