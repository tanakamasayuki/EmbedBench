"""Shared pytest hooks for EmbedBench experiments."""

import shutil
from pathlib import Path


def pytest_runtest_setup(item):
    """Discard artefacts from the previous run of the same experiment."""
    output_dir = Path(item.fspath).parent / "output"
    if output_dir.exists():
        shutil.rmtree(output_dir)

