"""
Dynamic metadata provider for the `halide` pip package.
"""

from __future__ import annotations
from typing import Any
from collections.abc import Mapping

__all__ = ["dynamic_metadata"]


def dynamic_metadata(
    settings: Mapping[str, Any],
    project: Mapping[str, Any],
) -> dict[str, Any]:
    import os

    if settings:
        raise ValueError("No inline configuration is supported")

    if os.environ.get("HALIDE_SPLIT_BUILD"):
        deps = [
            "imageio>=2",
            "pillow; platform_machine == 'armv8l' or platform_machine == 'armv7l'",
            "numpy>=1.26",
            f"halide-bin=={project['version']}",
        ]
    else:
        deps = []

    return {"dependencies": deps}
