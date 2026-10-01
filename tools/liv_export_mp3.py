#!/usr/bin/env python3
"""Export every sound of an EvoMusicBox show as an MP3 — only the part selected for the button.

Usage:
  liv_export_mp3.py MyShow.liv                      # -> MyShow-mp3/<Name>.mp3 next to the show file
  liv_export_mp3.py MyShow.liv -o /path/out --numbered --by-category
  liv_export_mp3.py MyShow.evobox --music-only --bitrate 256k
  liv_export_mp3.py MyShow.liv --dry-run

A .liv show file is a zip holding project.json plus clips/<uid>.mp3 (shows saved by older versions:
clips/<uid>.wav): each clip IS the trimmed region of the button (trim, gain, normalize and fades
already rendered by the app), so the tool simply exports those clips. MP3 clips exported as MP3 are
copied as they are (no second lossy generation; --bitrate/--vbr are ignored unless --reencode is
given), everything else is encoded with ffmpeg. When a clip is missing from the show (never
rendered), the region is cut from the original source file if it still exists on this machine
(trim + gain + fades are applied with ffmpeg filters; "normalize" is not reproduced).

Requires the `ffmpeg` command line tool (with the libmp3lame encoder when something has to be
encoded) on the PATH or via --ffmpeg. Legacy <name>.evobox folders (or their project.json) are
accepted as input too.
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

PROJECT_FILE = "project.json"
FORMATS = {
    # ext: (codec args, container-specific extra args)
    "mp3": (["-codec:a", "libmp3lame"], ["-id3v2_version", "3"]),
    "m4a": (["-codec:a", "aac"], ["-movflags", "+faststart"]),
    "ogg": (["-codec:a", "libvorbis"], []),
    "flac": (["-codec:a", "flac"], []),
    "wav": (["-codec:a", "pcm_s16le"], []),
}


# ---------------------------------------------------------------- show loading
class Show:
    """project.json + a way to fetch clip bytes (zip entry or bundle folder file)."""

    def __init__(self, path):
        self.path = Path(path)
        self.zip = None
        self.bundle_dir = None
        if self.path.is_dir():
            self.bundle_dir = self.path
        elif self.path.name == PROJECT_FILE:
            self.bundle_dir = self.path.parent
        elif zipfile.is_zipfile(self.path):
            self.zip = zipfile.ZipFile(self.path)
        else:
            raise SystemExit(f"error: '{path}' is neither a .liv show file nor a project folder")
        try:
            if self.zip:
                data = self.zip.read(PROJECT_FILE)
            else:
                data = (self.bundle_dir / PROJECT_FILE).read_bytes()
        except (KeyError, OSError) as ex:
            raise SystemExit(f"error: cannot read {PROJECT_FILE} from '{path}': {ex}")
        self.project = json.loads(data)
        if self.project.get("app") != "EvoMusicBox":
            raise SystemExit(f"error: '{path}' is not an EvoMusicBox show")

    @property
    def show_file(self):
        """The .liv file or the bundle folder (project.json input -> its folder)."""
        return self.bundle_dir if self.bundle_dir else self.path

    @property
    def title(self):
        return self.show_file.stem

    def has_clip(self, rel):
        if not rel:
            return False
        if self.zip:
            return rel in self.zip.namelist()
        return (self.bundle_dir / rel).is_file()

    def extract_clip(self, rel, dest_dir):
        """Copies the clip to dest_dir and returns its path."""
        dest = Path(dest_dir) / Path(rel).name
        if self.zip:
            with self.zip.open(rel) as src, open(dest, "wb") as out:
                shutil.copyfileobj(src, out)
        else:
            shutil.copyfile(self.bundle_dir / rel, dest)
        return dest

    def resolve_source(self, source):
        """Original media file of a sound if it still exists (absolute path, or bundle-relative copy)."""
        rel = source.get("relPath") or ""
        if rel and self.bundle_dir and (self.bundle_dir / rel).is_file():
            return self.bundle_dir / rel
        p = source.get("path") or ""
        if p and Path(p).is_file():
            return Path(p)
        return None


def category_names(project):
    return {c.get("uid"): c.get("niceName", "") for c in project.get("categories", {}).get("items", [])}


def sound_info(project):
    """Sounds in soundboard order with the fields the exporter needs."""
    cats = category_names(project)
    out = []
    for s in project.get("sounds", {}).get("items", []):
        p = s.get("params", {})
        out.append({
            "uid": s.get("uid"),
            "name": s.get("niceName") or f"sound-{s.get('uid')}",
            "category": cats.get(s.get("categoryUid"), ""),
            "effect": bool(p.get("effect", False)),
            "clipFile": s.get("clipFile") or "",
            "trimStart": float(p.get("trimStart", 0.0)),
            "trimEnd": float(p.get("trimEnd", 0.0)),
            "gainDb": float(p.get("gain", 0.0)),
            "normalize": bool(p.get("normalize", False)),
            "fadeInMs": float(p.get("fadeIn", 0.0)),
            "fadeOutMs": float(p.get("fadeOut", 0.0)),
            "source": s.get("source", {}),
        })
    return out


# ---------------------------------------------------------------- naming
_BAD = re.compile(r'[<>:"/\\|?*\x00-\x1f]')


def safe_name(name):
    name = _BAD.sub("_", name).strip().strip(".")
    return name or "sound"


def unique_path(path):
    if not path.exists():
        return path
    for n in range(2, 10000):
        cand = path.with_name(f"{path.stem} ({n}){path.suffix}")
        if not cand.exists():
            return cand
    raise SystemExit(f"error: too many files named like {path}")


# ---------------------------------------------------------------- ffmpeg
def find_ffmpeg(explicit):
    exe = explicit or shutil.which("ffmpeg")
    if not exe or (not Path(exe).is_file() and not shutil.which(exe)):
        raise SystemExit("error: ffmpeg not found — install it or pass --ffmpeg /path/to/ffmpeg")
    return exe


def has_encoder(ffmpeg, name):
    try:
        out = subprocess.run([ffmpeg, "-hide_banner", "-encoders"], capture_output=True, text=True, check=False).stdout
    except OSError:
        return False
    return re.search(rf"^\s*A\S*\s+{re.escape(name)}\s", out, re.M) is not None


def encode(ffmpeg, src, dest, fmt, bitrate, vbr, metadata, cut=None, filters=None, copy=False):
    """cut = (start_seconds, duration_seconds) of the source to keep (None = whole file).
    copy = keep the compressed audio as it is (MP3 clip -> MP3 file), only rewriting the tags."""
    codec, extra = FORMATS[fmt]
    cmd = [ffmpeg, "-hide_banner", "-loglevel", "error", "-y", "-nostdin"]
    if cut:
        # input-side seek (accurate for audio: decodes from the previous keyframe and drops the
        # excess) + output duration; timestamps restart at 0 so the fade filters use 0-based times
        cmd += ["-ss", f"{cut[0]:.6f}"]
    if Path(src).suffix.lower() == ".mp3":
        # very short clips (a handful of MP3 frames) probe ambiguously — the app's clips are MP3
        cmd += ["-f", "mp3"]
    cmd += ["-i", str(src)]
    if cut:
        cmd += ["-t", f"{max(0.0, cut[1]):.6f}"]
    cmd += ["-vn", "-map_metadata", "-1"]
    if filters:
        cmd += ["-af", ",".join(filters)]
    if copy:
        cmd += ["-codec:a", "copy"]
    else:
        cmd += codec
        if fmt in ("mp3", "m4a", "ogg"):
            if vbr is not None and fmt != "m4a":
                cmd += ["-q:a", str(vbr)]
            else:
                cmd += ["-b:a", bitrate]
    for k, v in metadata.items():
        if v not in (None, ""):
            cmd += ["-metadata", f"{k}={v}"]
    cmd += extra + [str(dest)]
    r = subprocess.run(cmd, capture_output=True, text=True, check=False)
    if r.returncode != 0:
        return r.stderr.strip() or f"ffmpeg exited with {r.returncode}"
    return None


def fallback_filters(info):
    """Reproduce gain + fades of the app's renderer on a raw source cut (normalize is skipped)."""
    f = []
    if abs(info["gainDb"]) > 1e-6:
        f.append(f"volume={info['gainDb']:.3f}dB")
    dur = max(0.0, info["trimEnd"] - info["trimStart"])
    fi = info["fadeInMs"] / 1000.0
    fo = info["fadeOutMs"] / 1000.0
    if fi > 0:
        f.append(f"afade=t=in:st=0:d={min(fi, dur):.3f}")
    if fo > 0 and dur > 0:
        f.append(f"afade=t=out:st={max(0.0, dur - fo):.3f}:d={min(fo, dur):.3f}")
    return f


