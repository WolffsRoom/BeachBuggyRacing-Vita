"""Build the redistributable binary-delta bundle from the developer VitaFiles."""
from __future__ import annotations

import hashlib
import json
import shutil
import tempfile
import zipfile
from pathlib import Path

import bsdiff4


ROOT = Path(__file__).resolve().parents[1]
APK_DIR = ROOT / "APK"
DEV_ROOT = ROOT / "VitaFiles" / "beachbuggyracing"
PATCH_ROOT = ROOT / "patch_data"
MANIFEST = PATCH_ROOT / "manifest.json"


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def source_for(relative: str) -> str | None:
    if relative.endswith(".so") and "/" not in relative:
        return "lib/armeabi-v7a/" + relative
    return relative


def main() -> None:
    apks = sorted(APK_DIR.glob("*.apk"))
    if len(apks) != 1:
        raise SystemExit("Place exactly one reference APK in APK before building patch data.")
    if not DEV_ROOT.is_dir():
        raise SystemExit(f"Developer output not found: {DEV_ROOT}")

    apk = apks[0]
    if PATCH_ROOT.exists():
        shutil.rmtree(PATCH_ROOT)
    (PATCH_ROOT / "patches").mkdir(parents=True)

    records: list[dict[str, object]] = []
    with zipfile.ZipFile(apk) as archive, tempfile.TemporaryDirectory() as td:
        names = set(archive.namelist())
        temp = Path(td)
        for number, output in enumerate(sorted(DEV_ROOT.rglob("*"))):
            if not output.is_file():
                continue
            relative = output.relative_to(DEV_ROOT).as_posix()
            source = source_for(relative)
            original = archive.read(source) if source and source in names else b""
            finished = output.read_bytes()
            record: dict[str, object] = {
                "output": relative,
                "source": source,
                "size": len(finished),
                "sha256": sha256_bytes(finished),
            }
            if original == finished:
                record["mode"] = "copy"
            else:
                patch_name = f"{number:04d}.bsdiff"
                old_path, new_path = temp / "old", temp / "new"
                old_path.write_bytes(original)
                new_path.write_bytes(finished)
                bsdiff4.file_diff(str(old_path), str(new_path), str(PATCH_ROOT / "patches" / patch_name))
                record["mode"] = "patch"
                record["patch"] = patch_name
            records.append(record)

    manifest = {
        "format": 1,
        "game": "Beach Buggy Racing",
        "apk_name": apk.name,
        "apk_size": apk.stat().st_size,
        "apk_sha256": sha256_file(apk),
        "output_folder": "beachbuggyracing",
        "files": records,
    }
    MANIFEST.write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    patch_size = sum(p.stat().st_size for p in (PATCH_ROOT / "patches").glob("*"))
    print(f"Created {len(records)} records ({patch_size / 1024 / 1024:.1f} MiB of deltas).")


if __name__ == "__main__":
    main()
