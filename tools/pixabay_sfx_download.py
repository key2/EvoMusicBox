#!/usr/bin/env python3
"""Download royalty-free sound effects (mp3 + icon) from a Pixabay search and build a JSON index.

Usage:
  pixabay_sfx_download.py                                  # "crowd reaction", 0-30 s -> soundpacks/pixabay-crowd-reaction/
  pixabay_sfx_download.py -q "crowd reaction" --duration 0-30 -o soundpacks/crowd
  pixabay_sfx_download.py -q applause --limit 50 --jobs 6
  pixabay_sfx_download.py --catalog-only                   # only enumerate the search (writes catalog.json)
  pixabay_sfx_download.py --index-only                     # rebuild index.json from catalog.json + files on disk
  pixabay_sfx_download.py --dry-run

Pixabay has no public API for sound effects (the documented /api/ endpoint only covers images and
videos, /api/audio/ is restricted), and the website sits behind a Cloudflare challenge that plain HTTP
clients cannot pass. The tool therefore drives a local headless Chrome/Chromium over the DevTools
protocol pipe (--remote-debugging-pipe, standard library only) to read the search result pages: every
page exposes a bootstrap JSON with the full metadata of its 20 hits (name, category, tags, duration,
uploader, audio and icon URLs). The mp3 files and the 200x200 icons then download straight from
cdn.pixabay.com, which does not need the browser.

Output layout (<out>/):
  catalog.json        raw hits as returned by the site (cache, re-used unless --refresh)
  index.json          bundle index: one entry per sound with category, tags, files, attribution, hashes
  audio/<name>.mp3    the sound effect (Pixabay's own file name, e.g. crowd-shocked-reaction-352766.mp3)
  icons/<name>.png    its icon (same stem; .jpg when the site serves JPEG data): the uploader's artwork, or -
                      as on the site, where sounds without artwork show a colored gradient - a tile rendered
                      with ffmpeg from that gradient plus the sound's waveform (index: icon.generated = true)

Requires Google Chrome or Chromium (auto-detected, or --chrome PATH); Linux/macOS only (the DevTools
pipe uses POSIX file descriptors). ffmpeg/ffprobe are optional: ffmpeg renders the icon tiles, ffprobe
adds exact duration, sample rate, channels and bit rate to the index. Sounds are released under the
Pixabay Content License (https://pixabay.com/service/license-summary/): free to use, no attribution
required.
"""
import argparse
import hashlib
import json
import os
import platform
import re
import select
import shutil
import subprocess
import sys
import tempfile
import threading
import time
import unicodedata
import urllib.error
import urllib.parse
import urllib.request
from concurrent.futures import ThreadPoolExecutor, as_completed
from datetime import datetime, timezone
from pathlib import Path

SITE = "https://pixabay.com"
LICENSE_NAME = "Pixabay Content License"
LICENSE_URL = "https://pixabay.com/service/license-summary/"
CHROME_CANDIDATES = [
    "google-chrome", "google-chrome-stable", "chromium", "chromium-browser", "chrome",
    "brave-browser", "microsoft-edge", "microsoft-edge-stable",
]
MAC_CHROME = [
    "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
    "/Applications/Chromium.app/Contents/MacOS/Chromium",
]
# Requests the search pages do not need (thumbnails, fonts, audio previews, ads, analytics).
BLOCKED_URLS = [
    "*.png", "*.jpg", "*.jpeg", "*.webp", "*.gif", "*.svg", "*.ico", "*.woff", "*.woff2", "*.ttf",
    "*.mp3", "*.mp4", "*.webm", "*googletagmanager*", "*google-analytics*", "*doubleclick*",
    "*googlesyndication*", "*onetrust*", "*snowplow*",
]
CHALLENGE_TITLE = "Just a moment"
ICON_SUFFIX = re.compile(r"_\d+x\d+\.png$")
IMAGE_EXTS = (".png", ".jpg", ".webp", ".gif")
# Tiles the site shows for sounds without artwork (CSS: linear-gradient(225deg, c0 0%, c1 100%)),
# keyed by the listing's sources.thumbnailOption.
GRADIENTS = {
    "aquaToIndigo": ("#93d9cd", "#686fee"),
    "redToIndigo": ("#bb434a", "#909cf2"),
    "orangeToGreen": ("#eed6ad", "#06885a"),
    "blueToOrange": ("#36d4fb", "#e3ab6c"),
    "aquaToGreen": ("#93d9cd", "#4d8168"),
    "orangeToYellow": ("#eed6ad", "#eac560"),
}
DEFAULT_GRADIENT = "aquaToIndigo"

_print_lock = threading.Lock()


def log(msg):
    with _print_lock:
        print(msg, flush=True)


# ---------------------------------------------------------------- helpers

def slugify(text):
    text = unicodedata.normalize("NFKD", str(text)).encode("ascii", "ignore").decode()
    text = re.sub(r"[^a-z0-9]+", "-", text.lower()).strip("-")
    return text or "sound"


def strip_html(text):
    return re.sub(r"\s+", " ", re.sub(r"<[^>]+>", "", text or "")).strip()


