"""
Dynamic metadata provider for the `halide` pip package.

Computes the version via setuptools_scm -- identically to
scikit_build_core's builtin `scikit_build_core.metadata.setuptools_scm`
provider -- and reuses that exact value to pin the `halide-bin` runtime
dependency when building in split mode (HALIDE_SPLIT_BUILD=1 -- see
.github/workflows/pip.yml), so the `==` pin and the actual `halide-bin`
version can never silently diverge. Outside of split mode (e.g. a plain
`pip install .`), `dependencies` is identical to the static list this
project shipped before the split.
"""

from __future__ import annotations

import os

__all__ = ["dynamic_metadata", "get_requires_for_dynamic_metadata"]


def __dir__() -> list[str]:
    return __all__


_STATIC_DEPENDENCIES = [
    "imageio>=2",
    "pillow; platform_machine == 'armv8l' or platform_machine == 'armv7l'",
    "numpy>=1.26",
]


def _compute_version() -> str:
    from setuptools_scm import Configuration, _get_version

    config = Configuration.from_file("pyproject.toml")
    try:
        version = _get_version(config, force_write_version_files=True)
    except TypeError:  # setuptools_scm < 8
        version = _get_version(config)

    if version is None:
        msg = f"setuptools-scm was unable to detect version for {config.absolute_root}."
        raise ValueError(msg)

    return version


def dynamic_metadata(
    field: str,
    settings: dict[str, object] | None = None,
) -> str | list[str]:
    if settings:
        msg = "No inline configuration is supported"
        raise ValueError(msg)

    if field == "version":
        return _compute_version()

    if field == "dependencies":
        if not os.environ.get("HALIDE_SPLIT_BUILD"):
            return list(_STATIC_DEPENDENCIES)
        return [*_STATIC_DEPENDENCIES, f"halide-bin=={_compute_version()}"]

    msg = f"Only 'version' and 'dependencies' fields are supported, not {field!r}"
    raise ValueError(msg)


def get_requires_for_dynamic_metadata(
    _settings: dict[str, object] | None = None,
) -> list[str]:
    return ["setuptools-scm"]
