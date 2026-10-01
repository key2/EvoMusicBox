#!/usr/bin/env python3
"""Build an EvoMusicBox show (.liv) from a sound pack downloaded with pixabay_sfx_download.py.

Usage:
  soundpack_to_liv.py soundpacks/pixabay-crowd-reaction                  # -> soundpacks/pixabay-crowd-reaction.liv
  soundpack_to_liv.py soundpacks/pixabay-crowd-reaction -o shows/Crowd.liv --music
  soundpack_to_liv.py soundpacks/pixabay-crowd-reaction --limit 20 --dry-run

The pack's index.json (one entry per sound with its category, mp3 and icon) becomes a project:
  - one category per distinct pack category (title-cased, with a matching Phosphor icon and colour)
  - one sound tile per entry: the pack icon as picture sticker, the whole file as clip (trim
    0..duration, no gain/fades), coloured like its category, marked as effect (stacks, does not stop
    the music) unless --music
  - the mp3 itself is stored as the rendered clip (clips/<uid>.mp3): with default trim/gain the
    rendered clip IS the source, so nothing is re-encoded and the show plays without the pack.
    source.path still points at the pack file so re-rendering after a trim edit works on this machine.

The .liv is a zip holding project.json + clips/ + icons/ exactly like the app writes it (picture
stickers are content-addressed icons/<sha1-16>.png; JPEG artwork is converted to PNG). Requires the
ffmpeg and ffprobe command line tools (icon conversion, exact decoded clip durations).
"""
import argparse
import hashlib
import json
import os
import shutil
import struct
import subprocess
import sys
import zipfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

FORMAT_VERSION = 2
TILE_SIZE = [160.0, 70.0]
DEFAULT_STICKER = "ph:speaker-high"
# pack category -> (Phosphor icon, colour); colours in the range of the app's default categories
CATEGORY_STYLE = {
    "people": ("ph:users-three", (0.85, 0.35, 0.55)),
    "film & special effects": ("ph:film-slate", (0.95, 0.62, 0.25)),
    "city": ("ph:buildings", (0.62, 0.55, 0.90)),
    "musical": ("ph:music-notes", (0.36, 0.56, 0.95)),
    "nature": ("ph:tree", (0.30, 0.72, 0.62)),
    "horror": ("ph:ghost", (0.60, 0.30, 0.65)),
    "household": ("ph:house", (0.80, 0.58, 0.38)),
    "uncategorized": ("ph:folder", (0.55, 0.60, 0.68)),
}
EXTRA_PALETTE = [(0.95, 0.45, 0.35), (0.35, 0.65, 0.90), (0.75, 0.70, 0.30), (0.45, 0.75, 0.45), (0.70, 0.45, 0.85)]


# ---------------------------------------------------------------- helpers

def find_tool(explicit, name):
    path = explicit or shutil.which(name)
    if not path or not os.path.exists(shutil.which(path) or path):
        raise SystemExit(f"error: {name} not found (install it or pass --{name} PATH)")
    return path


def content_hash(path):
    """MediaRef.contentHash as util/Hash.cpp computes it: FNV-1a 64 over the file size (as a
    little-endian long long), the first MiB and - for bigger files - the last MiB."""
    size = os.path.getsize(path)
    window = 1024 * 1024
    h = 1469598103934665603  # the seed util/Hash.h uses (not the textbook FNV offset basis)
    mask = (1 << 64) - 1

    def fnv(data, h):
        for b in data:
            h = ((h ^ b) * 0x100000001B3) & mask
        return h

    h = fnv(struct.pack("<q", size), h)
    with open(path, "rb") as f:
        h = fnv(f.read(min(window, size)), h)
        if size > window:
            f.seek(size - window)
            h = fnv(f.read(window), h)
    return f"{h:016x}"


def probe_decoded(ffprobe, path):
    """Exact decoded length (what the app's importer stores as trim end), sample rate and channels."""
    cmd = [ffprobe, "-v", "error", "-select_streams", "a:0",
           "-show_entries", "stream=sample_rate,channels:frame=nb_samples", "-of", "json", str(path)]
    out = subprocess.run(cmd, capture_output=True, text=True, timeout=300, check=False).stdout
    info = json.loads(out or "{}")
    stream = (info.get("streams") or [{}])[0]
    rate = int(stream.get("sample_rate") or 0)
    channels = int(stream.get("channels") or 0)
    samples = sum(int(f.get("nb_samples") or 0) for f in info.get("frames") or [])
    if rate <= 0 or samples <= 0:
        raise RuntimeError(f"ffprobe could not decode {path}")
    return {"duration": samples / rate, "sample_rate": rate, "channels": 2 if channels >= 2 else 1}


