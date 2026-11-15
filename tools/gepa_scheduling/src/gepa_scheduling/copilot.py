from __future__ import annotations

import json
import os
import subprocess
import time
from pathlib import Path
from typing import Any

from gepa_scheduling.models import RolloutResult


class CopilotBackend:
    def __init__(
        self,
        executable: str,
        model: str,
        *,
        timeout_seconds: int,
        max_output_bytes: int,
    ) -> None:
        self.executable = executable
        self.model = model
        self.timeout_seconds = timeout_seconds
        self.max_output_bytes = max_output_bytes

    def run_agent(self, prompt: str, cwd: Path) -> RolloutResult:
        command = [
            self.executable,
            "-p",
            prompt,
            "--model",
            self.model,
            "--output-format",
            "json",
            "--no-ask-user",
            "--available-tools",
            "read",
            "write",
            "shell",
        ]
        started = time.monotonic()
        try:
            result = subprocess.run(
                command,
                cwd=cwd,
                env=_copilot_environment(),
                check=False,
                capture_output=True,
                text=True,
                timeout=self.timeout_seconds,
            )
        except (OSError, subprocess.TimeoutExpired) as error:
            return RolloutResult(
                returncode=-1,
                stdout="",
                stderr="",
                duration_seconds=time.monotonic() - started,
                error=str(error),
            )
        stdout = _truncate(result.stdout, self.max_output_bytes)
        stderr = _truncate(result.stderr, self.max_output_bytes)
        input_tokens, output_tokens = parse_token_usage(stdout)
        return RolloutResult(
            returncode=result.returncode,
            stdout=stdout,
            stderr=stderr,
            duration_seconds=time.monotonic() - started,
            input_tokens=input_tokens,
            output_tokens=output_tokens,
            error=None if result.returncode == 0 else f"Copilot exited with {result.returncode}",
        )

    def complete(self, prompt: str | list[dict[str, Any]], cwd: Path) -> str:
        if not isinstance(prompt, str):
            prompt = "\n\n".join(f"{message.get('role', 'user')}: {message.get('content', '')}" for message in prompt)
        command = [
            self.executable,
            "-p",
            prompt,
            "-s",
            "--model",
            self.model,
            "--no-ask-user",
            "--available-tools",
            "read",
        ]
        try:
            result = subprocess.run(
                command,
                cwd=cwd,
                env=_copilot_environment(),
                check=False,
                capture_output=True,
                text=True,
                timeout=self.timeout_seconds,
            )
        except (OSError, subprocess.TimeoutExpired) as error:
            raise RuntimeError(f"Copilot reflection failed: {error}") from error
        if result.returncode != 0:
            raise RuntimeError(f"Copilot reflection exited with {result.returncode}: {result.stderr[-4000:]}")
        return result.stdout.strip()


class FakeCopilotBackend:
    def __init__(
        self,
        files: dict[str, str] | None = None,
        *,
        response: str = "fake response",
        require_sealed: bool = False,
    ) -> None:
        self.files = files or {}
        self.response = response
        self.require_sealed = require_sealed

    def run_agent(self, prompt: str, cwd: Path) -> RolloutResult:
        del prompt
        if self.require_sealed and ((cwd / ".git").exists() or (cwd / "tools/gepa_scheduling").exists()):
            raise RuntimeError("rollout workspace exposes git metadata or hidden harness data")
        for relative, content in self.files.items():
            path = cwd / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content)
        return RolloutResult(
            returncode=0,
            stdout=self.response,
            stderr="",
            duration_seconds=0.01,
            input_tokens=10,
            output_tokens=10,
        )

    def complete(self, prompt: str | list[dict[str, Any]], cwd: Path) -> str:
        del prompt, cwd
        return self.response


def parse_token_usage(json_lines: str) -> tuple[int | None, int | None]:
    input_tokens = 0
    output_tokens = 0
    found = False
    for line in json_lines.splitlines():
        try:
            value = json.loads(line)
        except json.JSONDecodeError:
            continue
        for key, item in _walk(value):
            normalized = key.lower()
            if normalized in {"input_tokens", "prompt_tokens"} and isinstance(item, int):
                input_tokens += item
                found = True
            elif normalized in {"output_tokens", "completion_tokens"} and isinstance(item, int):
                output_tokens += item
                found = True
    return (input_tokens, output_tokens) if found else (None, None)


def _walk(value: Any):
    if isinstance(value, dict):
        for key, item in value.items():
            yield key, item
            yield from _walk(item)
    elif isinstance(value, list):
        for item in value:
            yield from _walk(item)


def _truncate(value: str, max_bytes: int) -> str:
    encoded = value.encode()
    if len(encoded) <= max_bytes:
        return value
    return encoded[-max_bytes:].decode(errors="replace")


def _copilot_environment() -> dict[str, str]:
    environment = os.environ.copy()
    environment["COPILOT_AUTO_UPDATE"] = "false"
    return environment
