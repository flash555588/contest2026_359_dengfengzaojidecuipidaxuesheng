#!/usr/bin/env python3
"""Validate README and authentic AI logs before creating the contest PR."""

from __future__ import annotations

import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
TEAM = "contest2026_359_dengfengzaojidecuipidaxuesheng"
LOGS_ROOT = ROOT / "logs"
ALLOWED_TOOLS = {"opencode", "claude-code", "codex", "kiro", "mimocode"}
REQUIRED_EVENT_FIELDS = {
    "schema_version",
    "session_id",
    "team_id",
    "github_login",
    "tool",
    "seq",
    "ts",
    "role",
}
ALLOWED_ROLES = {"user", "assistant", "tool", "system"}
LOG_PATH = re.compile(
    r"^logs/(?P<login>[A-Za-z0-9][A-Za-z0-9-]{0,38})/"
    r"\d{4}-\d{2}-\d{2}/(?P<tool>opencode|claude-code|codex|kiro|mimocode)__"
    r"(?P<session>.+)\.jsonl$"
)


class CheckResult:
    def __init__(self) -> None:
        self.errors: list[str] = []
        self.notices: list[str] = []

    def require(self, condition: bool, message: str) -> None:
        if not condition:
            self.errors.append(message)


def relative(path: Path) -> str:
    return path.relative_to(ROOT).as_posix()


def load_json(path: Path, result: CheckResult) -> object | None:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as exc:
        result.errors.append(f"invalid JSON {relative(path)}: {exc}")
        return None


def check_readme(result: CheckResult) -> None:
    readme = ROOT / "README.md"
    if not readme.is_file():
        result.errors.append("missing README.md")
        return
    text = readme.read_text(encoding="utf-8", errors="replace")
    for marker in ("## 适配亮点", "## 获取工程", "## 构建", "## 验证", "## AI Coding 使用说明"):
        result.require(marker in text, f"README.md missing section: {marker}")
    result.require(TEAM in text, "README.md missing the contest repository identifier")


def check_event_file(
    path: Path,
    login: str,
    session: dict[str, object],
    result: CheckResult,
) -> int:
    tool = session.get("tool")
    session_id = session.get("session_id")
    previous_seq: int | None = None
    count = 0
    digest = hashlib.sha256()

    try:
        with path.open("rb") as raw_file:
            raw_bytes = raw_file.read()
        digest.update(raw_bytes)
        lines = raw_bytes.decode("utf-8").splitlines()
    except (OSError, UnicodeDecodeError) as exc:
        result.errors.append(f"cannot read {relative(path)}: {exc}")
        return 0

    for line_number, line in enumerate(lines, start=1):
        if not line.strip():
            continue
        try:
            event = json.loads(line)
        except json.JSONDecodeError as exc:
            result.errors.append(f"invalid JSONL {relative(path)}:{line_number}: {exc}")
            continue
        if not isinstance(event, dict):
            result.errors.append(f"non-object event {relative(path)}:{line_number}")
            continue
        missing = REQUIRED_EVENT_FIELDS - event.keys()
        if missing:
            result.errors.append(
                f"event missing {sorted(missing)} at {relative(path)}:{line_number}"
            )
            continue
        if event.get("team_id") != TEAM:
            result.errors.append(f"team_id mismatch at {relative(path)}:{line_number}")
        if event.get("github_login") != login:
            result.errors.append(f"github_login mismatch at {relative(path)}:{line_number}")
        if event.get("tool") != tool:
            result.errors.append(f"tool mismatch at {relative(path)}:{line_number}")
        if event.get("session_id") != session_id:
            result.errors.append(f"session_id mismatch at {relative(path)}:{line_number}")
        if event.get("role") not in ALLOWED_ROLES:
            result.errors.append(f"invalid role at {relative(path)}:{line_number}")

        seq = event.get("seq")
        if not isinstance(seq, int):
            result.errors.append(f"non-integer seq at {relative(path)}:{line_number}")
        elif previous_seq is None and seq != 0:
            result.errors.append(f"first seq must be 0 in {relative(path)}")
        elif previous_seq is not None and seq != previous_seq + 1:
            result.errors.append(
                f"seq discontinuity in {relative(path)}:{line_number}: "
                f"expected {previous_seq + 1}, got {seq}"
            )
        previous_seq = seq if isinstance(seq, int) else previous_seq
        count += 1

    expected_count = session.get("event_count")
    if expected_count != count:
        result.errors.append(
            f"event_count mismatch for {relative(path)}: manifest={expected_count}, actual={count}"
        )
    expected_hash = session.get("sha256")
    if expected_hash and expected_hash != digest.hexdigest():
        result.errors.append(f"sha256 mismatch for {relative(path)}")
    return count


