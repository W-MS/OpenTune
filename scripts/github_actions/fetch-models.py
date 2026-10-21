#!/usr/bin/env python3
"""Download the model files named in BUILDING.md into the runner workspace."""

import hashlib
import pathlib
import shutil
import sys
import tempfile
import time
import urllib.request
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
FCPE_URL = (
    "https://raw.githubusercontent.com/KCKT0112/HachiTune/"
    "d68808c37dede264d80d2d1050dce27af34ad654/Resources/models/fcpe.onnx"
)
FCPE_SHA256 = "013b507acd406de122b61ee497300ac6be082511101bc2136b886e48d9737e5d"
# BUILDING.md links the .zip, but that archive only contains the PyTorch checkpoint.
# The ONNX file with the documented name is in the sibling .oudep package from the
# same release. OpenVPI's release notes say to rename .oudep to .zip and unzip it.
HIFIGAN_URL = (
    "https://github.com/openvpi/vocoders/releases/download/"
    "pc-nsf-hifigan-44.1k-hop512-128bin-2025.02/"
    "pc_nsf_hifigan_44.1k_hop512_128bin_2025.02.oudep"
)
HIFIGAN_NAME = "pc_nsf_hifigan_44.1k_hop512_128bin_2025.02.onnx"
HIFIGAN_SHA256 = "ba7d43142d41f6900c8264b5662ca7125a50feb8760bb8b9615c61a8f5e6902e"
HIFIGAN_MODEL_SHA256 = "b297c0c790dbcfe6b2123d87cc20bf8718969f91ffa3170a198c8fb53686155d"
def download(url: str, dest: pathlib.Path) -> None:
    dest.parent.mkdir(parents=True, exist_ok=True)
    partial = dest.with_suffix(dest.suffix + ".partial")
    last_error = None
    for attempt in range(5):
        try:
            request = urllib.request.Request(url, headers={"User-Agent": "OpenTune-CI"})
            with urllib.request.urlopen(request, timeout=180) as response, partial.open("wb") as output:
                shutil.copyfileobj(response, output, length=1024 * 1024)
            partial.replace(dest)
            return
        except Exception as exc:
            last_error = exc
            partial.unlink(missing_ok=True)
            time.sleep(2 * (attempt + 1))
    raise SystemExit(f"Download failed {url}: {last_error}")


def sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def verify_sha256(path: pathlib.Path, expected: str, label: str) -> None:
    actual = sha256(path)
    if actual != expected:
        raise SystemExit(f"{label} SHA-256 mismatch: expected {expected}, got {actual}")


def fetch_fcpe() -> None:
    dest = ROOT / "models" / "fcpe.onnx"
    if dest.is_file() and sha256(dest) == FCPE_SHA256:
        print(f"FCPE already present and checksum matches: {dest}")
        return
    print(f"Downloading FCPE: {FCPE_URL}")
    download(FCPE_URL, dest)
    actual = sha256(dest)
    if actual != FCPE_SHA256:
        dest.unlink(missing_ok=True)
        raise SystemExit(f"FCPE SHA-256 mismatch: {actual}")
    print(f"FCPE checksum matches: {dest}")


def fetch_hifigan() -> None:
    dest = ROOT / "pc_nsf_hifigan_44.1k_ONNX" / HIFIGAN_NAME
    if dest.is_file() and sha256(dest) == HIFIGAN_MODEL_SHA256:
        print(f"HifiGAN already present and checksum matches: {dest}")
        return
    with tempfile.TemporaryDirectory() as temp_dir:
        archive_path = pathlib.Path(temp_dir) / "hifigan.zip"
        print(f"Downloading HifiGAN: {HIFIGAN_URL}")
        download(HIFIGAN_URL, archive_path)
        verify_sha256(archive_path, HIFIGAN_SHA256, "HifiGAN archive")
        with zipfile.ZipFile(archive_path) as archive:
            matches = [
                name
                for name in archive.namelist()
                if pathlib.PurePosixPath(name).name == HIFIGAN_NAME and not name.endswith("/")
            ]
            if len(matches) != 1:
                names = "\n".join(archive.namelist())
                raise SystemExit(
                    f"HifiGAN archive contains {HIFIGAN_NAME} {len(matches)} times. Archive entries:\n{names}"
                )
            dest.parent.mkdir(parents=True, exist_ok=True)
            with archive.open(matches[0]) as src, dest.open("wb") as output:
                shutil.copyfileobj(src, output)
    verify_sha256(dest, HIFIGAN_MODEL_SHA256, "HifiGAN model")
    print(f"HifiGAN written to: {dest}")


def main() -> None:
    fetch_fcpe()
    fetch_hifigan()


if __name__ == "__main__":
    main()
