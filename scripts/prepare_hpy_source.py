import json
import shutil
import tarfile
from pathlib import Path
from urllib.parse import urlparse
from urllib.request import urlopen

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
        _extract_tarball(tar, SOURCE_DIR)

    return 0


def _download_sdist() -> Path:
    if ARCHIVE.exists():
        return ARCHIVE

    metadata_url = f"https://pypi.org/pypi/hpy/{HPY_VERSION}/json"
    _validate_url(metadata_url)

    with urlopen(metadata_url) as response:
        payload = json.load(response)

    sdist_url = None
    for file in payload["urls"]:
        if file["packagetype"] == "sdist" and file["filename"].endswith(".tar.gz"):
            sdist_url = file["url"]
            break

    if sdist_url is None:
        message = f"could not find HPy sdist URL for {HPY_VERSION}"
        raise RuntimeError(message)

    _validate_url(sdist_url)

    with urlopen(sdist_url) as response, ARCHIVE.open("wb") as file:  # noqa: S310
        file.write(response.read())

    return ARCHIVE


def _validate_url(url: str) -> None:
    parsed = urlparse(url)
    if parsed.scheme not in {"http", "https"}:
        message = f"unsupported URL scheme: {parsed.scheme or '<empty>'}"
        raise RuntimeError(message)


def _extract_tarball(archive: tarfile.TarFile, destination: Path) -> None:
    destination = destination.resolve()

    for member in archive.getmembers():
        member_path = (destination / member.name).resolve()
        if destination not in member_path.parents and member_path != destination:
            message = f"tar archive contains unsafe path: {member.name}"
            raise RuntimeError(message)

    archive.extractall(destination)  # noqa: S202


if __name__ == "__main__":
    raise SystemExit(main())
