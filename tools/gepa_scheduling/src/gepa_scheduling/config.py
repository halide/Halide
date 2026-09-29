from __future__ import annotations

import dataclasses
import importlib.metadata
import shutil
import subprocess
import sys
import tomllib
from pathlib import Path
from typing import Any


@dataclasses.dataclass(frozen=True)
class HarnessConfig:
    repository: Path
    halide_dir: Path
    task_manifest: Path
    run_dir: Path
    copilot_model: str
    reflection_model: str
    copilot_executable: str = "copilot"
    max_metric_calls: int = 40
    max_workers: int = 1
    rollout_timeout_seconds: int = 900
    reflection_timeout_seconds: int = 300
    max_output_bytes: int = 1_048_576

    @classmethod
    def load(cls, path: Path) -> HarnessConfig:
        path = path.resolve()
        with path.open("rb") as file:
            values = tomllib.load(file)
        root = path.parent
        for key in ("repository", "halide_dir", "task_manifest", "run_dir"):
            values[key] = (root / values[key]).resolve()
        config = cls(**values)
        config._validate()
        return config

    def _validate(self) -> None:
        if not (self.repository / ".git").exists():
            raise ValueError(f"repository is not a git checkout: {self.repository}")
        if not self.task_manifest.is_file():
            raise ValueError(f"task manifest does not exist: {self.task_manifest}")
        if not (self.halide_dir / "HalideConfig.cmake").is_file():
            raise ValueError(f"Halide_DIR does not contain HalideConfig.cmake: {self.halide_dir}")
        if not self.copilot_model or not self.reflection_model:
            raise ValueError("Copilot model identifiers must be explicit and non-empty")
        if self.max_metric_calls < 1 or self.max_workers < 1:
            raise ValueError("metric-call and worker limits must be positive")
        if self.rollout_timeout_seconds < 1 or self.reflection_timeout_seconds < 1:
            raise ValueError("timeouts must be positive")
        if self.max_output_bytes < 1024:
            raise ValueError("max_output_bytes must be at least 1024")

    def versions(self) -> dict[str, Any]:
        copilot = shutil.which(self.copilot_executable)
        if copilot is None:
            copilot_version = None
        else:
            result = subprocess.run(
                [copilot, "--version"],
                check=False,
                capture_output=True,
                text=True,
                timeout=30,
            )
            copilot_version = (result.stdout or result.stderr).strip()
        git_result = subprocess.run(
            ["git", "status", "--short"],
            cwd=self.repository,
            check=False,
            capture_output=True,
            text=True,
            timeout=30,
        )
        sha_result = subprocess.run(
            ["git", "rev-parse", "HEAD"],
            cwd=self.repository,
            check=False,
            capture_output=True,
            text=True,
            timeout=30,
        )
        return {
            "config": {
                field.name: (
                    str(getattr(self, field.name))
                    if isinstance(getattr(self, field.name), Path)
                    else getattr(self, field.name)
                )
                for field in dataclasses.fields(self)
            },
            "copilot_version": copilot_version,
            "gepa_version": importlib.metadata.version("gepa"),
            "git_dirty": bool(git_result.stdout.strip()),
            "git_sha": sha_result.stdout.strip() or None,
            "python_version": sys.version,
        }
