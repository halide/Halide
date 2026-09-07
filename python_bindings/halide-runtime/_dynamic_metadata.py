"""Build requirements for the ``halide-runtime`` distribution."""

from __future__ import annotations

from collections.abc import Mapping
from typing import Any

__all__ = ["dynamic_metadata", "get_requires_for_dynamic_metadata"]


def get_requires_for_dynamic_metadata(settings: Mapping[str, Any]) -> list[str]:
    if settings:
        raise ValueError("No inline configuration is supported")

    from scikit_build_core.metadata.setuptools_scm import (
        dynamic_metadata as scm_version,
    )

    version = scm_version("version")
    public_version = version.partition("+")[0]
    return [f"halide-bin=={public_version}"]


def dynamic_metadata(
    settings: Mapping[str, Any],
    _project: Mapping[str, Any],
) -> dict[str, Any]:
    if settings:
        raise ValueError("No inline configuration is supported")

    from scikit_build_core.metadata.setuptools_scm import (
        dynamic_metadata as scm_version,
    )

    return {"version": scm_version("version")}