def split_keywords(text):
    seen, out = set(), []
    for part in re.split(r"[,;/]+", text or ""):
        part = part.strip()
        if part and part.lower() not in seen:
            seen.add(part.lower())
            out.append(part)
    return out


def human_size(n):
    for unit in ("B", "KB", "MB", "GB"):
        if n < 1024 or unit == "GB":
            return f"{n:.0f} {unit}" if unit == "B" else f"{n:.1f} {unit}"
        n /= 1024.0
    return f"{n:.1f} GB"


def now_iso():
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def search_url(query, duration, page, extra_params):
    params = []
    if duration:
        params.append(("duration", duration))
    params.extend(extra_params)
    if page > 1:
        params.append(("pagi", str(page)))
    url = f"{SITE}/sound-effects/search/{urllib.parse.quote(query, safe='')}/"
    return url + ("?" + urllib.parse.urlencode(params) if params else "")


def find_chrome(explicit):
    if explicit:
        path = shutil.which(explicit) or explicit
        if not os.path.exists(path):
            raise SystemExit(f"error: chrome binary not found: {explicit}")
        return path
    for name in CHROME_CANDIDATES:
        path = shutil.which(name)
        if path:
            return path
    for path in MAC_CHROME:
        if os.path.exists(path):
            return path
    raise SystemExit("error: no Chrome/Chromium found on PATH (use --chrome PATH)")


def chrome_user_agent(binary):
    """A plain desktop UA matching the real Chrome major version (headless Chrome advertises
    'HeadlessChrome', which is enough for Cloudflare to serve a challenge page)."""
    version = ""
    try:
        version = subprocess.run([binary, "--version"], capture_output=True, text=True, timeout=20).stdout
    except (OSError, subprocess.SubprocessError):
        pass
    m = re.search(r"(\d+)\.\d+\.\d+\.\d+", version)
    major = m.group(1) if m else "124"
    plat = {
        "Linux": "X11; Linux x86_64",
        "Darwin": "Macintosh; Intel Mac OS X 10_15_7",
        "Windows": "Windows NT 10.0; Win64; x64",
    }.get(platform.system(), "X11; Linux x86_64")
    return f"Mozilla/5.0 ({plat}) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/{major}.0.0.0 Safari/537.36"


# ---------------------------------------------------------------- headless Chrome over the DevTools pipe

