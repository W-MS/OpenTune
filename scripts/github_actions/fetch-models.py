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
GAME_URL = "https://github.com/openvpi/GAME/releases/download/v1.0.3/GAME-1.0.3-small-onnx.zip"
GAME_FILES = ("encoder.onnx", "segmenter.onnx", "estimator.onnx", "bd2dur.onnx", "config.json")


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
    raise SystemExit(f"下载失败 {url}: {last_error}")


def sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def fetch_fcpe() -> None:
    dest = ROOT / "models" / "fcpe.onnx"
    if dest.is_file() and sha256(dest) == FCPE_SHA256:
        print(f"FCPE 已存在且校验通过: {dest}")
        return
    print(f"下载 FCPE: {FCPE_URL}")
    download(FCPE_URL, dest)
    actual = sha256(dest)
    if actual != FCPE_SHA256:
        dest.unlink(missing_ok=True)
        raise SystemExit(f"FCPE SHA-256 不符: {actual}")
    print(f"FCPE 校验通过: {dest}")


def fetch_hifigan() -> None:
    dest = ROOT / "pc_nsf_hifigan_44.1k_ONNX" / HIFIGAN_NAME
    if dest.is_file():
        print(f"HifiGAN 已存在: {dest}")
        return
    with tempfile.TemporaryDirectory() as temp_dir:
        archive_path = pathlib.Path(temp_dir) / "hifigan.zip"
        print(f"下载 HifiGAN: {HIFIGAN_URL}")
        download(HIFIGAN_URL, archive_path)
        with zipfile.ZipFile(archive_path) as archive:
            matches = [
                name
                for name in archive.namelist()
                if pathlib.PurePosixPath(name).name == HIFIGAN_NAME and not name.endswith("/")
            ]
            if len(matches) != 1:
                names = "\n".join(archive.namelist())
                raise SystemExit(
                    f"HifiGAN 包中 {HIFIGAN_NAME} 出现 {len(matches)} 次。包内文件:\n{names}"
                )
            dest.parent.mkdir(parents=True, exist_ok=True)
            with archive.open(matches[0]) as src, dest.open("wb") as output:
                shutil.copyfileobj(src, output)
    print(f"HifiGAN 已放到: {dest}")


def fetch_game() -> None:
    dest = ROOT / "models" / "GAME"
    if all((dest / name).is_file() for name in GAME_FILES):
        print(f"GAME 已存在: {dest}")
        return
    with tempfile.TemporaryDirectory() as temp_dir:
        archive_path = pathlib.Path(temp_dir) / "game.zip"
        print(f"下载 GAME: {GAME_URL}")
        download(GAME_URL, archive_path)
        with zipfile.ZipFile(archive_path) as archive:
            names = [name for name in archive.namelist() if not name.endswith("/")]
            encoders = [name for name in names if pathlib.PurePosixPath(name).name == "encoder.onnx"]
            if len(encoders) != 1:
                raise SystemExit(f"GAME 压缩包中 encoder.onnx 出现 {len(encoders)} 次")
            prefix = pathlib.PurePosixPath(encoders[0]).parent.as_posix()
            dest.mkdir(parents=True, exist_ok=True)
            copied = []
            for name in names:
                path = pathlib.PurePosixPath(name)
                if path.parent.as_posix() != prefix:
                    continue
                with archive.open(name) as src, (dest / path.name).open("wb") as output:
                    shutil.copyfileobj(src, output)
                copied.append(path.name)
    missing = [name for name in GAME_FILES if name not in copied]
    if missing:
        raise SystemExit("GAME 模型目录缺少: " + ", ".join(missing))
    print(f"GAME 已放到: {dest}")


def main() -> None:
    fetch_fcpe()
    fetch_hifigan()
    fetch_game()


if __name__ == "__main__":
    main()