def check_logs(result: CheckResult) -> None:
    result.require(not (LOGS_ROOT / "your-github-login").exists(), "example AI logs must be removed")
    member_dirs = sorted(
        path for path in LOGS_ROOT.iterdir() if path.is_dir() and path.name != "your-github-login"
    )
    result.require(bool(member_dirs), "no AI log member directory found")

    actual_files = {
        relative(path)
        for path in LOGS_ROOT.glob("*/*/*.jsonl")
        if path.is_file()
    }
    declared_files: set[str] = set()
    total_events = 0

    for member_dir in member_dirs:
        login = member_dir.name
        manifest_path = member_dir / "manifest.json"
        if not manifest_path.is_file():
            result.errors.append(f"missing log manifest: {relative(manifest_path)}")
            continue
        manifest = load_json(manifest_path, result)
        if not isinstance(manifest, dict):
            continue
        result.require(manifest.get("team_id") == TEAM, f"team_id mismatch in {relative(manifest_path)}")
        result.require(
            manifest.get("github_login") == login,
            f"github_login mismatch in {relative(manifest_path)}",
        )
        sessions = manifest.get("sessions")
        if not isinstance(sessions, list):
            result.errors.append(f"sessions must be an array in {relative(manifest_path)}")
            continue

        seen_sessions: set[tuple[object, object]] = set()
        for index, session in enumerate(sessions):
            if not isinstance(session, dict):
                result.errors.append(f"invalid session entry {relative(manifest_path)}:{index}")
                continue
            tool = session.get("tool")
            session_id = session.get("session_id")
            file_path = session.get("file_path")
            result.require(tool in ALLOWED_TOOLS, f"unsupported tool in manifest: {tool}")
            result.require(isinstance(session_id, str) and bool(session_id), "empty session_id in manifest")
            result.require(isinstance(file_path, str), "invalid file_path in manifest")
            key = (tool, session_id)
            result.require(key not in seen_sessions, f"duplicate session in manifest: {tool}/{session_id}")
            seen_sessions.add(key)
            if not isinstance(file_path, str):
                continue
            match = LOG_PATH.fullmatch(file_path)
            result.require(match is not None, f"invalid AI log path: {file_path}")
            if match is None:
                continue
            result.require(match.group("login") == login, f"login/path mismatch: {file_path}")
            result.require(match.group("tool") == tool, f"tool/path mismatch: {file_path}")
            result.require(match.group("session") == session_id, f"session/path mismatch: {file_path}")
            path = (ROOT / file_path).resolve()
            try:
                path.relative_to(ROOT.resolve())
            except ValueError:
                result.errors.append(f"AI log path escapes repository: {file_path}")
                continue
            if not path.is_file():
                result.errors.append(f"missing declared AI log: {file_path}")
                continue
            declared_files.add(file_path)
            total_events += check_event_file(path, login, session, result)

    for orphan in sorted(actual_files - declared_files):
        result.errors.append(f"orphan AI log not declared in manifest: {orphan}")
    for missing in sorted(declared_files - actual_files):
        result.errors.append(f"manifest references missing AI log: {missing}")
    result.require(bool(declared_files), "no authentic AI sessions declared")
    result.notices.append(f"validated AI sessions: {len(declared_files)}, events: {total_events}")


def check_git_diff(result: CheckResult) -> None:
    completed = subprocess.run(
        ["git", "diff", "--check"],
        cwd=ROOT,
        text=True,
        capture_output=True,
        check=False,
    )
    if completed.returncode:
        result.errors.append("git diff --check failed: " + completed.stdout.strip())


def main() -> int:
    result = CheckResult()
    package = subprocess.run(
        [sys.executable, str(ROOT / "tools/check_package.py")],
        cwd=ROOT,
        text=True,
        capture_output=True,
        check=False,
    )
    if package.returncode:
        result.errors.append("package gate failed")
        if package.stdout.strip():
            result.notices.append(package.stdout.strip())

    check_readme(result)
    check_logs(result)
    check_git_diff(result)

    upstream_text = "\n".join(
        path.read_text(encoding="utf-8", errors="replace")
        for path in ROOT.glob("**/*.md")
        if path.stat().st_size < 2 * 1024 * 1024
    )
    if not re.search(r"https://github\.com/open-vela/(?:nuttx|apps)/pull/\d+", upstream_text):
        result.notices.append(
            "public NuttX/apps PR URLs are not present; they are outside this contest-repo-only submission"
        )

    for notice in result.notices:
        print(f"NOTICE: {notice}")
    if result.errors:
        print("SUBMISSION CHECK FAILED")
        for error in result.errors:
            print(f"- {error}")
        return 1

    print("PASS: README, package and authentic AI logs are ready for contest PR")
    return 0


if __name__ == "__main__":
    sys.exit(main())