def fmt_time(sec):
    m = int(sec // 60)
    return f"{m}:{sec - m * 60:06.3f}"


# ---------------------------------------------------------------- main
def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter,
                                 epilog="\n".join(__doc__.split("\n\n")[1:]))
    ap.add_argument("show", help="MyShow.liv (or a legacy MyShow.evobox folder / its project.json)")
    ap.add_argument("-o", "--out", help="output folder (default: <show name>-mp3 next to the show file)")
    ap.add_argument("--format", choices=sorted(FORMATS), default="mp3", help="audio format (default mp3)")
    ap.add_argument("--bitrate", default="192k", help="constant bitrate for mp3/m4a/ogg (default 192k)")
    ap.add_argument("--vbr", type=int, help="mp3/ogg VBR quality instead of --bitrate (mp3: 0 best .. 9 worst)")
    ap.add_argument("--reencode", action="store_true",
                    help="re-encode MP3 clips (applies --bitrate/--vbr) instead of copying them unchanged")
    ap.add_argument("--numbered", action="store_true", help='prefix files with the tile order: "01 - Name.mp3"')
    ap.add_argument("--by-category", action="store_true", help="one sub-folder per category")
    ap.add_argument("--music-only", action="store_true", help="skip sounds flagged as effects")
    ap.add_argument("--effects-only", action="store_true", help="export only sounds flagged as effects")
    ap.add_argument("--category", action="append", default=[], metavar="NAME", help="only this category (repeatable)")
    ap.add_argument("--no-fallback", action="store_true", help="never cut from the original media when a clip is missing")
    ap.add_argument("--overwrite", action="store_true", help="replace existing files instead of adding (2), (3)...")
    ap.add_argument("--dry-run", action="store_true", help="list what would be written, do not encode")
    ap.add_argument("--ffmpeg", help="path to the ffmpeg executable")
    args = ap.parse_args()

    show = Show(args.show)
    sounds = sound_info(show.project)
    wanted_cats = {c.lower() for c in args.category}
    selected = set()
    for s in sounds:
        if args.music_only and s["effect"]:
            continue
        if args.effects_only and not s["effect"]:
            continue
        if wanted_cats and s["category"].lower() not in wanted_cats:
            continue
        selected.add(s["uid"])
    if not selected:
        print("nothing to export (no sound matches the filters)")
        return 0

    out_root = Path(args.out) if args.out else show.show_file.with_name(f"{show.title}-{args.format}")

    ffmpeg = None
    encoder_checked = False
    if not args.dry_run:
        ffmpeg = find_ffmpeg(args.ffmpeg)
        out_root.mkdir(parents=True, exist_ok=True)

    def need_encoder():
        # checked lazily: a show of MP3 clips exported as MP3 only copies streams
        nonlocal encoder_checked
        if encoder_checked:
            return
        codec_name = FORMATS[args.format][0][1]
        if not has_encoder(ffmpeg, codec_name):
            raise SystemExit(f"error: this ffmpeg has no '{codec_name}' encoder (needed for {args.format})")
        encoder_checked = True

    print(f"{show.title}: {len(selected)} of {len(sounds)} sound(s) -> {out_root}")
    ok = failed = skipped = 0
    with tempfile.TemporaryDirectory(prefix="liv-export-") as tmp:
        width = max(2, len(str(len(sounds))))
        for index, s in enumerate(sounds, start=1):
            if s["uid"] not in selected:
                continue
            base = safe_name(s["name"])
            if args.numbered:
                base = f"{index:0{width}d} - {base}"
            folder = out_root / safe_name(s["category"] or "Uncategorized") if args.by_category else out_root
            dest = folder / f"{base}.{args.format}"
            if not args.overwrite:
                dest = unique_path(dest)
            dur = max(0.0, s["trimEnd"] - s["trimStart"])
            region = f"{fmt_time(s['trimStart'])} - {fmt_time(s['trimEnd'])} ({fmt_time(dur)})"
            kind = "effect" if s["effect"] else "music"
            src_name = Path(s["source"].get("path") or "").name

            use_clip = show.has_clip(s["clipFile"])
            copy_clip = False
            src = None
            if use_clip:
                copy_clip = (args.format == "mp3" and Path(s["clipFile"]).suffix.lower() == ".mp3"
                             and not args.reencode)
                plan = "rendered clip" + (", copied as is" if copy_clip else "")
            else:
                src = None if args.no_fallback else show.resolve_source(s["source"])
                if src is None:
                    print(f"  SKIP  {s['name']!r}: no rendered clip in the show and the source is not available"
                          + ("" if args.no_fallback else f" ({src_name or 'no source path'})"))
                    skipped += 1
                    continue
                plan = f"cut from source {src.name}" + (" (normalize not reproduced)" if s["normalize"] else "")

            print(f"  {'DRY ' if args.dry_run else ''}{dest.relative_to(out_root)}  [{kind}, {region}, {plan}]")
            if args.dry_run:
                ok += 1
                continue
            folder.mkdir(parents=True, exist_ok=True)
            metadata = {
                "title": s["name"],
                "album": show.title,
                "genre": s["category"],
                "track": str(index),
                "comment": f"EvoMusicBox {kind}; {region}" + (f"; from {src_name}" if src_name else ""),
            }
            if use_clip:
                if not copy_clip:
                    need_encoder()
                clip = show.extract_clip(s["clipFile"], tmp)
                err = encode(ffmpeg, clip, dest, args.format, args.bitrate, args.vbr, metadata, copy=copy_clip)
                try:
                    os.remove(clip)
                except OSError:
                    pass
            else:
                need_encoder()
                err = encode(ffmpeg, src, dest, args.format, args.bitrate, args.vbr, metadata,
                             cut=(s["trimStart"], dur), filters=fallback_filters(s))
            if err:
                print(f"  FAIL  {s['name']!r}: {err}")
                failed += 1
            else:
                ok += 1

    verb = "would be written" if args.dry_run else "written"
    print(f"done: {ok} {verb}, {skipped} skipped, {failed} failed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
