"""Content-addressed cache of tool runs and comparisons for parity.py check.

A tool run is keyed by everything that can change what the tool writes: the tool binary (and
every other file next to it, e.g. hlbsp's entities.void), its arguments, the map name, and the
content of every file in the directory it runs in (plus the corpus WADs for hlcsg). At -threads 1
the tools are deterministic (checked when the oracle was set up, and by `check --no-cache`, which
reruns everything and fails if a run differs from its cache entry), so a run with a known key
need not be repeated: its outputs are taken from the cache.

.log/.err files are keyed by their normalized content (no timings, dates or argv[0] path). That is
sound because the tools only append to the .log and delete the .err; none reads them.

A comparison with the oracle is keyed by both sides' file keys, the exit codes, the padding mode
and the harness source, so an unchanged candidate output is not compared again either.

Layout under the cache root:
    blobs/<sha[:2]>/<sha>   raw file contents, by SHA-256
    runs/<key>.json         {"result": {...}, "outputs": manifest, "console": manifest}
    compares/<key>.json     {"problems": [...]}
A manifest maps each file name of a (flat) directory to [key digest, blob sha].
"""

import hashlib
import json
import os
import shutil
import tempfile
from pathlib import Path

VERSION = 1  # bump when the meaning of a key changes (e.g. how stages are run)

NORMALIZED_SUFFIXES = (".log", ".err")


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def digest(obj) -> str:
    """Stable digest of a JSON-serializable value."""
    return sha256_bytes(json.dumps(obj, sort_keys=True, separators=(",", ":")).encode())


def manifest_digest(manifest: dict | None) -> str | None:
    return None if manifest is None else digest(sorted((name, entry[0]) for name, entry in manifest.items()))


def _write_atomic(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, tmp = tempfile.mkstemp(dir=path.parent, prefix=".tmp-")
    try:
        with os.fdopen(fd, "wb") as f:
            f.write(data)
        os.replace(tmp, path)
    except BaseException:
        os.unlink(tmp)
        raise


class StageCache:
    def __init__(self, root: Path, normalize):
        self.root = Path(root)
        self.normalize = normalize  # bytes -> bytes, for .log/.err

    # --- files and manifests

    def file_entry(self, path: Path, store: bool) -> list[str]:
        data = path.read_bytes()
        blob = sha256_bytes(data)
        key = sha256_bytes(self.normalize(data)) if path.name.endswith(NORMALIZED_SUFFIXES) else blob
        if store:
            target = self.root / "blobs" / blob[:2] / blob
            if not target.exists():
                _write_atomic(target, data)
        return [key, blob]

    def manifest(self, directory: Path, store: bool = False) -> dict:
        return {f.name: self.file_entry(f, store) for f in sorted(directory.iterdir()) if f.is_file()}

    def materialize(self, manifest: dict, dest: Path) -> None:
        """dest becomes a fresh directory holding exactly the manifest's files (copies: the tools
        rewrite files in place, which must not reach the blobs)."""
        if dest.exists():
            shutil.rmtree(dest)
        dest.mkdir(parents=True)
        for name, (_, blob) in manifest.items():
            shutil.copyfile(self.root / "blobs" / blob[:2] / blob, dest / name)

    # --- entries

    def _get(self, kind: str, key: str) -> dict | None:
        try:
            return json.loads((self.root / kind / f"{key}.json").read_text())
        except (FileNotFoundError, json.JSONDecodeError):
            return None

    def _put(self, kind: str, key: str, value: dict) -> None:
        _write_atomic(self.root / kind / f"{key}.json", json.dumps(value, sort_keys=True).encode())

    def get_run(self, key: str) -> dict | None:
        return self._get("runs", key)

    def put_run(self, key: str, value: dict) -> None:
        self._put("runs", key, value)

    def get_compare(self, key: str) -> dict | None:
        return self._get("compares", key)

    def put_compare(self, key: str, value: dict) -> None:
        self._put("compares", key, value)

    # --- maintenance

    def stats(self) -> dict:
        out = {}
        for kind in ("runs", "compares", "blobs"):
            files = [f for f in (self.root / kind).rglob("*") if f.is_file()] if (self.root / kind).exists() else []
            out[kind] = (len(files), sum(f.stat().st_size for f in files))
        return out

    def clear(self) -> None:
        shutil.rmtree(self.root, ignore_errors=True)
