"""
Eat Lead: The Return of Matt Hazard - PC launcher.

Shows the installer if the game isn't installed yet; otherwise a settings screen and a Play button.
On Play it starts eatlead.exe with the matching ReXGlue command-line settings and the native
renderer's environment settings. None of the game's files are changed.

Settings are saved in %LOCALAPPDATA%\\EatLeadPC\\settings.json.
Adapted from the Bourne Conspiracy port's launcher (thegh0sts/bourne-conspiracy-x360-recomp).
"""

import json
import os
import re
import subprocess
import threading
import time
import urllib.request
import webbrowser
import zipfile
from pathlib import Path

import tkinter as tk
from tkinter import filedialog, messagebox, ttk

import launcher_text
import xex_achievements
from launcher_text import tr, tr_label
from eatlead_installer import (ACCENT, APP_NAME, BG, DIM, FG, LAUNCHER_HOME, InstallView, install_info_path,
                               make_root, start_game)

VERSION = "0.1.0"
# The last launcher version that changed the game itself: installs built before it are asked to rebuild.
GAME_VERSION = "0.1.0"
RELEASES_API = "https://api.github.com/repos/thegh0sts/EAT-LEAD-X360-PORT/releases/latest"
RELEASES_URL = "https://github.com/thegh0sts/EAT-LEAD-X360-PORT/releases/latest"
SETTINGS_PATH = LAUNCHER_HOME / "settings.json"
PANEL = "#1c1e24"
ACCENT_HOVER = "#d08a2e"

TABS = ("Game files", "Display", "Graphics", "Controls", "Saves", "Achievements")
# The game's languages (Games\<lang> on the disc) -> XLanguage ids (--user_language).
LANGUAGES = {"English": 1, "German": 3, "French": 4, "Spanish": 5, "Italian": 6}

RESOLUTIONS = ["Original (720p)", "1080p", "1440p", "4K"]
RES_FLAG = {"1080p": "1080p", "1440p": "1440p", "4K": "4k"}
RES_SIZE = {"Original (720p)": (1280, 720), "1080p": (1920, 1080), "1440p": (2560, 1440), "4K": (3840, 2160)}
# Render resolution: the size the native renderer draws at (BOTS_GFX_RENDER_SCALE); the game itself draws 1280x720.
# Whole steps only: at 1.5x (1920x1080) effects that work from exact pixel positions broke (the main menu's model in
# green and orange stripes, a cutscene all cyan), at 2x they're right (2026-11-19).
SCALES = ["1280×720 (original)", "2560×1440", "3840×2160 (demanding)"]
SCALE_FACTOR = {SCALES[0]: 1, SCALES[1]: 2, SCALES[2]: 3}
AA = ["Off", "FXAA", "FXAA (strong)"]
FXAA_MODE = {"FXAA": "1", "FXAA (strong)": "2"}
TEXTURE_FILTERING = ["Game's own", "Anisotropic 4x", "Anisotropic 8x", "Anisotropic 16x"]
ANISOTROPY = {TEXTURE_FILTERING[1]: 4, TEXTURE_FILTERING[2]: 8, TEXTURE_FILTERING[3]: 16}
# Only 30 and 60: the engine steps a fixed time per frame and has a 60 FPS mode of its own (src/fps_unlock.cpp);
# other rates would run the game at rate / 30 speed.
FPS = ["30 (original)", "60"]
FPS_RATE = {FPS[0]: 30, FPS[1]: 60}
FPS_COUNTER = ["Off", "FPS", "Detailed (frame times, GPU, CPU, memory)"]
PAD_PROMPTS = ["Auto", "PlayStation only", "Xbox only"]
PAD_PROMPTS_FLAG = {"Auto": "auto", "PlayStation only": "playstation", "Xbox only": "xbox"}

DEFAULTS = {
    "fullscreen": True,
    "resolution": "1080p",
    "render_scale": SCALES[1],  # 2x
    "aa": "FXAA",
    "texture_filtering": "Anisotropic 16x",
    "brightness": 100,
    "fps": "60",
    "fps_counter": "Off",
    "hide_achievements": False,
    "language": "English",
    "deadzone": 0,
    "vibration_strength": 100,
    "pad_prompts": "Auto",
    "menu_b_quit": True,
    "close_on_play": True,
    # The launcher's own language (launcher_text.py): "same" as the game's, or en / de / fr / es / it.
    "launcher_language": "same",
}


# --------------------------------------------------------------------------
# Settings and the install
# --------------------------------------------------------------------------

def version_key(tag):
    """'v0.1.0-1' -> (0, 1, 0, 1): numbers to compare; anything else in the tag counts as 0."""
    tag = tag.strip().lstrip("vV")
    main, _, build = tag.partition("-")
    nums = []
    for part in main.split("."):
        digits = "".join(c for c in part if c.isdigit())
        nums.append(int(digits) if digits else 0)
    nums += [0] * (3 - len(nums))
    digits = "".join(c for c in build if c.isdigit())
    return tuple(nums[:3]) + (int(digits) if digits else 0,)


def latest_release(timeout=5):
    """(tag, page URL, EatLead.exe download URL or None) of the newest GitHub release, or None."""
    req = urllib.request.Request(RELEASES_API, headers={"Accept": "application/vnd.github+json",
                                                        "User-Agent": f"EatLeadPC-launcher/{VERSION}"})
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            data = json.loads(r.read().decode("utf-8"))
        exe = next((a.get("browser_download_url") for a in data.get("assets", [])
                    if a.get("name", "").lower() == "eatlead.exe"), None)
        return data["tag_name"], data.get("html_url") or RELEASES_URL, exe
    except Exception:  # offline, rate-limited, no release yet: no update notice, nothing else
        return None


def download_file(url, dest, progress, timeout=30):
    """Downloads url to dest (via a .part file), calling progress(fraction or None)."""
    dest.parent.mkdir(parents=True, exist_ok=True)
    part = dest.with_name(dest.name + ".part")
    req = urllib.request.Request(url, headers={"User-Agent": f"EatLeadPC-launcher/{VERSION}"})
    with urllib.request.urlopen(req, timeout=timeout) as r, open(part, "wb") as f:
        total = int(r.headers.get("Content-Length") or 0)
        done = 0
        while True:
            chunk = r.read(1 << 16)
            if not chunk:
                break
            f.write(chunk)
            done += len(chunk)
            progress(done / total if total else None)
    part.replace(dest)