class ChromeSession:
    """Minimal Chrome DevTools Protocol client over --remote-debugging-pipe.

    Chrome reads commands from fd 3 and writes responses/events to fd 4; every message is one JSON
    object terminated by a NUL byte. Only what this tool needs is implemented: create a tab, navigate,
    wait for DOMContentLoaded, evaluate JavaScript (with promises) and block unneeded requests.
    """

    KEPT_EVENTS = {"Page.domContentEventFired", "Page.loadEventFired", "Inspector.targetCrashed"}

    def __init__(self, binary, user_agent, headless=True):
        self.profile = tempfile.mkdtemp(prefix="pixabay-sfx-chrome-")
        self.proc = None
        self.session = None
        self._responses = {}
        self._events = []
        self._buf = b""
        self._next_id = 0
        r_in, w_in = os.pipe()      # Chrome reads r_in (as fd 3), we write w_in
        r_out, w_out = os.pipe()    # Chrome writes w_out (as fd 4), we read r_out
        args = [
            binary, "--remote-debugging-pipe", f"--user-data-dir={self.profile}", "--no-first-run",
            "--no-default-browser-check", "--disable-gpu", "--disable-dev-shm-usage", "--disable-extensions",
            "--disable-background-networking", "--disable-sync", "--mute-audio", "--window-size=1366,900",
            "--lang=en-US", "--disable-blink-features=AutomationControlled", f"--user-agent={user_agent}",
        ]
        if headless:
            args.insert(1, "--headless=new")
        if hasattr(os, "geteuid") and os.geteuid() == 0:
            args.append("--no-sandbox")
        args.append("about:blank")

        def child_setup():
            # Move the pipe ends onto fds 3 and 4 through safe temporaries (works whatever their numbers).
            tmp = max(r_in, w_out) + 10
            os.dup2(r_in, tmp)
            os.dup2(w_out, tmp + 1)
            os.dup2(tmp, 3)
            os.dup2(tmp + 1, 4)
            os.close(tmp)
            os.close(tmp + 1)

        try:
            self.proc = subprocess.Popen(
                args, preexec_fn=child_setup, close_fds=False, stdin=subprocess.DEVNULL,
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        finally:
            os.close(r_in)
            os.close(w_out)
        self._wfd, self._rfd = w_in, r_out

    # -- transport
    def _write(self, data):
        while data:
            n = os.write(self._wfd, data)
            data = data[n:]

    def _pump(self, timeout):
        """Read messages until one full message was dispatched or the timeout elapsed."""
        deadline = time.monotonic() + timeout
        while True:
            nul = self._buf.find(b"\0")
            if nul >= 0:
                raw, self._buf = self._buf[:nul], self._buf[nul + 1:]
                msg = json.loads(raw.decode("utf-8", "replace"))
                if "id" in msg:
                    self._responses[msg["id"]] = msg
                elif msg.get("method") in self.KEPT_EVENTS:
                    self._events.append(msg)
                return
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError(f"DevTools: no message within {timeout:.0f}s")
            if self.proc.poll() is not None:
                raise RuntimeError(f"Chrome exited unexpectedly (code {self.proc.returncode})")
            ready, _, _ = select.select([self._rfd], [], [], min(remaining, 0.5))
            if ready:
                chunk = os.read(self._rfd, 1 << 20)
                if not chunk:
                    raise RuntimeError("DevTools pipe closed")
                self._buf += chunk

    def send(self, method, params=None, session=None, timeout=60):
        self._next_id += 1
        mid = self._next_id
        msg = {"id": mid, "method": method, "params": params or {}}
        if session:
            msg["sessionId"] = session
        self._write(json.dumps(msg).encode("utf-8") + b"\0")
        deadline = time.monotonic() + timeout
        while mid not in self._responses:
            self._pump(max(0.05, deadline - time.monotonic()))
        reply = self._responses.pop(mid)
        if "error" in reply:
            raise RuntimeError(f"DevTools {method}: {reply['error'].get('message')}")
        return reply.get("result", {})

    def wait_event(self, method, timeout=60):
        deadline = time.monotonic() + timeout
        while True:
            for i, ev in enumerate(self._events):
                if ev.get("method") == method and ev.get("sessionId") == self.session:
                    del self._events[i]
                    return ev.get("params", {})
            self._pump(max(0.05, deadline - time.monotonic()))

    # -- page control
    def open_tab(self):
        target = self.send("Target.createTarget", {"url": "about:blank"})
        attached = self.send("Target.attachToTarget", {"targetId": target["targetId"], "flatten": True})
        self.session = attached["sessionId"]
        self.send("Page.enable", session=self.session)
        try:
            self.send("Network.enable", session=self.session)
            self.send("Network.setBlockedURLs", {"urls": BLOCKED_URLS}, session=self.session)
        except RuntimeError:
            pass  # blocking is only an optimisation

    def navigate(self, url, timeout=60):
        self._events.clear()
        result = self.send("Page.navigate", {"url": url}, session=self.session, timeout=timeout)
        if result.get("errorText"):
            raise RuntimeError(f"navigation failed: {result['errorText']}")
        self.wait_event("Page.domContentEventFired", timeout=timeout)

    def evaluate(self, expression, timeout=60):
        result = self.send("Runtime.evaluate", {
            "expression": expression, "awaitPromise": True, "returnByValue": True,
        }, session=self.session, timeout=timeout)
        if "exceptionDetails" in result:
            details = result["exceptionDetails"]
            text = (details.get("exception") or {}).get("description") or details.get("text") or "JS error"
            raise RuntimeError(text.splitlines()[0])
        return result.get("result", {}).get("value")

    def title(self):
        return self.evaluate("document.title") or ""

    def close(self):
        if self.proc is not None:
            try:
                if self.proc.poll() is None:
                    self.send("Browser.close", timeout=5)
            except (RuntimeError, TimeoutError, OSError):
                pass
            try:
                self.proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait()
        for fd in (self._wfd, self._rfd):
            try:
                os.close(fd)
            except OSError:
                pass
        shutil.rmtree(self.profile, ignore_errors=True)

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()


FETCH_BOOTSTRAP_JS = r"""
(async () => {
  if (window.__BOOTSTRAP__ && typeof window.__BOOTSTRAP__ === "object")
    return JSON.stringify({status: 200, text: JSON.stringify(window.__BOOTSTRAP__)});
  const u = window.__BOOTSTRAP_URL__;
  if (!u) return JSON.stringify({status: 0, text: "", error: "page has no __BOOTSTRAP_URL__"});
  const r = await fetch(u, {credentials: "include", headers: {"Accept": "application/json"}});
  return JSON.stringify({status: r.status, text: await r.text()});
})()
"""


def fetch_search_page(chrome, url, challenge_timeout, attempts=3):
    """Navigate to one search result page and return its bootstrap JSON (dict)."""
    last_error = None
    for attempt in range(1, attempts + 1):
        try:
            chrome.navigate(url)
            deadline = time.monotonic() + challenge_timeout
            while CHALLENGE_TITLE in chrome.title():
                if time.monotonic() > deadline:
                    raise RuntimeError("Cloudflare challenge not solved in time (try --no-headless)")
                time.sleep(1.0)
            reply = json.loads(chrome.evaluate(FETCH_BOOTSTRAP_JS))
            if reply.get("status") != 200:
                raise RuntimeError(reply.get("error") or f"bootstrap HTTP {reply.get('status')}")
            data = json.loads(reply["text"])
            if "page" not in data:
                raise RuntimeError("unexpected bootstrap layout (no 'page' key)")
            return data
        except (RuntimeError, TimeoutError, ValueError) as exc:
            last_error = exc
            log(f"  retry {attempt}/{attempts} for {url}: {exc}")
            time.sleep(3.0 * attempt)
    raise RuntimeError(f"giving up on {url}: {last_error}")


def slim_hit(hit):
    """Drop fields nobody needs from a raw hit (Canva deep links, uploader bios) before caching."""
    hit = {k: v for k, v in hit.items() if not k.startswith("canva")}
    user = hit.get("user")
    if isinstance(user, dict):
        hit["user"] = {k: v for k, v in user.items()
                       if k not in ("aboutMe", "socialLinks", "donation", "donations")}
    return hit


def walk_pages(chrome, args, params, hits, seen, label, stop_at):
    """Read the result pages of one search ordering, appending hits not seen before.

    Returns (site_total, pages_total, pages_fetched, exhausted); exhausted is False when the walk stopped
    early because stop_at hits were collected or --max-pages was reached.
    """
    page_no, pages_total, site_total = 1, None, None
    while True:
        url = search_url(args.query, args.duration, page_no, params)
        data = fetch_search_page(chrome, url, args.challenge_timeout)
        page = data.get("page") or {}
        results = [r for r in (page.get("results") or []) if r.get("mediaType", "audio") == "audio"]
        if pages_total is None:
            pages_total = int(page.get("pages") or 1)
            site_total = int(page.get("total") or len(results))
            log(f"{label}: {page.get('query') or args.query!r} filters={page.get('filtersString') or '-'}"
                f" order={page.get('order') or '-'} -> {site_total} sounds on {pages_total} pages")
        if int(page.get("page") or page_no) != page_no:
            return site_total, pages_total, page_no - 1, True  # past the end: the site serves page 1 again
        fresh = [slim_hit(r) for r in results if r.get("id") not in seen]
        seen.update(r["id"] for r in fresh)
        hits.extend(fresh)
        log(f"  {label} page {page_no}/{pages_total}: {len(fresh)} new hits ({len(hits)} total)")
        if not results or page_no >= pages_total:
            return site_total, pages_total, page_no, True
        if (stop_at and len(hits) >= stop_at) or (args.max_pages and page_no >= args.max_pages):
            return site_total, pages_total, page_no, False
        page_no += 1
        time.sleep(args.delay)


def enumerate_hits(chrome, args, extra_params):
    hits, seen = [], set()
    site_total, pages_total, fetched, exhausted = walk_pages(
        chrome, args, extra_params, hits, seen, "search", args.limit or None)
    # The default "most relevant" order is not stable across pages: some sounds show up twice while
    # others never do. A second walk in upload order (stable) catches the ones the first walk missed.
    ordered = any(key == "order" for key, _ in extra_params)
    if exhausted and site_total and len(hits) < site_total and not ordered:
        log(f"  {len(hits)} distinct sounds listed of {site_total}: re-reading in upload order for the rest")
        time.sleep(args.delay)
        fill_total, _, fill_fetched, _ = walk_pages(
            chrome, args, extra_params + [("order", "latest")], hits, seen, "fill", site_total)
        site_total, fetched = max(site_total, fill_total or 0), fetched + fill_fetched
    return hits, {"total": site_total, "pages": pages_total, "pages_fetched": fetched, "complete": exhausted}


# ---------------------------------------------------------------- downloads

def http_download(url, dest, user_agent, timeout=90, attempts=4):
    """Download url to dest atomically (via .part); returns the size in bytes."""
    req = urllib.request.Request(url, headers={
        "User-Agent": user_agent, "Referer": SITE + "/", "Accept": "*/*", "Accept-Language": "en-US,en;q=0.9",
    })
    tmp = dest.with_name(dest.name + ".part")
    last_error = None
    for attempt in range(1, attempts + 1):
        try:
            with urllib.request.urlopen(req, timeout=timeout) as resp, open(tmp, "wb") as f:
                expected = resp.headers.get("Content-Length")
                shutil.copyfileobj(resp, f, 1 << 16)
            size = tmp.stat().st_size
            if expected and int(expected) != size:
                raise OSError(f"short read ({size}/{expected} bytes)")
            if size == 0:
                raise OSError("empty response")
            os.replace(tmp, dest)
            return size
        except urllib.error.HTTPError as exc:
            last_error = exc
            if exc.code not in (429, 500, 502, 503, 504):
                break  # permanent (403/404): do not hammer
        except (urllib.error.URLError, OSError, TimeoutError) as exc:
            last_error = exc
        time.sleep(1.5 * attempt)
    if tmp.exists():
        tmp.unlink()
    raise OSError(f"{url}: {last_error}")


def sha256_of(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def image_extension(path):
    """Real image type from the magic bytes (the CDN serves JPEG data behind some .png URLs)."""
    with open(path, "rb") as f:
        head = f.read(16)
    if head.startswith(b"\x89PNG"):
        return ".png"
    if head.startswith(b"\xff\xd8\xff"):
        return ".jpg"
    if head[:4] == b"RIFF" and head[8:12] == b"WEBP":
        return ".webp"
    if head.startswith(b"GIF8"):
        return ".gif"
    return None


def existing_icon(icons_dir, stem):
    for ext in IMAGE_EXTS:
        path = icons_dir / f"{stem}{ext}"
        if path.exists() and path.stat().st_size > 0:
            return path
    return None


def download_icon(url, icons_dir, stem, user_agent):
    """Download an icon and store it under its real extension; returns the final path."""
    tmp = icons_dir / f"{stem}.download"
    http_download(url, tmp, user_agent)
    ext = image_extension(tmp)
    if ext is None:
        tmp.unlink()
        raise OSError(f"{url}: not an image")
    final = icons_dir / f"{stem}{ext}"
    os.replace(tmp, final)
    return final


def render_icon(ffmpeg, audio_path, gradient, dest, size):
    """Render the tile the site shows for a sound without artwork (its gradient) plus the waveform."""
    c0, c1 = GRADIENTS.get(gradient) or GRADIENTS[DEFAULT_GRADIENT]
    last = size - 1
    wave_w, wave_h = round(size * 0.84), round(size * 0.55)
    tmp = dest.with_name(dest.name + ".part.png")  # ffmpeg picks the encoder from the extension
    cmd = [
        ffmpeg, "-y", "-v", "error", "-nostdin",
        "-f", "lavfi", "-i", f"gradients=s={size}x{size}:c0={c0}:c1={c1}:x0={last}:y0=0:x1=0:y1={last}:nb_colors=2:d=1",
        "-i", str(audio_path),
        "-filter_complex",
        f"[1:a]aformat=channel_layouts=mono,showwavespic=s={wave_w}x{wave_h}:colors=white@0.92:filter=peak:scale=sqrt[w];"
        f"[0:v][w]overlay=(W-w)/2:(H-h)/2:format=auto",
        "-frames:v", "1", "-pix_fmt", "rgb24", str(tmp),
    ]
    try:
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=120, check=False)
    except (OSError, subprocess.SubprocessError) as exc:
        raise OSError(f"ffmpeg failed: {exc}")
    if result.returncode != 0 or not tmp.exists() or tmp.stat().st_size == 0:
        if tmp.exists():
            tmp.unlink()
        reason = result.stderr.strip().splitlines()[-1] if result.stderr.strip() else f"exit code {result.returncode}"
        raise OSError(f"ffmpeg could not render an icon: {reason}")
    os.replace(tmp, dest)
    return dest


def probe_audio(ffprobe, path):
    if not ffprobe:
        return None
    cmd = [ffprobe, "-v", "error", "-select_streams", "a:0", "-show_entries",
           "format=duration,bit_rate:stream=codec_name,sample_rate,channels", "-of", "json", str(path)]
    try:
        out = subprocess.run(cmd, capture_output=True, text=True, timeout=60, check=False).stdout
        info = json.loads(out or "{}")
    except (OSError, subprocess.SubprocessError, ValueError):
        return None
    fmt = info.get("format") or {}
    stream = (info.get("streams") or [{}])[0]
    result = {}
    if fmt.get("duration"):
        result["duration_sec"] = round(float(fmt["duration"]), 3)
    if fmt.get("bit_rate"):
        result["bit_rate"] = int(fmt["bit_rate"])
    if stream.get("codec_name"):
        result["codec"] = stream["codec_name"]
    if stream.get("sample_rate"):
        result["sample_rate"] = int(stream["sample_rate"])
    if stream.get("channels"):
        result["channels"] = int(stream["channels"])
    return result or None


def file_stem(hit):
    """Pixabay's own download name without extension, e.g. 'crowd-shocked-reaction-352766'."""
    sources = hit.get("sources") or {}
    stem = slugify(Path(sources.get("filename") or "").stem) if sources.get("filename") else ""
    if not stem or stem == "sound":
        stem = slugify(hit.get("name") or "sound")
    if not stem.endswith(str(hit["id"])):
        stem = f"{stem}-{hit['id']}"
    return stem


def icon_url(hit, size):
    url = (hit.get("sources") or {}).get("thumbnailUrl") or ""
    if url and size and ICON_SUFFIX.search(url):
        return ICON_SUFFIX.sub(f"_{size}x{size}.png", url)
    return url


def icon_info(hit, icon_path, out_dir, args):
    """Index description of an icon file: the site's artwork or a tile rendered from its gradient."""
    if icon_path is None:
        return None
    sources = hit.get("sources") or {}
    generated = not sources.get("thumbnailUrl")
    return {
        "file": icon_path.relative_to(out_dir).as_posix(),
        "bytes": icon_path.stat().st_size,
        "size_px": args.icon_size,
        "generated": generated,
        "gradient": (sources.get("thumbnailOption") or DEFAULT_GRADIENT) if generated else None,
        "url": None if generated else icon_url(hit, args.icon_size),
    }


def build_entry(hit, stem, audio_rel, audio_bytes, audio_sha, probe, icon):
    sources = hit.get("sources") or {}
    user = hit.get("user") or {}
    primary = hit.get("primaryTag") or []
    description = hit.get("description") or hit.get("alt") or ""
    listed = hit.get("duration")
    audio = {
        "file": audio_rel, "bytes": audio_bytes, "sha256": audio_sha, "url": sources.get("src"),
    }
    if probe:
        for key in ("codec", "sample_rate", "channels", "bit_rate"):
            if key in probe:
                audio[key] = probe[key]
    entry = {
        "id": hit["id"],
        "name": hit.get("name") or stem,
        "slug": stem,
        "category": (primary[0] if primary else "").strip().lower() or "uncategorized",
        "tags": [t[0] for t in (hit.get("tagList") or []) if t and t[0]],
        "keywords": split_keywords(description),
        "description": description,
        "duration_sec": (probe or {}).get("duration_sec", listed),
        "duration_listed_sec": listed,
        "audio": audio,
        "icon": icon,
        "page_url": SITE + (hit.get("href") or ""),
        "author": {
            "name": user.get("username") or "",
            "id": user.get("id"),
            "url": SITE + user["profileUrl"] if user.get("profileUrl") else "",
        },
        "attribution": strip_html(hit.get("attributionHtml")) or
                       f"Sound Effect by {user.get('username') or 'unknown'} from Pixabay",
        "stats": {
            "likes": hit.get("likeCount"), "downloads": hit.get("downloadCount"),
            "views": hit.get("viewCount"), "comments": hit.get("commentCount"),
        },
        "ai_generated": bool(hit.get("isAiGenerated")),
        "editors_choice": bool(hit.get("isEditorsChoice")),
    }
    return entry


def process_hit(hit, out_dir, args, user_agent, tools):
    """Download audio + icon of one hit and return (entry, status, detail)."""
    stem = file_stem(hit)
    sources = hit.get("sources") or {}
    audio_src = sources.get("src")
    if not audio_src:
        return None, "FAIL", "no audio url in listing"
    audio_path = out_dir / "audio" / f"{stem}.mp3"
    icons_dir = out_dir / "icons"
    status = "OK"
    try:
        if audio_path.exists() and audio_path.stat().st_size > 0 and not args.overwrite:
            audio_bytes, status = audio_path.stat().st_size, "SKIP"
        else:
            audio_bytes = http_download(audio_src, audio_path, user_agent)
        icon_src = icon_url(hit, args.icon_size)
        icon_path = None if args.overwrite else existing_icon(icons_dir, stem)
        if icon_path is None and icon_src:
            try:
                icon_path = download_icon(icon_src, icons_dir, stem, user_agent)
            except OSError:
                fallback = sources.get("thumbnailUrl")
                if not fallback or fallback == icon_src:
                    raise
                icon_path = download_icon(fallback, icons_dir, stem, user_agent)
        elif icon_path is None and tools.get("ffmpeg"):
            icon_path = render_icon(tools["ffmpeg"], audio_path, sources.get("thumbnailOption") or "",
                                    icons_dir / f"{stem}.png", args.icon_size)
    except OSError as exc:
        return None, "FAIL", str(exc)
    probe = probe_audio(tools.get("ffprobe"), audio_path)
    entry = build_entry(hit, stem, f"audio/{audio_path.name}", audio_bytes, sha256_of(audio_path), probe,
                        icon_info(hit, icon_path, out_dir, args))
    detail = f"{entry['duration_sec']}s  {human_size(audio_bytes)}  [{entry['category']}]"
    if entry["icon"] and entry["icon"]["generated"]:
        detail += "  icon rendered"
    elif entry["icon"] is None:
        detail += "  no icon"
    return entry, status, detail


def index_from_disk(hit, out_dir, args, tools):
    """--index-only: build an entry from files already present (no network)."""
    stem = file_stem(hit)
    audio_path = out_dir / "audio" / f"{stem}.mp3"
    if not audio_path.exists() or audio_path.stat().st_size == 0:
        return None
    icon_path = existing_icon(out_dir / "icons", stem)
    return build_entry(hit, stem, f"audio/{audio_path.name}", audio_path.stat().st_size, sha256_of(audio_path),
                       probe_audio(tools.get("ffprobe"), audio_path), icon_info(hit, icon_path, out_dir, args))


def write_index(path, entries, args, catalog_meta, extra_params):
    categories = {}
    for e in entries:
        categories[e["category"]] = categories.get(e["category"], 0) + 1
    filters = dict(extra_params)
    if args.duration:
        filters["duration"] = args.duration
    index = {
        "comment": "Royalty-free sound effects fetched from Pixabay. Generated by tools/pixabay_sfx_download.py; "
                   "'category' is Pixabay's primary category of the sound (lowercase, 'uncategorized' when the "
                   "site lists none), 'tags' its listed tags, 'keywords' the words of its description. "
                   "'icon.generated' marks tiles rendered locally (the site's gradient for that sound plus its "
                   "waveform) because the uploader set no artwork. 'sounds' keeps the site's relevance order. "
                   "File paths are relative to this file.",
        "generator": "tools/pixabay_sfx_download.py",
        "generated_at": now_iso(),
        "source": {
            "site": SITE,
            "search_url": search_url(args.query, args.duration, 1, extra_params),
            "query": args.query,
            "filters": filters,
            "site_total": catalog_meta.get("total"),
            "fetched_at": catalog_meta.get("fetched_at"),
        },
        "license": {"name": LICENSE_NAME, "url": LICENSE_URL, "attribution_required": False},
        "count": len(entries),
        "total_audio_bytes": sum(e["audio"]["bytes"] for e in entries),
        "icons": {
            "artwork": sum(1 for e in entries if e["icon"] and not e["icon"]["generated"]),
            "rendered": sum(1 for e in entries if e["icon"] and e["icon"]["generated"]),
            "missing": sum(1 for e in entries if e["icon"] is None),
        },
        "categories": dict(sorted(categories.items(), key=lambda kv: (-kv[1], kv[0]))),
        "sounds": entries,
    }
    path.write_text(json.dumps(index, indent=1, ensure_ascii=False) + "\n", encoding="utf-8")
    return index


# ---------------------------------------------------------------- main

def find_media_tools(args):
    """Optional ffmpeg (icon tiles) and ffprobe (exact durations); missing tools only degrade the index."""
    ffmpeg = None if args.no_render else (args.ffmpeg or shutil.which("ffmpeg"))
    ffprobe = None
    if not args.no_probe:
        ffprobe = args.ffprobe or shutil.which("ffprobe")
        if not ffprobe and ffmpeg:  # same directory as an explicitly given ffmpeg
            sibling = Path(ffmpeg).with_name("ffprobe" + Path(ffmpeg).suffix)
            ffprobe = str(sibling) if sibling.exists() else None
    if not args.no_render and not ffmpeg:
        log("note: ffmpeg not found, sounds without artwork will have no icon")
    if not args.no_probe and not ffprobe:
        log("note: ffprobe not found, index will carry the site's rounded durations only")
    return {"ffmpeg": ffmpeg, "ffprobe": ffprobe}


def parse_args():
    parser = argparse.ArgumentParser(
        description=__doc__.split("\n\n")[0],
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="\n\n".join(__doc__.split("\n\n")[1:]))
    parser.add_argument("-q", "--query", default="crowd reaction", help="search terms (default: %(default)s)")
    parser.add_argument("--duration", default="0-30", metavar="MIN-MAX",
                        help="duration filter in seconds as on the site, '' to disable (default: %(default)s)")
    parser.add_argument("--param", action="append", default=[], metavar="KEY=VALUE",
                        help="extra search query parameter, e.g. order=latest (repeatable)")
    parser.add_argument("-o", "--out", metavar="DIR",
                        help="output directory (default: <project>/soundpacks/pixabay-<query-slug>)")
    parser.add_argument("--limit", type=int, default=0, metavar="N", help="stop after N sounds (0 = all)")
    parser.add_argument("--max-pages", type=int, default=0, metavar="N", help="fetch at most N result pages")
    parser.add_argument("-j", "--jobs", type=int, default=4, metavar="N", help="parallel downloads (default: %(default)s)")
    parser.add_argument("--icon-size", type=int, default=200, choices=(200, 640),
                        help="icon edge in pixels (default: %(default)s)")
    parser.add_argument("--delay", type=float, default=1.5, metavar="SEC",
                        help="pause between result pages (default: %(default)s)")
    parser.add_argument("--challenge-timeout", type=float, default=45.0, metavar="SEC",
                        help="how long to wait for a Cloudflare challenge to clear (default: %(default)s)")
    parser.add_argument("--chrome", metavar="PATH", help="Chrome/Chromium binary (default: auto-detect)")
    parser.add_argument("--no-headless", action="store_true", help="show the browser window (debugging)")
    parser.add_argument("--ffmpeg", metavar="PATH", help="ffmpeg binary (default: from PATH; optional)")
    parser.add_argument("--ffprobe", metavar="PATH", help="ffprobe binary (default: next to ffmpeg / PATH; optional)")
    parser.add_argument("--no-probe", action="store_true", help="do not run ffprobe on downloaded files")
    parser.add_argument("--no-render", action="store_true",
                        help="do not render gradient+waveform tiles for sounds without artwork (icon stays null)")
    parser.add_argument("--refresh", action="store_true", help="re-read the search even if catalog.json exists")
    parser.add_argument("--overwrite", action="store_true", help="re-download files that already exist")
    parser.add_argument("--catalog-only", action="store_true", help="enumerate the search only, download nothing")
    parser.add_argument("--index-only", action="store_true", help="rebuild index.json from catalog.json and files on disk")
    parser.add_argument("--dry-run", action="store_true", help="list what would be downloaded, write nothing")
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be >= 1")
    if args.duration and not re.fullmatch(r"\d+-\d+", args.duration):
        parser.error("--duration must look like 0-30")
    return args


def main():
    args = parse_args()
    extra_params = []
    for item in args.param:
        if "=" not in item:
            raise SystemExit(f"error: --param expects KEY=VALUE, got {item!r}")
        key, value = item.split("=", 1)
        extra_params.append((key.strip(), value.strip()))

    project_root = Path(__file__).resolve().parent.parent
    out_dir = Path(args.out).expanduser() if args.out else project_root / "soundpacks" / f"pixabay-{slugify(args.query)}"
    catalog_path = out_dir / "catalog.json"
    index_path = out_dir / "index.json"
    tools = find_media_tools(args)

    # -- catalog: cached listing of the search
    catalog = None
    if catalog_path.exists() and not args.refresh:
        catalog = json.loads(catalog_path.read_text(encoding="utf-8"))
        cached = len(catalog.get("hits", []))
        partial = not catalog.get("complete", True) and (not args.limit or args.limit > cached)
        if partial and not args.index_only:
            log(f"catalog: {catalog_path} holds only {cached} hits of a partial earlier run, re-reading the search")
            catalog = None
        else:
            log(f"catalog: {catalog_path} ({cached} hits fetched {catalog.get('fetched_at')}; "
                f"--refresh to re-read the search)")
    if catalog is None and args.index_only:
        raise SystemExit(f"error: --index-only needs an existing {catalog_path}")
    if catalog is None:
        binary = find_chrome(args.chrome)
        user_agent = chrome_user_agent(binary)
        log(f"browser: {binary}")
        try:
            with ChromeSession(binary, user_agent, headless=not args.no_headless) as chrome:
                chrome.open_tab()
                hits, meta = enumerate_hits(chrome, args, extra_params)
        except KeyboardInterrupt:
            raise SystemExit("\ninterrupted")
        catalog = {
            "query": args.query, "duration": args.duration, "params": extra_params, "fetched_at": now_iso(),
            "total": meta["total"], "pages": meta["pages"], "pages_fetched": meta["pages_fetched"],
            "complete": meta["complete"], "hits": hits,
        }
        if not args.dry_run:
            out_dir.mkdir(parents=True, exist_ok=True)
            catalog_path.write_text(json.dumps(catalog, indent=1, ensure_ascii=False) + "\n", encoding="utf-8")
            log(f"catalog: wrote {catalog_path} ({len(hits)} hits)")

    hits = catalog.get("hits") or []
    if args.limit:
        hits = hits[:args.limit]
    if not hits:
        raise SystemExit("error: the search returned no sounds")

    if args.dry_run:
        for i, hit in enumerate(hits, 1):
            primary = hit.get("primaryTag") or ["uncategorized"]
            log(f"DRY  {i:4d}/{len(hits)}  {file_stem(hit)}  {hit.get('duration')}s  [{primary[0].lower()}]")
        total_listed = sum(int(h.get('duration') or 0) for h in hits)
        log(f"dry run: {len(hits)} sounds, ~{total_listed // 60} min of audio -> {out_dir}")
        return 0
    if args.catalog_only:
        return 0

    # -- index only: no network
    if args.index_only:
        entries, missing = [], 0
        for hit in hits:
            entry = index_from_disk(hit, out_dir, args, tools)
            if entry is None:
                missing += 1
            else:
                entries.append(entry)
        write_index(index_path, entries, args, catalog, extra_params)
        log(f"done: index {index_path} ({len(entries)} entries, {missing} listed sounds missing on disk)")
        return 0 if not missing else 1

    # -- download (the CDN needs no browser; a Chrome-like UA is enough)
    try:
        user_agent = chrome_user_agent(find_chrome(args.chrome))
    except SystemExit:
        user_agent = chrome_user_agent("")
    (out_dir / "audio").mkdir(parents=True, exist_ok=True)
    (out_dir / "icons").mkdir(parents=True, exist_ok=True)
    entries_by_id, counts = {}, {"OK": 0, "SKIP": 0, "FAIL": 0}
    width = len(str(len(hits)))
    try:
        with ThreadPoolExecutor(max_workers=args.jobs) as pool:
            futures = {pool.submit(process_hit, hit, out_dir, args, user_agent, tools): hit for hit in hits}
            for n, future in enumerate(as_completed(futures), 1):
                hit = futures[future]
                try:
                    entry, status, detail = future.result()
                except Exception as exc:  # a bug in the worker must not lose the run
                    entry, status, detail = None, "FAIL", f"{type(exc).__name__}: {exc}"
                counts[status] += 1
                if entry is not None:
                    entries_by_id[hit["id"]] = entry
                log(f"{status:<4} {n:{width}d}/{len(hits)}  {file_stem(hit)}  {detail}")
    except KeyboardInterrupt:
        raise SystemExit("\ninterrupted (re-run to resume; finished files are kept)")

    entries = [entries_by_id[h["id"]] for h in hits if h["id"] in entries_by_id]  # keep the search order
    index = write_index(index_path, entries, args, catalog, extra_params)
    cats = ", ".join(f"{k} {v}" for k, v in index["categories"].items())
    log(f"done: {len(hits)} sounds - {counts['OK']} downloaded, {counts['SKIP']} already present, {counts['FAIL']} failed")
    log(f"index: {index_path} ({index['count']} entries, {human_size(index['total_audio_bytes'])} of audio, "
        f"icons: {index['icons']['artwork']} artwork, {index['icons']['rendered']} rendered, {index['icons']['missing']} missing)")
    log(f"categories: {cats}")
    return 0 if counts["FAIL"] == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
