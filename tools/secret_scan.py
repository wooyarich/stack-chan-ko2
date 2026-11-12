#!/usr/bin/env python3
"""API 키/비밀값 스캔 — 커밋·푸시 전에 반드시 실행한다.

사용법:
    python3 secret_scan.py /path/to/repo [--no-history]

종료 코드:
    0 = 깨끗함
    1 = 비밀값 발견 (커밋/푸시 금지)
    2 = 사용법 오류
"""
import os
import re
import subprocess
import sys

# (이름, 정규식)
PATTERNS = [
    ("OpenAI",      re.compile(r"\bsk-(?!ant-)[A-Za-z0-9_-]{20,}")),
    ("OpenAI-proj", re.compile(r"\bsk-proj-[A-Za-z0-9_-]{20,}")),
    ("Anthropic",   re.compile(r"\bsk-ant-[A-Za-z0-9_-]{20,}")),
    ("Google",      re.compile(r"\bAIza[0-9A-Za-z_-]{30,}")),
    ("AWS",         re.compile(r"\bAKIA[0-9A-Z]{16}\b")),
    ("GitHub",      re.compile(r"\bgh[pousr]_[A-Za-z0-9]{30,}")),
    ("Slack",       re.compile(r"\bxox[baprs]-[A-Za-z0-9-]{10,}")),
    ("JWT",         re.compile(r"\beyJ[A-Za-z0-9_-]{10,}\.[A-Za-z0-9_-]{10,}\.[A-Za-z0-9_-]{5,}")),
    ("PEM",         re.compile(r"-----BEGIN [A-Z ]*PRIVATE KEY-----")),
    # 일반적인 key=value 형태 (플레이스홀더는 아래에서 제외)
    ("generic",     re.compile(
        r"(?i)\b(api[_-]?key|apikey|secret[_-]?key|access[_-]?token|auth[_-]?token|password|passwd)\b"
        r"\s*[:=]\s*[\"']([^\"'\s]{12,})[\"']")),
]

# 실제 비밀이 아닌 값(템플릿/예시)
PLACEHOLDER = re.compile(
    r"^(?:\*+|x{4,}|X{4,}|\.{3,}|-+|<[^>]+>|your[-_].*|YOUR[-_].*|replace.*|REPLACE.*|"
    r"example.*|EXAMPLE.*|dummy.*|DUMMY.*|test.*|TEST.*|none|null|changeme|CHANGEME|"
    r"\$\{[^}]+\}|\$[A-Z_]+)$"
)

SKIP_DIRS = {".git", "node_modules", "__pycache__", ".venv", "venv", ".pio", "dist", "build"}
MAX_BYTES = 2_000_000


def is_placeholder(value: str) -> bool:
    v = value.strip()
    if PLACEHOLDER.match(v):
        return True
    # 전부 같은 문자이거나 별표 위주면 플레이스홀더
    if len(set(v)) <= 2:
        return True
    return False


def scan_text(text: str, label: str, out: list):
    for lineno, line in enumerate(text.splitlines(), 1):
        for name, rx in PATTERNS:
            m = rx.search(line)
            if not m:
                continue
            if name == "generic" and is_placeholder(m.group(2)):
                continue
            snippet = line.strip()
            if len(snippet) > 120:
                snippet = snippet[:117] + "..."
            out.append((label, lineno, name, snippet))
            break  # 한 줄에 한 번만 보고


def scan_worktree(root: str, out: list):
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in dirnames if d not in SKIP_DIRS]
        for fn in filenames:
            path = os.path.join(dirpath, fn)
            try:
                if os.path.getsize(path) > MAX_BYTES:
                    continue
                with open(path, "rb") as f:
                    raw = f.read()
                if b"\x00" in raw:          # 바이너리 제외
                    continue
                text = raw.decode("utf-8", "ignore")
            except OSError:
                continue
            scan_text(text, os.path.relpath(path, root), out)


def scan_history(root: str, out: list):
    try:
        revs = subprocess.run(["git", "rev-list", "--all"], cwd=root,
                              capture_output=True, text=True, timeout=120).stdout.split()
    except Exception:
        return
    if not revs:
        return
    names = "|".join(rx.pattern for _, rx in PATTERNS)
    for rev in revs:
        try:
            r = subprocess.run(["git", "grep", "-I", "-n", "-E", names, rev, "--"],
                               cwd=root, capture_output=True, text=True, timeout=180)
        except Exception:
            continue
        for line in r.stdout.splitlines():
            # <rev>:<path>:<line>:<content>
            parts = line.split(":", 3)
            if len(parts) < 4:
                continue
            rev_s, path, lineno, content = parts
            for name, rx in PATTERNS:
                m = rx.search(content)
                if not m:
                    continue
                if name == "generic" and is_placeholder(m.group(2)):
                    continue
                out.append((f"{path}@{rev_s[:8]}", int(lineno), name, content.strip()[:120]))
                break


def main() -> int:
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if not args:
        print(__doc__)
        return 2
    root = os.path.abspath(args[0])
    if not os.path.isdir(root):
        print(f"디렉터리 아님: {root}")
        return 2
    check_history = "--no-history" not in sys.argv

    findings = []
    scan_worktree(root, findings)
    wt_count = len(findings)
    if check_history:
        scan_history(root, findings)
    hist_count = len(findings) - wt_count

    print(f"== secret scan: {root}")
    print(f"   working tree: {wt_count}건 / history: {hist_count}건")
    for label, lineno, name, snippet in findings[:50]:
        print(f"  [{name}] {label}:{lineno}  {snippet}")
    if len(findings) > 50:
        print(f"  ... 외 {len(findings) - 50}건")

    if findings:
        print("\nRESULT: FAIL — 비밀값이 발견되었습니다. 커밋/푸시하지 마세요.")
        print("        실제 값은 저장소 밖(예: ~/tmp/secrets/)으로 옮기고 플레이스홀더로 바꾸세요.")
        return 1

    print("\nRESULT: PASS — 비밀값 0건")
    return 0


if __name__ == "__main__":
    sys.exit(main())
