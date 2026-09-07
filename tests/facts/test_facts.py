"""Keep docs/FACTS.md true.

Other repositories quote these numbers. Before this test existed, three
outside quotations of this project's state were each wrong in at least
one figure — not because anyone was careless, but because there was no
single place that was kept correct. This measures the repository and
compares it with the table, so a stale number fails here instead of
travelling.

What it does NOT pin is sizes. The first version counted source lines,
which move whenever a comment is edited: adding three comment lines to
three models turned a correct change into a red build. A figure that
cannot be cited without going stale within the day is not worth citing,
so the size questions are answered by the footprint script and by the
LOC pins that already own them, and this table carries only what changes
for a reason worth telling people about.
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
    experiments = [
        d for d in (ROOT / "tests").iterdir()
        if d.is_dir() and not d.name.startswith("common")
        and any(d.glob("test_*.py"))
    ]
    # Only figures that change for a reason worth telling people about.
    # Sizes deliberately left out: a line count moves when a comment is
    # edited, so pinning one makes an ordinary change fail the build.
    return {
        "version": int(re.search(r"kDeviceInterfaceVersion = (\d+)",
                                 header).group(1)),
        "revision": int(re.search(r"kDeviceInterfaceRevision = (\d+)",
                                  header).group(1)),
        "if_loc": effective_loc(ROOT / "src" / "embedbench_device.h"),
        "models": len(models),
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
    expected = [m["version"], m["revision"], m["if_loc"], m["models"], 2,
                m["experiments"], m["presets"]]
    for doc in ("FACTS.md", "FACTS.ja.md"):
        actual = table_values(DOCS / doc)
        assert actual == expected, (
            f"docs/{doc} is out of date.\n"
            f"  measured: {expected}\n"
            f"  in doc  : {actual}\n"
            "  The rows, in order: interface version, revision, interface "
            "effective LOC, models, environments, experiments, presets.")


def test_both_languages_agree():
    assert table_values(DOCS / "FACTS.md") == table_values(DOCS / "FACTS.ja.md")