def icon_png_bytes(ffmpeg, path, tmp_dir):
    """PNG bytes of an icon file (converted with ffmpeg when it is not a PNG already)."""
    data = path.read_bytes()
    if data.startswith(b"\x89PNG"):
        return data
    tmp = tmp_dir / (path.stem + ".png")
    cmd = [ffmpeg, "-y", "-v", "error", "-nostdin", "-i", str(path), "-frames:v", "1", str(tmp)]
    result = subprocess.run(cmd, capture_output=True, text=True, timeout=120, check=False)
    if result.returncode != 0 or not tmp.exists():
        raise RuntimeError(f"cannot convert {path.name} to PNG: {result.stderr.strip()}")
    data = tmp.read_bytes()
    tmp.unlink()
    return data


def display_name(category):
    return " ".join(w.capitalize() if w != "&" else w for w in category.split()) or "Uncategorized"


def category_style(category, index):
    if category in CATEGORY_STYLE:
        return CATEGORY_STYLE[category]
    return "ph:folder", EXTRA_PALETTE[index % len(EXTRA_PALETTE)]


def base_item(type_name, uid, name, color, params):
    return {
        "type": type_name, "uid": uid, "niceName": name, "locked": False, "mini": False,
        "params": dict(params, color=[color[0], color[1], color[2], 1.0], enabled=True),
        "viewPos": [0.0, 0.0], "viewSize": list(TILE_SIZE),
    }


def category_item(uid, category, index):
    icon, color = category_style(category, index)
    item = base_item("Category", uid, display_name(category), color, {"icon": icon})
    item["builtin"] = False
    return item


def sound_item(uid, entry, category_uid, color, sticker, clip_file, source_path, media, effect):
    params = {
        "sticker": sticker, "effect": effect, "trimStart": 0.0, "trimEnd": round(media["duration"], 6),
        "gain": 0.0, "normalize": False, "fadeIn": 0.0, "fadeOut": 0.0, "hotkey": "",
    }
    item = base_item("Sound", uid, entry.get("name") or entry.get("slug") or f"Sound {uid}", color, params)
    item["categoryUid"] = category_uid
    item["clipFile"] = clip_file
    item["source"] = {
        "path": str(source_path), "relPath": "", "sizeBytes": source_path.stat().st_size,
        "contentHash": content_hash(source_path), "durationSec": round(media["duration"], 6),
        "sampleRate": media["sample_rate"], "channels": media["channels"],
        "container": "mp3", "codec": "mp3float", "hasVideo": False,
    }
    return item


# ---------------------------------------------------------------- main

def parse_args():
    parser = argparse.ArgumentParser(
        description=__doc__.split("\n\n")[0],
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="\n\n".join(__doc__.split("\n\n")[1:]))
    parser.add_argument("pack", help="sound pack directory holding index.json, audio/ and icons/")
    parser.add_argument("-o", "--out", metavar="FILE", help="show file to write (default: <pack>.liv next to the pack)")
    parser.add_argument("--music", action="store_true", help="mark the sounds as music (exclusive) instead of effects")
    parser.add_argument("--limit", type=int, default=0, metavar="N", help="only the first N sounds (testing)")
    parser.add_argument("-j", "--jobs", type=int, default=4, metavar="N", help="parallel ffprobe runs (default: %(default)s)")
    parser.add_argument("--ffmpeg", metavar="PATH", help="ffmpeg binary (default: from PATH)")
    parser.add_argument("--ffprobe", metavar="PATH", help="ffprobe binary (default: from PATH)")
    parser.add_argument("--overwrite", action="store_true", help="replace an existing show file")
    parser.add_argument("--dry-run", action="store_true", help="print the plan, write nothing")
    return parser.parse_args()


