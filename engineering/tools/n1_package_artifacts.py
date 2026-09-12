#!/usr/bin/env python3
"""Resolve sealed N-1 package bytes supplied for package-upgrade continuity.

GLYPHASTORE_N1_PACKAGE_DIR points at a directory of already-published packages.
This module never rebuilds a predecessor from HEAD: it only selects files that
already exist on disk and whose names carry the SemVer selected by the release
context.
"""

from __future__ import annotations

import os
import re
from pathlib import Path

N1_PACKAGE_DIR_ENVIRONMENT = "GLYPHASTORE_N1_PACKAGE_DIR"
CONTAINER_N1_MOUNT = "/n1-packages"

_EXCLUDED_NAME_TOKENS = ("debuginfo", "debugsource", "-dbgsym")
_LINUX_NAME = re.compile(
    r"^(?:lib)?glyphastore",
    re.IGNORECASE,
)


class N1PackageError(RuntimeError):
    """Sealed N-1 packages were requested but could not be resolved honestly."""


def supplied_n1_package_dir() -> Path | None:
    """Return the operator-supplied directory, or None when the env var is unset."""
    raw = os.environ.get(N1_PACKAGE_DIR_ENVIRONMENT, "").strip()
    if not raw:
        return None
    path = Path(raw)
    if path.is_symlink() or not path.is_dir():
        raise N1PackageError(
            f"{N1_PACKAGE_DIR_ENVIRONMENT}={raw!r} is not a regular directory of sealed packages"
        )
    return path.resolve()


def select_linux_n1_packages(directory: Path, backend: str, version: str) -> list[Path]:
    """Primary deb/rpm packages for ``version`` under ``directory`` (never rebuilt)."""
    if backend not in ("deb", "rpm"):
        raise N1PackageError(f"Linux N-1 package selection does not support backend {backend!r}")
    if not version or "/" in version or ".." in version:
        raise N1PackageError(f"refusing unsafe N-1 product version: {version!r}")
    if directory.is_symlink() or not directory.is_dir():
        raise N1PackageError(f"N-1 package directory is missing or not regular: {directory}")

    suffix = ".deb" if backend == "deb" else ".rpm"
    selected: list[Path] = []
    for path in sorted(directory.rglob(f"*{suffix}")):
        if path.is_symlink() or not path.is_file():
            continue
        name = path.name
        if any(token in name for token in _EXCLUDED_NAME_TOKENS):
            continue
        if _LINUX_NAME.match(name) is None:
            continue
        if version not in name:
            continue
        selected.append(path.resolve())
    if not selected:
        raise N1PackageError(
            f"no sealed {backend} packages for product version {version} under {directory}"
        )
    return selected


def upgrade_exercise_requested(previous: dict) -> bool:
    """True when a SemVer predecessor exists and sealed package bytes were supplied."""
    if not previous.get("available"):
        return False
    return supplied_n1_package_dir() is not None
