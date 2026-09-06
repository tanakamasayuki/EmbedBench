"""Keep docs/FACTS.md true.

Other repositories quote these numbers. Before this test existed, three
outside quotations of this project's state were each wrong in at least
one figure — not because anyone was careless, but because there was no
single place that was kept correct. This measures the repository and
compares it with the table, so a stale number fails here instead of
travelling.
"""

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DOCS = ROOT / "docs"


def effective_loc(path: Path) -> int:
    return len([
        line for line in path.read_text().splitlines()
        if line.strip() and not line.strip().startswith("//")
    ])


def measured() -> dict:
    header = (ROOT / "src" / "embedbench_device.h").read_text()
    models = sorted((ROOT / "devices" / "src").glob("*_model.h"))
    model_lines = 0
    for model in models:
        for path in (model, model.with_suffix(".cpp")):
            if path.exists():
                model_lines += len(path.read_text().splitlines())
    experiments = [
        d for d in (ROOT / "tests").iterdir()
        if d.is_dir() and not d.name.startswith("common")
        and any(d.glob("test_*.py"))
    ]
    return {
        "version": int(re.search(r"kDeviceInterfaceVersion = (\d+)",
                                 header).group(1)),
        "revision": int(re.search(r"kDeviceInterfaceRevision = (\d+)",
                                  header).group(1)),
        "if_loc": effective_loc(ROOT / "src" / "embedbench_device.h"),
        "models": len(models),
        "model_lines": model_lines,
        "host_loc": (effective_loc(ROOT / "src" / "embedbench_host.h")
                     + effective_loc(ROOT / "src" / "embedbench_host.cpp")),
        "nenv_loc": (effective_loc(ROOT / "tests/common_env/nenv.h")
                     + effective_loc(ROOT / "tests/common_env/nenv.cpp")),
        "experiments": len(experiments),
        "presets": len(re.findall(
            r"extern const Image",
            (ROOT / "devices" / "src" / "sd_images.h").read_text())),
    }


def table_values(path: Path) -> list:
    body = path.read_text()
    block = body[body.index("<!-- FACTS BEGIN -->"):body.index("<!-- FACTS END -->")]
    rows = [line for line in block.splitlines()
            if line.startswith("|") and "---" not in line]
    return [int(row.rsplit("|", 2)[1].strip()) for row in rows[1:]]


def test_facts_are_current():
    m = measured()
    expected = [m["version"], m["revision"], m["if_loc"], m["models"],
                m["model_lines"], 2, m["host_loc"], m["nenv_loc"],
                m["experiments"], m["presets"]]
    for doc in ("FACTS.md", "FACTS.ja.md"):
        assert table_values(DOCS / doc) == expected, (
            f"docs/{doc} is out of date; measured {expected}")


def test_both_languages_agree():
    assert table_values(DOCS / "FACTS.md") == table_values(DOCS / "FACTS.ja.md")