def main():
    args = parse_args()
    pack = Path(args.pack).expanduser().resolve()
    index_path = pack / "index.json"
    if not index_path.is_file():
        raise SystemExit(f"error: {index_path} not found (is this a pack made by pixabay_sfx_download.py?)")
    out = Path(args.out).expanduser() if args.out else pack.with_name(pack.name + ".liv")
    if out.suffix.lower() != ".liv":
        out = out.with_name(out.name + ".liv")
    if out.exists() and not args.overwrite and not args.dry_run:
        raise SystemExit(f"error: {out} exists (use --overwrite)")

    index = json.loads(index_path.read_text(encoding="utf-8"))
    entries = [e for e in index.get("sounds") or [] if (e.get("audio") or {}).get("file")]
    if args.limit:
        entries = entries[:args.limit]
    if not entries:
        raise SystemExit("error: the pack index lists no sounds")
    missing = [e["slug"] for e in entries if not (pack / e["audio"]["file"]).is_file()]
    if missing:
        raise SystemExit(f"error: {len(missing)} audio files listed in the index are missing, e.g. {missing[0]}")

    # -- categories: the pack's, most populated first
    counts = {}
    for e in entries:
        cat = (e.get("category") or "uncategorized").strip().lower()
        counts[cat] = counts.get(cat, 0) + 1
    order = sorted(counts, key=lambda c: (-counts[c], c))
    categories = {cat: category_item(i + 1, cat, i) for i, cat in enumerate(order)}
    for cat in order:
        print(f"category {categories[cat]['uid']:2d}  {display_name(cat):<24} {counts[cat]:4d} sounds  {categories[cat]['params']['icon']}")
    print(f"{len(entries)} sounds -> {out}" + (" (dry run)" if args.dry_run else ""))
    if args.dry_run:
        for i, e in enumerate(entries[:20], 1):
            print(f"  {i:4d}  {e.get('name')!r:<50} [{display_name(e.get('category') or 'uncategorized')}]  {e['audio']['file']}")
        if len(entries) > 20:
            print(f"  ... {len(entries) - 20} more")
        return 0

    ffmpeg = find_tool(args.ffmpeg, "ffmpeg")
    ffprobe = find_tool(args.ffprobe, "ffprobe")
    out.parent.mkdir(parents=True, exist_ok=True)
    tmp_dir = out.parent / (out.name + ".tmp.d")
    tmp_dir.mkdir(exist_ok=True)
    try:
        # -- exact clip lengths (parallel ffprobe)
        print(f"probing {len(entries)} files with ffprobe...", flush=True)
        with ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
            media = list(pool.map(lambda e: probe_decoded(ffprobe, pack / e["audio"]["file"]), entries))

        # -- icons: content-addressed PNGs, identical artwork shared
        icons = {}          # icon name -> png bytes
        stickers = []       # per entry
        converted = 0
        for e in entries:
            icon = e.get("icon") or {}
            path = pack / icon["file"] if icon.get("file") else None
            if path is None or not path.is_file():
                stickers.append(DEFAULT_STICKER)
                continue
            data = icon_png_bytes(ffmpeg, path, tmp_dir)
            converted += path.suffix.lower() != ".png"
            name = hashlib.sha1(data).hexdigest()[:16] + ".png"
            icons.setdefault(name, data)
            stickers.append(f"img:icons/{name}")

        # -- sounds
        sounds = []
        for i, e in enumerate(entries):
            uid = i + 1
            cat = categories[(e.get("category") or "uncategorized").strip().lower()]
            color = tuple(cat["params"]["color"][:3])
            sounds.append(sound_item(uid, e, cat["uid"], color, stickers[i], f"clips/{uid:06d}.mp3",
                                     pack / e["audio"]["file"], media[i], not args.music))

        project = {
            "app": "EvoMusicBox",
            "formatVersion": FORMAT_VERSION,
            "categories": {"items": [categories[c] for c in order], "nextUid": len(order) + 1},
            "sounds": {"items": sounds, "nextUid": len(sounds) + 1},
        }

        # -- pack the show: project.json deflated, clips and icons stored (already compressed)
        tmp_zip = out.with_name(out.name + ".tmp")
        with zipfile.ZipFile(tmp_zip, "w", allowZip64=True) as zf:
            zf.writestr("project.json", json.dumps(project, indent=2, ensure_ascii=False), compress_type=zipfile.ZIP_DEFLATED)
            for s in sounds:
                zf.write(s["source"]["path"], s["clipFile"], compress_type=zipfile.ZIP_STORED)
            for name, data in icons.items():
                zf.writestr(f"icons/{name}", data, compress_type=zipfile.ZIP_STORED)
        os.replace(tmp_zip, out)
    finally:
        shutil.rmtree(tmp_dir, ignore_errors=True)

    total = sum(m["duration"] for m in media)
    print(f"done: {out} ({out.stat().st_size / 1e6:.1f} MB) - {len(sounds)} sounds in {len(order)} categories, "
          f"{len(icons)} icon files ({converted} converted from JPEG), {total / 60:.1f} min of audio")
    return 0


if __name__ == "__main__":
    sys.exit(main())