def load_settings():
    s = dict(DEFAULTS)
    try:
        s.update(json.loads(SETTINGS_PATH.read_text(encoding="utf-8")))
    except (OSError, ValueError):
        pass
    for key, values in (("resolution", RESOLUTIONS), ("render_scale", SCALES), ("aa", AA),
                        ("texture_filtering", TEXTURE_FILTERING), ("fps", FPS), ("fps_counter", FPS_COUNTER),
                        ("pad_prompts", PAD_PROMPTS), ("language", list(LANGUAGES))):
        if s.get(key) not in values:
            s[key] = DEFAULTS[key]
    for key, lo, hi in (("brightness", 50, 200), ("deadzone", 0, 40), ("vibration_strength", 0, 100)):
        try:
            s[key] = min(hi, max(lo, int(s[key])))
        except (TypeError, ValueError):
            s[key] = DEFAULTS[key]
    return s


def save_settings(s):
    LAUNCHER_HOME.mkdir(parents=True, exist_ok=True)
    SETTINGS_PATH.write_text(json.dumps(s, indent=2), encoding="utf-8")


def load_install():
    try:
        info = json.loads(install_info_path().read_text(encoding="utf-8"))
        if Path(info["exe"]).is_file() and (Path(info["game"]) / "default.xex").is_file():
            return info
    except (OSError, ValueError, KeyError):
        pass
    return None


def save_install(exe, game, root=None):
    """Remembers the install, and that this launcher's version built it ("version")."""
    LAUNCHER_HOME.mkdir(parents=True, exist_ok=True)
    info = {"exe": str(exe), "game": str(game), "root": str(root or Path(exe).parent), "version": VERSION}
    install_info_path().write_text(json.dumps(info, indent=2), encoding="utf-8")
    return info


def built_version(info):
    return (info or {}).get("version") or "0.0.0"


# --------------------------------------------------------------------------
# The game's user data: saves, achievements, logs
# --------------------------------------------------------------------------

def documents_dir():
    """The Documents folder (FOLDERID_Documents), or None."""
    if os.name != "nt":
        return None
    import ctypes
    from ctypes import wintypes

    class GUID(ctypes.Structure):
        _fields_ = [("d1", wintypes.DWORD), ("d2", wintypes.WORD), ("d3", wintypes.WORD), ("d4", ctypes.c_ubyte * 8)]

    docs = GUID(0xFDD39AD0, 0x238F, 0x46AF, (ctypes.c_ubyte * 8)(0xAD, 0xB4, 0x6C, 0x85, 0x48, 0x03, 0x69, 0xC7))
    out = ctypes.c_wchar_p()
    try:
        if ctypes.windll.shell32.SHGetKnownFolderPath(ctypes.byref(docs), 0, None, ctypes.byref(out)) != 0:
            return None
        return Path(out.value)
    except (OSError, AttributeError):
        return None
    finally:
        if out:
            ctypes.windll.ole32.CoTaskMemFree(out)


def logs_dir(info) -> Path:
    return Path(info.get("root") or Path(info["exe"]).parent)


def user_data_dir(info) -> Path:
    """Where the game keeps its saves and achievements: a user_data_root set in eatlead.toml, else the
    runtime's default, Documents\\eatlead."""
    exe = Path(info["exe"])
    try:
        m = re.search(r'^\s*user_data_root\s*=\s*["\']([^"\']+)["\']', (exe.parent / "eatlead.toml").read_text(
            encoding="utf-8", errors="replace"), re.M)
        if m:
            return Path(m.group(1))
    except OSError:
        pass
    docs = documents_dir() or Path.home() / "Documents"
    return docs / "eatlead"


def backup_saves(info) -> Path:
    """Zips the game's user data, leaving out caches and older backups, to <user data>\\backups."""
    root = user_data_dir(info)
    if not root.is_dir():
        raise OSError(f"{root} doesn't exist yet: start the game once first")
    out = root / "backups" / time.strftime("saves-%Y%m%d-%H%M%S.zip")
    out.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        for path in root.rglob("*"):
            rel = path.relative_to(root)
            if rel.parts and rel.parts[0] in ("cache", "backups"):
                continue
            if path.is_file():
                z.write(path, rel)
    return out


def save_display_name(header: Path):
    """A save's name: XCONTENT_DATA's display name (UTF-16BE, at offset 8) in the runtime's <name>.header."""
    try:
        data = header.read_bytes()[8:8 + 256]
        name = data.decode("utf-16-be", errors="ignore").split("\0", 1)[0].strip()
        return name or None
    except OSError:
        return None


def list_saves(root: Path):
    """The game's saved games: <user data>\\<profile>\\<title id>\\00000001\\<file name>\\, newest first."""
    saves = []
    if not root.is_dir():
        return saves
    for folder in root.glob("*/*/00000001/*"):
        if not folder.is_dir():
            continue
        files = [f for f in folder.rglob("*") if f.is_file()]
        modified = max([f.stat().st_mtime for f in files] or [folder.stat().st_mtime])
        header = folder.parent.parent / "Headers" / "00000001" / (folder.name + ".header")
        saves.append({"path": folder, "name": save_display_name(header) or folder.name, "modified": modified,
                      "size": sum(f.stat().st_size for f in files)})
    return sorted(saves, key=lambda s: s["modified"], reverse=True)


