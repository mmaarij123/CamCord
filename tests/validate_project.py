from pathlib import Path
import json
import re
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def text(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8-sig")


# Parse the source manifests instead of mirroring implementation strings.
ET.parse(ROOT / "CamCord.vcxproj")
ET.parse(ROOT / "app.manifest")
project = text("CamCord.vcxproj")
for source in (ROOT / "src").glob("*.cpp"):
    require(source.name in project, f"{source.name} is missing from CamCord.vcxproj")

package = json.loads(text("ui/package.json"))
require(re.fullmatch(r"\d+\.\d+\.\d+", package["version"]), "UI package version must be major.minor.patch")
require((ROOT / "ui/package-lock.json").is_file(), "locked frontend dependencies are required")

resource = text("resources.rc")
numeric = re.search(r"FILEVERSION\s+(\d+),(\d+),(\d+),(\d+)", resource)
string = re.search(r'VALUE "FileVersion",\s*"([0-9.]+)(?:\\0)?"', resource)
require(numeric and ".".join(numeric.groups()[:3]) == package["version"],
        "numeric executable version must match ui/package.json")
require(string and string.group(1) == package["version"],
        "string executable version must match ui/package.json")

installer = text("installer/CamCord.iss")
installer_version = re.search(r'#define MyAppVersion "([0-9.]+)"', installer)
require(installer_version and installer_version.group(1) == package["version"], "installer version must match ui/package.json")
manifest = ET.parse(ROOT / "app.manifest")
identity = manifest.find("{urn:schemas-microsoft-com:asm.v1}assemblyIdentity")
require(identity is not None and identity.get("version") == package["version"] + ".0", "manifest version must match ui/package.json")
require("MinVersion=10.0.19041" in installer, "installer minimum Windows version is inconsistent")
require("FFmpegArchiveHash" in installer and "FFmpegBinaryHash" in installer,
        "installer must verify the downloaded recording engine")
require("Source: \"..\\bin\\Release\\ffmpeg.exe\"" not in installer,
        "public installer must not embed the development FFmpeg binary")

required = [
    ".github/workflows/windows-release.yml",
    "scripts/build-release.ps1",
    "scripts/build-installer.ps1",
    "scripts/package-release.ps1",
    "THIRD_PARTY_NOTICES.md",
]
for relative in required:
    require((ROOT / relative).is_file(), f"required release file is missing: {relative}")

ignore = text(".gitignore")
for rule in ("/bin/", "/obj/", "/dist/", "/third_party/", "*.mp4", "*.ini"):
    require(rule in ignore, f".gitignore is missing safety rule {rule}")

print("CamCord source and release manifests are structurally valid.")
