"""PlatformIO pre-build hook: the entity registry is regenerated, never trusted.

AGENTS.md forbids a manual "don't forget to regenerate" step for the web UI, and the
same argument applies here with more force: a stale registry does not fail to build, it
builds and ships a firmware whose entities disagree with pdo_table.py.
"""
import subprocess
import sys
from pathlib import Path

Import("env")  # noqa: F821  -- injected by PlatformIO/SCons

ROOT = Path(env.subst("$PROJECT_DIR"))  # noqa: F821
generator = ROOT / "tools" / "generate_registry.py"

result = subprocess.run([sys.executable, str(generator)], cwd=ROOT,
                        capture_output=True, text=True)
if result.returncode != 0:
    print(result.stdout)
    print(result.stderr, file=sys.stderr)
    raise SystemExit("registry generation failed")
print(result.stdout.strip())
