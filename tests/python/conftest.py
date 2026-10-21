"""Loads the table and the generator on a bare interpreter.

No PlatformIO, no ESP-IDF, no firmware build -- that is the whole point of testing this
layer here (AGENTS.md). If a test in this directory ever needs any of those,
the thing it is testing has moved to the wrong layer.
"""
import importlib.util
import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[2] / "tools"


def _load(name: str):
    spec = importlib.util.spec_from_file_location(name, TOOLS / f"{name}.py")
    module = importlib.util.module_from_spec(spec)
    # dataclasses resolves string annotations through sys.modules, so the module has to be
    # registered before it is executed or `from __future__ import annotations` blows up.
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


def pytest_configure():
    pass
