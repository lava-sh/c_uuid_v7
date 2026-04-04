from __future__ import annotations

import shutil
import sys
import tarfile
import json
from urllib.request import urlopen
from pathlib import Path

HPY_VERSION = "0.9.0"
ROOT = Path(__file__).resolve().parent.parent
DOWNLOAD_DIR = ROOT / ".hpy-download"
SOURCE_DIR = ROOT / ".hpy-source"
EXTRACTED_ROOT = SOURCE_DIR / f"hpy-{HPY_VERSION}"
ARCHIVE = DOWNLOAD_DIR / f"hpy-{HPY_VERSION}.tar.gz"


def main() -> int:
    marker = EXTRACTED_ROOT / "hpy" / "devel" / "__init__.py"
    if marker.exists():
        return 0

    DOWNLOAD_DIR.mkdir(exist_ok=True)
    SOURCE_DIR.mkdir(exist_ok=True)

    archive = _download_sdist()

    if EXTRACTED_ROOT.exists():
        shutil.rmtree(EXTRACTED_ROOT)

    with tarfile.open(archive, "r:gz") as tar:
        tar.extractall(SOURCE_DIR)

    return 0


def _download_sdist() -> Path:
    if ARCHIVE.exists():
        return ARCHIVE

    with urlopen(f"https://pypi.org/pypi/hpy/{HPY_VERSION}/json") as response:
        payload = json.load(response)

    sdist_url = None
    for file in payload["urls"]:
        if file["packagetype"] == "sdist" and file["filename"].endswith(".tar.gz"):
            sdist_url = file["url"]
            break

    if sdist_url is None:
        raise RuntimeError(f"could not find HPy sdist URL for {HPY_VERSION}")

    with urlopen(sdist_url) as response, ARCHIVE.open("wb") as file:
        file.write(response.read())

    return ARCHIVE


if __name__ == "__main__":
    raise SystemExit(main())