def load_achievements(info, language=1):
    """The game's achievements (src/achievement_list.cpp writes the list on every start, xex_achievements.py on
    the launcher's first opening) with their unlock times, or None before either."""
    folder = user_data_dir(info) / "achievements"
    lists = sorted(folder.glob("*-list.json")) if folder.is_dir() else []
    if not lists:
        return None
    data = json.loads(lists[0].read_text(encoding="utf-8"))
    unlocked = {}
    try:
        text = (folder / f"{data.get('title_id', '')}.toml").read_text(encoding="utf-8", errors="replace")
        for m in re.finditer(r"\[unlocked\.(\d+)\]\s*filetime\s*=\s*(\d+)", text):
            unlocked[int(m.group(1))] = int(m.group(2))
    except OSError:
        pass
    for a in data.get("achievements", []):
        ft = unlocked.get(int(a["id"]))
        a["unlocked"] = None if ft is None else time.localtime(max(0, (ft - 116444736000000000) // 10_000_000))
        a["icon_path"] = folder / a["icon"] if a.get("icon") else None
        for key, plural in (("name", "names"), ("description", "descriptions"),
                            ("locked_description", "locked_descriptions")):
            texts = a.get(plural) or {}
            a[key] = texts.get(str(language)) or texts.get("1") or a.get(key, "")
    return data["achievements"]


def save_crash_report(info) -> Path:
    """Zip the game log, the newest crash files and the launcher settings onto the desktop."""
    root = logs_dir(info)
    exe_logs = Path(info["exe"]).parent / "logs"
    files = [p for p in (root / "game.log", SETTINGS_PATH, install_info_path()) if p.is_file()]
    crash = sorted(exe_logs.glob("crash-*.*"), key=lambda p: p.stat().st_mtime, reverse=True) if exe_logs.is_dir() else []
    if crash:
        stem = crash[0].stem  # crash-<pid>: the newest crash's .txt and .dmp
        files += [p for p in crash if p.stem == stem]
    desktop = Path(os.environ.get("USERPROFILE", str(Path.home()))) / "Desktop"
    if not desktop.is_dir():
        desktop = Path.home()
    out = desktop / f"EatLead-report-{time.strftime('%Y%m%d-%H%M%S')}.zip"
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        for p in files:
            z.write(p, p.name)
    return out


# --------------------------------------------------------------------------
# Starting the game
# --------------------------------------------------------------------------

def window_size(s, screen):
    """Window size for windowed mode: the output size, shrunk (keeping its shape) to fit 85% of the screen."""
    w, h = RES_SIZE.get(s["resolution"], (1280, 720))
    if screen:
        sw, sh = int(screen[0] * 0.85), int(screen[1] * 0.85)
        k = min(1.0, sw / w, sh / h)
        if k < 1.0:
            w, h = int(w * k) // 2 * 2, int(h * k) // 2 * 2
    return w, h


def game_args(s, screen=None):
    """ReXGlue command-line settings for eatlead.exe (the native renderer's own come from game_env)."""
    args = ["--gpu_plugin", "native"]
    # Always explicit: the runtime's own default is fullscreen.
    args.append("--fullscreen=true" if s["fullscreen"] else "--fullscreen=false")
    if s["resolution"] in RES_FLAG:
        if s["fullscreen"]:
            args += ["--resolution", RES_FLAG[s["resolution"]]]
        else:
            # --resolution also sizes the window: set the game's output size and the window size separately.
            w, h = RES_SIZE[s["resolution"]]
            args += ["--video_mode_width", str(w), "--video_mode_height", str(h)]
    if not s["fullscreen"]:
        ww, wh = window_size(s, screen)
        args += ["--window_width", str(ww), "--window_height", str(wh)]
    # The guest vblank imitation off: eatlead.exe's own limiter holds the frame rate (src/frame_pacing.cpp),
    # and at 60 the engine's own 60 FPS step is switched on (src/fps_unlock.cpp).
    args.append("--vsync=false")
    args.append(f"--frame_rate={FPS_RATE[s['fps']]}")
    counter = s.get("fps_counter", "Off")
    if counter != "Off":
        args.append("--show_fps=true")
        if counter.startswith("Detailed"):
            args.append("--show_fps_detail=true")
    if s.get("hide_achievements"):
        args.append("--achievement_popups=false")
    if int(s.get("brightness", 100)) != 100:
        args.append(f"--brightness={int(s['brightness']) / 100:.2f}")  # the native renderer's last pass
    args.append(f"--user_language={LANGUAGES.get(s.get('language'), 1)}")
    # Controls: an extra dead zone on both sticks (the SDK's, as a fraction; 0 leaves the game's own), the
    # rumble strength (src/vibration.cpp; at 0 the runtime's own switch too), the button prompts
    # (src/pad_prompts.cpp) and B on the main menu (src/menu_quit.cpp).
    deadzone = int(s.get("deadzone", 0))
    if deadzone > 0:
        args += [f"--left_stick_deadzone_percentage={deadzone / 100:.2f}",
                 f"--right_stick_deadzone_percentage={deadzone / 100:.2f}"]
    strength = int(s.get("vibration_strength", 100))
    if strength < 100:
        args.append(f"--vibration_strength={strength}")
    if strength <= 0:
        args.append("--vibration=false")
    args.append(f"--pad_prompts={PAD_PROMPTS_FLAG.get(s.get('pad_prompts'), 'auto')}")
    if not s.get("menu_b_quit", True):
        args.append("--menu_b_quit=false")
    return args


def game_env(s):
    """The native renderer's settings, read from the environment: render resolution, FXAA, texture filtering.
    (eatlead.exe sets BOTS_GFX_DEPTH_BIAS_SCALE itself, src/gpu_preload.cpp.)"""
    env = {}
    k = SCALE_FACTOR.get(s.get("render_scale"), 1)
    env["BOTS_GFX_RENDER_SCALE"] = f"{k:g}"
    if s.get("aa") in FXAA_MODE:
        env["BOTS_GFX_FXAA"] = FXAA_MODE[s["aa"]]
    aniso = ANISOTROPY.get(s.get("texture_filtering"), 0)
    if aniso:
        env["BOTS_GFX_FORCE_ANISO"] = str(aniso)
    return env


# --------------------------------------------------------------------------
# UI
# --------------------------------------------------------------------------

def link_button(parent, text, cmd):
    """A small brass button: the launcher's secondary actions (Open logs, ...)."""
    return tk.Button(parent, text=text, command=cmd, font=("Segoe UI", 9), fg="white", bg=ACCENT,
                     activebackground=ACCENT_HOVER, activeforeground="white", bd=0, padx=10, pady=3, cursor="hand2")


def note(box, row, text):
    """A small grey explanation under a setting."""
    tk.Label(box, text=tr(text), font=("Segoe UI", 8), fg=DIM, bg=PANEL, anchor="w", justify="left", wraplength=640
             ).grid(row=row, column=0, columnspan=2, sticky="w", pady=(0, 4))


class SettingsView(tk.Frame):
    def __init__(self, master, info, on_reinstall, on_locate, on_language=None, settings=None, saved=None):
        super().__init__(master, bg=BG)
        self.info = info
        self.s = dict(settings) if settings else load_settings()
        self.on_language = on_language
        self.vars, self.combos, self.combo_values, self.combo_labels = {}, {}, {}, {}

        head = tk.Frame(self, bg=BG)
        head.pack(fill="x", padx=28, pady=(20, 4))
        tk.Label(head, text="EAT LEAD", font=("Impact", 26), fg=ACCENT_HOVER, bg=BG).pack(side="left")
        tk.Label(head, text="The Return of Matt Hazard", font=("Segoe UI", 11), fg=DIM, bg=BG
                 ).pack(side="left", anchor="s", padx=(12, 0), pady=(0, 6))
        choices = ["same"] + list(launcher_text.LANGUAGE_CODES)
        names = [tr("Same as game")] + [launcher_text.NATIVE_NAMES[c] for c in launcher_text.LANGUAGE_CODES]
        current = self.s.get("launcher_language", "same")
        self.vars["launcher_language"] = tk.StringVar(value=current if current in choices else "same")
        picker = ttk.Combobox(head, values=names, state="readonly", width=16)
        picker.current(choices.index(self.vars["launcher_language"].get()))
        picker.pack(side="right", anchor="s")
        picker.bind("<<ComboboxSelected>>", lambda e: self._launcher_language(choices[picker.current()]))
        tk.Label(head, text=tr("Launcher language"), font=("Segoe UI", 9), fg=DIM, bg=BG
                 ).pack(side="right", anchor="s", padx=(16, 6), pady=(0, 2))
        tk.Label(head, text=tr("Launcher {version}", version=VERSION), font=("Segoe UI", 9), fg=DIM, bg=BG
                 ).pack(side="right", anchor="s", pady=(0, 2))
        if launcher_text.language() != "en":
            tk.Label(self, text=tr("Translated by AI, not yet checked by native speakers."), font=("Segoe UI", 8),
                     fg=DIM, bg=BG).pack(anchor="e", padx=28)
        self.update_line = tk.Frame(self, bg=BG)
        self.update_line.pack(fill="x", padx=28)
        if version_key(built_version(info)) < version_key(GAME_VERSION):
            box = tk.Frame(self, bg=ACCENT)
            box.pack(fill="x", padx=28, pady=(6, 2))
            tk.Label(box, text=tr("Your game was built with {built}; this launcher is {version}. Reinstall to rebuild "
                                  "the game with this version's changes.", built=built_version(info), version=VERSION),
                     font=("Segoe UI", 10, "bold"), fg="white", bg=ACCENT, wraplength=700, justify="left"
                     ).pack(side="left", padx=10, pady=8)
            tk.Button(box, text=tr("Reinstall now"), command=on_reinstall, font=("Segoe UI", 10, "bold"),
                      bd=0, padx=10, pady=4).pack(side="right", padx=10)
            self.needs_rebuild = True
        else:
            self.needs_rebuild = False
        self._update_shown = False
        self.check_updates(manual=False)
        self.check_achievements()

        body = tk.Frame(self, bg=BG)
        nav = tk.Frame(body, bg=BG, width=180)
        nav.pack(side="left", fill="y", pady=(4, 0))
        nav.pack_propagate(False)
        self.pages_frame = tk.Frame(body, bg=PANEL)
        self.pages_frame.pack(side="left", fill="both", expand=True)
        self.pages, self.tab_buttons = {}, {}
        for name in TABS:
            b = tk.Button(nav, text=tr(name), anchor="w", font=("Segoe UI", 11, "bold"), bd=0, padx=16, pady=9,
                          cursor="hand2", command=lambda n=name: self.show_tab(n))
            b.pack(fill="x", pady=(0, 2))
            self.tab_buttons[name] = b

        box = self._page("Game files")
        tk.Label(box, text=tr("Installed in {folder}", folder=info['game']), font=("Segoe UI", 9), fg=DIM, bg=PANEL,
                 anchor="w").grid(row=0, column=0, columnspan=2, sticky="w")
        self._link(box, tr("Reinstall from a disc image..."), on_reinstall).grid(row=1, column=0, sticky="w", pady=(10, 4))
        self._link(box, tr("Use a different install..."), on_locate).grid(row=2, column=0, sticky="w")
        self._subheading(box, 3, "Help")
        links = tk.Frame(box, bg=PANEL)
        links.grid(row=4, column=0, columnspan=2, sticky="w")
        self._link(links, tr("Open logs"), self.open_logs).pack(side="left")
        self._link(links, tr("Save crash report"), self.crash_report).pack(side="left", padx=8)
        self._link(links, tr("Check for updates"), lambda: self.check_updates(manual=True)).pack(side="left")
        self._subheading(box, 5, "Game")
        self._combo(box, 6, "language", "Game language", list(LANGUAGES))
        note(box, 7, "The game's voices, subtitles and text. The achievement names follow it too.")

        box = self._page("Display")
        self._check(box, 0, "fullscreen", "Fullscreen")
        self._combo(box, 1, "resolution", "Output resolution", RESOLUTIONS)
        self._combo(box, 2, "render_scale", "Render resolution (sharper, slower)", SCALES)
        note(box, 3, "The game draws at 1280×720; the render resolution is what the native renderer draws it at. "
                     "Output resolution is the size of the picture on your screen.")
        self._combo(box, 4, "aa", "Anti-aliasing", AA)
        self._combo(box, 5, "fps", "Frame rate", FPS)
        note(box, 6, "60 uses the game's own 60 FPS mode, so it runs at normal speed.")
        self._combo(box, 7, "fps_counter", "FPS counter", FPS_COUNTER)
        self._check(box, 8, "hide_achievements", "Hide achievement pop-ups")
        self.vars["language"].trace_add("write", lambda *_: self._game_language_changed())
        tk.Label(box, text=tr("Native renderer by the Skullburglar project (github.com/nlwstein/skullburglar, "
                              "BSD 3-Clause)."), font=("Segoe UI", 8), fg=DIM, bg=PANEL, anchor="w"
                 ).grid(row=9, column=0, columnspan=2, sticky="w", pady=(10, 0))

        box = self._page("Graphics")
        self._slider(box, 0, "brightness", "Brightness (%)", 50, 200, 5)
        note(box, 1, "Black and white stay as they are.")
        self._combo(box, 2, "texture_filtering", "Texture filtering", TEXTURE_FILTERING)
        note(box, 3, "Keeps floors and walls sharp at an angle. The HUD and text keep the game's own filtering.")

        box = self._page("Controls")
        tk.Label(box, text=tr("Xbox-style controllers and PlayStation controllers, with the game's own button layout."),
                 font=("Segoe UI", 9), fg=DIM, bg=PANEL, anchor="w").grid(row=0, column=0, columnspan=2, sticky="w",
                                                                         pady=(0, 8))
        self._combo(box, 1, "pad_prompts", "Button prompts", PAD_PROMPTS)
        note(box, 2, "Auto shows PlayStation buttons while a DualShock or DualSense is connected, Xbox buttons "
                     "otherwise.")
        self._slider(box, 3, "deadzone", "Extra stick dead zone (%)", 0, 40, 1)
        note(box, 4, "On top of the game's own; raise it if the camera or Matt drifts on his own. 0: the game's alone.")
        self._slider(box, 5, "vibration_strength", "Controller vibration (%, 0 = off)", 0, 100, 5)
        self._check(box, 6, "menu_b_quit", "B (circle) on the main menu quits the game")

        box = self._page("Saves")
        line = tk.Frame(box, bg=PANEL)
        line.grid(row=0, column=0, columnspan=2, sticky="w")
        self._link(line, tr("Open saves folder"), self.open_saves).pack(side="left")
        self._link(line, tr("Back up saves"), self.backup_saves).pack(side="left", padx=8)
        self._link(line, tr("Refresh"), self.refresh_saves).pack(side="left")
        self.saves_label = tk.Label(box, text="", font=("Segoe UI", 9), fg=DIM, bg=PANEL, anchor="w", justify="left",
                                    wraplength=600)
        self.saves_label.grid(row=1, column=0, columnspan=2, sticky="w", pady=(10, 6))
        style = ttk.Style(self)
        style.configure("Saves.Treeview", background=BG, fieldbackground=BG, foreground=FG, rowheight=24,
                        borderwidth=0)
        style.configure("Saves.Treeview.Heading", font=("Segoe UI", 9, "bold"))
        tree_box = tk.Frame(self.pages["Saves"], bg=PANEL)
        tree_box.pack(fill="both", expand=True, pady=(4, 0))
        self.saves_tree = ttk.Treeview(tree_box, style="Saves.Treeview", show="headings",
                                       columns=("name", "modified", "size", "folder"))
        for col, text, width in (("name", "Save", 220), ("modified", "Last saved", 150), ("size", "Size", 70),
                                 ("folder", "Folder", 160)):
            self.saves_tree.heading(col, text=tr(text, "column"), anchor="w")
            self.saves_tree.column(col, width=width, anchor="w", stretch=col == "name")
        bar = ttk.Scrollbar(tree_box, orient="vertical", command=self.saves_tree.yview)
        self.saves_tree.configure(yscrollcommand=bar.set)
        self.saves_tree.pack(side="left", fill="both", expand=True)
        bar.pack(side="right", fill="y")
        self.saves_tree.bind("<Double-1>", lambda e: self.open_save_folder())

        self._page("Achievements")
        self.achievements_box = tk.Frame(self.pages["Achievements"], bg=PANEL)
        self.achievements_box.pack(fill="both", expand=True)
        self.show_tab(self.s.get("tab") if self.s.get("tab") in TABS else "Display")

        foot = tk.Frame(self, bg=BG)
        foot.pack(side="bottom", fill="x", padx=28, pady=18)
        tk.Button(foot, text=tr("Play"), width=16, command=self.play, font=("Segoe UI", 13, "bold"), bg=ACCENT,
                  fg="white", activebackground=ACCENT_HOVER, activeforeground="white", bd=0, pady=8).pack(side="left")
        tk.Button(foot, text=tr("Save settings"), width=13, command=self.save, font=("Segoe UI", 12), bd=0, pady=8,
                  fg=ACCENT_HOVER, bg="#22252d", activebackground="#2c3039", activeforeground=ACCENT_HOVER,
                  highlightthickness=1, highlightbackground=ACCENT).pack(side="left", padx=(10, 0))
        tk.Button(foot, text=tr("Quit"), width=10, command=self.quit_launcher, font=("Segoe UI", 12),
                  bd=0, pady=8).pack(side="left", padx=10)
        tk.Button(foot, text=tr("Defaults"), width=10, command=self.reset, font=("Segoe UI", 11),
                  bd=0, pady=6).pack(side="right")
        self.vars["close_on_play"] = tk.BooleanVar(value=self.s["close_on_play"])
        tk.Checkbutton(foot, text=tr("Close launcher when the game starts"), variable=self.vars["close_on_play"],
                       font=("Segoe UI", 9), fg=DIM, bg=BG, selectcolor=BG, activebackground=BG,
                       activeforeground=FG).pack(side="right", padx=10)

        self.status = tk.StringVar(value=tr("Settings are saved with Save settings, or when you press Play."))
        tk.Label(self, textvariable=self.status, font=("Segoe UI", 9), fg=DIM, bg=BG, wraplength=700,
                 justify="left").pack(side="bottom", anchor="w", padx=28)
        body.pack(fill="both", expand=True, padx=28, pady=(14, 4))
        self.saved = saved if saved is not None else self._snapshot()

    # widgets ------------------------------------------------------------
    def _page(self, title):
        page = tk.Frame(self.pages_frame, bg=PANEL, padx=22, pady=18)
        page.place(relx=0, rely=0, relwidth=1, relheight=1)
        box = tk.Frame(page, bg=PANEL)
        box.pack(fill="x")
        box.columnconfigure(1, weight=1)
        self.pages[title] = page
        return box

    def _subheading(self, box, row, text):
        tk.Label(box, text=tr(text), font=("Segoe UI", 9, "bold"), fg=DIM, bg=PANEL
                 ).grid(row=row, column=0, columnspan=2, sticky="w", pady=(14, 4))

    def show_tab(self, name):
        self.pages[name].tkraise()
        if name == "Saves":
            self.refresh_saves()
        elif name == "Achievements":
            self.refresh_achievements()
        for n, b in self.tab_buttons.items():
            on = n == name
            b.configure(bg=PANEL if on else BG, fg=ACCENT_HOVER if on else DIM, activebackground=PANEL,
                        activeforeground=FG)
        self.s["tab"] = name

    def _link(self, parent, text, cmd):
        return link_button(parent, text, cmd)

    def _check(self, box, row, key, text):
        v = self.vars[key] = tk.BooleanVar(value=self.s[key])
        tk.Checkbutton(box, text=tr(text), variable=v, font=("Segoe UI", 10), fg=FG, bg=PANEL, selectcolor=PANEL,
                       activebackground=PANEL, activeforeground=FG, anchor="w"
                       ).grid(row=row, column=0, columnspan=2, sticky="w")

    def _combo(self, box, row, key, text, values):
        v = self.vars[key] = tk.StringVar(value=self.s[key] if self.s[key] in values else DEFAULTS[key])
        label = tk.Label(box, text=tr(text), font=("Segoe UI", 10), fg=FG, bg=PANEL)
        label.grid(row=row, column=0, sticky="w", pady=2)
        combo = ttk.Combobox(box, state="readonly", width=36)
        combo.grid(row=row, column=1, sticky="e", pady=2)
        self.combos[key] = combo
        self.combo_labels[key] = label
        combo.bind("<<ComboboxSelected>>", lambda e, k=key: self._combo_picked(k))
        v.trace_add("write", lambda *_, k=key: self._combo_show(k))
        self.combo_values[key] = list(values)
        combo["values"] = [tr_label(x) for x in values]
        self._combo_show(key)

    def _combo_show(self, key):
        if key in self.combos:
            self.combos[key].set(tr_label(self.vars[key].get()))

    def _combo_picked(self, key):
        i = self.combos[key].current()
        if 0 <= i < len(self.combo_values[key]):
            self.vars[key].set(self.combo_values[key][i])

    def _slider(self, box, row, key, text, lo, hi, step):
        v = self.vars[key] = tk.IntVar(value=int(self.s[key]))
        tk.Label(box, text=tr(text), font=("Segoe UI", 10), fg=FG, bg=PANEL).grid(row=row, column=0, sticky="w")
        tk.Scale(box, variable=v, from_=lo, to=hi, resolution=step, orient="horizontal", length=230,
                 bg=PANEL, fg=FG, troughcolor="#2a2d35", highlightthickness=0, bd=0, activebackground=ACCENT
                 ).grid(row=row, column=1, sticky="e")

    def _launcher_language(self, choice):
        """A launcher language picked: saved at once on its own, then the launcher is drawn again in it."""
        self.vars["launcher_language"].set(choice)
        try:
            stored = json.loads(SETTINGS_PATH.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            stored = dict(DEFAULTS)
        stored["launcher_language"] = choice
        try:
            save_settings(stored)
        except OSError:
            pass
        saved = json.loads(self.saved)
        saved["launcher_language"] = choice
        self.saved = json.dumps(saved, sort_keys=True, default=str)
        if self.on_language:
            self.after_idle(self.on_language, self.collect(), self.saved)

    def _game_language_changed(self):
        if self.vars["launcher_language"].get() == "same" and self.on_language \
                and launcher_text.resolve("same", self.vars["language"].get()) != launcher_text.language():
            self.after_idle(self.on_language, self.collect(), self.saved)
        elif hasattr(self, "achievements_box"):
            self.refresh_achievements()

    # actions --------------------------------------------------------------
    def collect(self):
        for k, v in self.vars.items():
            self.s[k] = v.get()
        return self.s

    def reset(self):
        for k, v in self.vars.items():
            if k != "launcher_language":
                v.set(DEFAULTS[k])

    def check_achievements(self):
        """No achievement list yet: read it from default.xex (xex_achievements.py) in the background, so the
        Achievements tab doesn't wait for the game's first start."""
        folder = user_data_dir(self.info) / "achievements"
        try:
            lists = sorted(folder.glob("*-list.json")) if folder.is_dir() else []
            current = bool(lists) and all("names" in a for a in
                                          json.loads(lists[0].read_text(encoding="utf-8")).get("achievements", []))
        except (OSError, ValueError, AttributeError):
            current = False
        xex = Path(self.info["game"]) / "default.xex"
        if current or not xex.is_file():
            return
        result = {}

        def work():
            try:
                result["path"] = xex_achievements.export(xex, user_data_dir(self.info))
            except Exception as e:
                result["error"] = e
            result["done"] = True

        threading.Thread(target=work, daemon=True).start()
        self.after(300, self._poll_achievements, result, 100)

    def _poll_achievements(self, result, tries):
        try:
            if not self.winfo_exists():
                return
            if not result.get("done"):
                if tries > 0:
                    self.after(300, self._poll_achievements, result, tries - 1)
                return
            if result.get("path"):
                self.refresh_achievements()
        except tk.TclError:
            pass

    def check_updates(self, manual):
        result = {}
        threading.Thread(target=lambda: result.update(found=latest_release(), done=True), daemon=True).start()
        if manual:
            self.status.set(tr("Checking for updates..."))
        self.after(300, self._poll_update, result, manual, 40)

    def _poll_update(self, result, manual, tries):
        try:
            if not self.winfo_exists():
                return
            if not result.get("done"):
                if tries > 0:
                    self.after(300, self._poll_update, result, manual, tries - 1)
                elif manual:
                    self.status.set(tr("Couldn't reach GitHub to check for updates."))
                return
            found = result.get("found")
            if found and version_key(found[0]) > version_key(VERSION):
                self._show_update(*found)
                if manual:
                    self.status.set(tr("Version {version} is out: \"Download and update\" above.",
                                       version=found[0].lstrip('vV')))
            elif manual:
                self.status.set(tr("You have the latest version ({version}).", version=VERSION) if found
                                else tr("Couldn't reach GitHub to check for updates."))
        except tk.TclError:
            pass

    def _show_update(self, tag, url, exe_url=None):
        if self._update_shown:
            return
        self._update_shown = True
        version = tag.lstrip("vV")
        tk.Label(self.update_line, text=tr("Update available: {version} (you have {current}).", version=version,
                                           current=VERSION),
                 font=("Segoe UI", 10, "bold"), fg=ACCENT_HOVER, bg=BG).pack(side="left")
        if exe_url:
            tk.Button(self.update_line, text=tr("Download and update"), font=("Segoe UI", 10, "bold"), bd=0, padx=8,
                      command=lambda: self.download_update(version, exe_url)).pack(side="left", padx=10)
        self._link(self.update_line, tr("What's new"), lambda: webbrowser.open(url)).pack(side="left")

    def download_update(self, version, exe_url):
        dest = LAUNCHER_HOME / "updates" / f"EatLead-{version}.exe"
        state = {"frac": None, "done": False, "error": None}

        def work():
            try:
                download_file(exe_url, dest, lambda f: state.update(frac=f))
            except Exception as e:
                state["error"] = e
            state["done"] = True

        def poll():
            try:
                if not self.winfo_exists():
                    return
            except tk.TclError:
                return
            if not state["done"]:
                f = state["frac"]
                self.status.set(tr("Downloading {version}...", version=version) +
                                (f" {int(f * 100)}%" if f is not None else ""))
                self.after(250, poll)
                return
            if state["error"]:
                self.status.set(tr("Download failed ({error}). Use \"What's new\" to download it yourself.",
                                   error=state['error']))
                return
            self.status.set(tr("Downloaded {version} to {folder}.", version=version, folder=dest.parent))
            if messagebox.askyesno(tr("Update"), tr("{version} is downloaded.\n\nStart it now? This launcher closes, "
                                                    "and the new one asks you to reinstall the game (pick your disc "
                                                    "image) so the game is rebuilt with the update.",
                                                    version=version)):
                try:
                    subprocess.Popen([str(dest)], cwd=str(dest.parent))
                except OSError as e:
                    messagebox.showerror(tr("Update"), tr("Couldn't start {file}: {error}", file=dest, error=e))
                    return
                self.winfo_toplevel().destroy()

        threading.Thread(target=work, daemon=True).start()
        self.status.set(tr("Downloading {version}...", version=version))
        self.after(250, poll)

    def open_logs(self):
        folder = logs_dir(self.info)
        try:
            os.startfile(str(folder))
        except OSError as e:
            messagebox.showerror(tr("Open logs"), tr("Couldn't open {folder}: {error}", folder=folder, error=e))

    def open_saves(self):
        folder = user_data_dir(self.info)
        try:
            folder.mkdir(parents=True, exist_ok=True)
            os.startfile(str(folder))
        except OSError as e:
            messagebox.showerror(tr("Saves"), tr("Couldn't open {folder}: {error}", folder=folder, error=e))

    def backup_saves(self):
        try:
            out = backup_saves(self.info)
        except (OSError, zipfile.BadZipFile) as e:
            messagebox.showerror(tr("Back up saves"), tr("Couldn't back up the saves: {error}", error=e))
            return
        messagebox.showinfo(tr("Back up saves"), tr("Saved {file} to {folder}.\n\nTo restore it, close the game and "
                                                    "unzip it into {saves folder}, replacing the files there.")
                            .replace("{file}", out.name).replace("{folder}", str(out.parent))
                            .replace("{saves folder}", str(out.parent.parent)))
        try:
            os.startfile(str(out.parent))
        except OSError:
            pass

    def refresh_saves(self):
        root = user_data_dir(self.info)
        self.saves_tree.delete(*self.saves_tree.get_children())
        self.save_paths = {}
        saves = list_saves(root)
        for i, save in enumerate(saves):
            iid = str(i)
            self.save_paths[iid] = save["path"]
            self.saves_tree.insert("", "end", iid=iid, values=(
                save["name"], self._date(time.localtime(save["modified"])) +
                time.strftime("  %H:%M", time.localtime(save["modified"])),
                tr("{n} KB", n=f"{save['size'] / 1024:.0f}"), save["path"].name))
        where = tr("Saves folder: {folder}", folder=root)
        count = (tr("{n} saved game. ", n=1) if len(saves) == 1 else tr("{n} saved games. ", n=len(saves))) if saves \
            else tr("No saved games yet. ")
        self.saves_label.configure(text=count + where + "\n" + tr("Double-click a save to open its folder."))

    @staticmethod
    def _date(t):
        months = tr("Jan Feb Mar Apr May Jun Jul Aug Sep Oct Nov Dec").split()
        return f"{t.tm_mday} {months[t.tm_mon - 1]} {t.tm_year}"

    def open_save_folder(self):
        sel = self.saves_tree.selection()
        if sel and sel[0] in self.save_paths:
            try:
                os.startfile(str(self.save_paths[sel[0]]))
            except OSError as e:
                messagebox.showerror(tr("Saves"), tr("Couldn't open the folder: {error}", error=e))

    def refresh_achievements(self):
        box = self.achievements_box
        for child in box.winfo_children():
            child.destroy()
        try:
            items = load_achievements(self.info, LANGUAGES.get(self.vars["language"].get(), 1))
        except (OSError, ValueError, KeyError) as e:
            tk.Label(box, text=tr("Couldn't read the achievements: {error}", error=e), font=("Segoe UI", 10), fg=FG,
                     bg=PANEL).pack(anchor="w")
            return
        if not items:
            tk.Label(box, text=tr("Start the game once (to the title screen) and its achievements appear here."),
                     font=("Segoe UI", 10), fg=FG, bg=PANEL).pack(anchor="w")
            return
        got = [a for a in items if a["unlocked"]]
        score = sum(int(a["gamerscore"]) for a in got)
        total = sum(int(a["gamerscore"]) for a in items)
        tk.Label(box, text=tr("{got} of {total} unlocked    {score} of {max} G", got=len(got), total=len(items),
                              score=score, max=total), font=("Segoe UI", 11, "bold"),
                 fg=FG, bg=PANEL, anchor="w").pack(fill="x", pady=(0, 8))
        outer = tk.Frame(box, bg=PANEL)
        outer.pack(fill="both", expand=True)
        canvas = tk.Canvas(outer, bg=PANEL, highlightthickness=0)
        bar = ttk.Scrollbar(outer, orient="vertical", command=canvas.yview)
        rows = tk.Frame(canvas, bg=PANEL)
        rows.bind("<Configure>", lambda e: canvas.configure(scrollregion=canvas.bbox("all")))
        window = canvas.create_window((0, 0), window=rows, anchor="nw")
        canvas.bind("<Configure>", lambda e: canvas.itemconfigure(window, width=e.width))
        canvas.configure(yscrollcommand=bar.set)
        canvas.pack(side="left", fill="both", expand=True)
        bar.pack(side="right", fill="y")
        scroll = lambda e: canvas.yview_scroll(-1 * (e.delta // 120), "units")
        canvas.bind("<Enter>", lambda e: canvas.bind_all("<MouseWheel>", scroll))
        canvas.bind("<Leave>", lambda e: canvas.unbind_all("<MouseWheel>"))
        self.achievement_images = []
        items = sorted(got, key=lambda a: time.mktime(a["unlocked"]), reverse=True) + \
            [a for a in items if not a["unlocked"]]
        for a in items:
            bg = BG if a["unlocked"] else PANEL
            row = tk.Frame(rows, bg=bg, padx=8, pady=6)
            row.pack(fill="x", pady=2)
            icon = None
            if a.get("icon_path") and a["icon_path"].is_file():
                try:
                    icon = tk.PhotoImage(file=str(a["icon_path"]))
                    if icon.width() >= 64:
                        icon = icon.subsample(max(1, icon.width() // 48))
                    self.achievement_images.append(icon)
                except tk.TclError:
                    icon = None
            holder = tk.Frame(row, bg=bg, width=48, height=48)
            holder.grid(row=0, column=0, rowspan=2, padx=(0, 10))
            holder.grid_propagate(False)
            if icon:
                tk.Label(holder, image=icon, bg=bg).place(relx=0.5, rely=0.5, anchor="center")
            dim = not a["unlocked"]
            tk.Label(row, text=a["name"], font=("Segoe UI", 10, "bold"), fg=DIM if dim else FG, bg=bg,
                     anchor="w").grid(row=0, column=1, sticky="w")
            desc = a["description"] if a["unlocked"] or not a.get("locked_description") else a["locked_description"]
            tk.Label(row, text=desc, font=("Segoe UI", 9), fg=DIM, bg=bg, anchor="w", justify="left",
                     wraplength=420).grid(row=1, column=1, sticky="w")
            when = tr("Unlocked {date}", date=self._date(a["unlocked"])) if a["unlocked"] else tr("Locked")
            tk.Label(row, text=f"{a['gamerscore']} G", font=("Segoe UI", 10, "bold"),
                     fg=ACCENT_HOVER if a["unlocked"] else DIM, bg=bg).grid(row=0, column=2, sticky="e", padx=(10, 0))
            tk.Label(row, text=when, font=("Segoe UI", 8), fg=DIM, bg=bg).grid(row=1, column=2, sticky="e", padx=(10, 0))
            row.columnconfigure(1, weight=1)

    def crash_report(self):
        try:
            out = save_crash_report(self.info)
        except OSError as e:
            messagebox.showerror(tr("Crash report"), tr("Couldn't save the report: {error}", error=e))
            return
        messagebox.showinfo(tr("Crash report"), tr("Saved {file} to {folder}.\n\nAttach it when you report a problem. "
                                                   "It contains the game log, your launcher settings and any crash "
                                                   "dump (no game files).", file=out.name, folder=out.parent))

    def save(self):
        """Saves the settings without starting the game (Play saves them too)."""
        save_settings(self.collect())
        self.saved = self._snapshot()
        self.status.set(tr("Settings saved."))

    def _snapshot(self) -> str:
        return json.dumps({k: v for k, v in self.collect().items() if k != "tab"}, sort_keys=True, default=str)

    def unsaved(self) -> bool:
        return self._snapshot() != getattr(self, "saved", None)

    def confirm_close(self) -> bool:
        if not self.unsaved():
            return True
        answer = messagebox.askyesnocancel(tr("Quit"), tr("Save your changed settings before quitting?"))
        if answer is None:
            return False
        if answer:
            self.save()
        return True

    def quit_launcher(self):
        if self.confirm_close():
            self.winfo_toplevel().destroy()

    def play(self):
        if self.needs_rebuild and not messagebox.askyesno(
                tr("Play"), tr("The game hasn't been rebuilt for {version} yet, so this version's changes aren't "
                               "in it, and its new settings may not work.\n\nPlay anyway? (Reinstall now to rebuild "
                               "it.)", version=VERSION)):
            return
        s = self.collect()
        save_settings(s)
        self.saved = self._snapshot()
        exe, game = Path(self.info["exe"]), Path(self.info["game"])
        if not any(exe.parent.glob("rexgpu-native*.dll")):
            messagebox.showerror(tr("Play"), tr("The native renderer (rexgpu-native) isn't next to eatlead.exe. "
                                                "Reinstall to rebuild the game."))
            return
        log = logs_dir(self.info) / "game.log"
        args = [str(exe), "--game_data_root", str(game)] + \
            game_args(s, (self.winfo_screenwidth(), self.winfo_screenheight())) + ["--log_file", str(log)]
        try:
            # The working folder is the exe's: the renderer finds its images there (hints\, prompts\).
            start_game(args, exe.parent, game_env(s))
        except OSError as e:
            messagebox.showerror(tr("Play"), tr("Couldn't start the game: {error}", error=e))
            return
        self.status.set(tr("Game started."))
        if s["close_on_play"]:
            self.after(800, self.winfo_toplevel().destroy)


class Launcher:
    def __init__(self):
        self.root = make_root(APP_NAME)
        height = min(720, self.root.winfo_screenheight() - 80)
        self.root.geometry(f"980x{height}")
        self.root.minsize(900, 640)
        self.view = None
        s = load_settings()
        launcher_text.set_language(launcher_text.resolve(s.get("launcher_language", "same"),
                                                         s.get("language") if load_install() else None))
        info = load_install()
        if info:
            self.show_settings(info)
        else:
            self.show_install(first_time=True)
        self.root.protocol("WM_DELETE_WINDOW", self.on_close)

    def _swap(self, view):
        if self.view:
            self.view.destroy()
        self.view = view
        view.pack(fill="both", expand=True)

    def show_settings(self, info, settings=None, saved=None):
        self._swap(SettingsView(self.root, info, on_reinstall=lambda: self.show_install(), on_locate=self.locate,
                                on_language=lambda s, done: self._relabel(info, s, done), settings=settings,
                                saved=saved))

    def _relabel(self, info, s, saved):
        launcher_text.set_language(launcher_text.resolve(s.get("launcher_language", "same"), s.get("language")))
        self.show_settings(info, settings=s, saved=saved)

    def show_install(self, first_time=False, iso="", folder=None):
        info = load_install()
        default = Path(info["root"]) if info and info.get("root") else None
        back = None if first_time else (lambda: self.show_settings(load_install()))
        view = InstallView(self.root, on_installed=self.installed, on_back=back, default_dir=default,
                           on_language=lambda choice, iso, folder: self._relabel_install(first_time, choice, iso,
                                                                                         folder),
                           language_choice=load_settings().get("launcher_language", "same"), iso=iso)
        if folder:
            view.dir_var.set(folder)
        if first_time:
            link_button(view, tr("Already installed? Locate eatlead.exe..."), self.locate
                        ).pack(anchor="w", padx=28, pady=(8, 0))
        self._swap(view)

    def _relabel_install(self, first_time, choice, iso, folder):
        s = load_settings()
        s["launcher_language"] = choice
        try:
            save_settings(s)
        except OSError:
            pass
        info = load_install()
        launcher_text.set_language(launcher_text.resolve(choice, s.get("language") if info else None))
        self.show_install(first_time, iso=iso, folder=folder)

    def installed(self, exe, game, root):
        self.show_settings(save_install(exe, game, root))

    def locate(self):
        exe = filedialog.askopenfilename(title=tr("Locate eatlead.exe"),
                                         filetypes=[("eatlead.exe", "eatlead.exe"), (tr("Programs"), "*.exe")])
        if not exe:
            return
        game = filedialog.askdirectory(title=tr("Choose the extracted game folder (the one with default.xex)"))
        if not game:
            return
        if not (Path(game) / "default.xex").is_file():
            messagebox.showerror(tr("Locate"), tr("That folder doesn't contain default.xex."))
            return
        self.show_settings(save_install(exe, game))

    def on_close(self):
        if isinstance(self.view, SettingsView) and not self.view.confirm_close():
            return
        if isinstance(self.view, InstallView) and self.view.busy():
            if not messagebox.askyesno(tr("Install running"), tr("An install is still running. Quit anyway?\nRunning "
                                                                 "the launcher again will pick up where it left off.")):
                return
        self.root.destroy()

    def run(self):
        self.root.mainloop()


if __name__ == "__main__":
    Launcher().run()
