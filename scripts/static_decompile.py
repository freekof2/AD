#!/usr/bin/env python3
"""Reproducible, non-executing static analysis workflow for a Windows PE sample.

The script deliberately treats the input as bytes. It never launches the sample,
loads its DLLs, or evaluates extracted JavaScript. Optional decompilers are invoked
only against a copy in an ephemeral analysis workspace.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import struct
import subprocess
import sys
import textwrap
import zipfile
from pathlib import Path
from typing import Any
from urllib.parse import urlparse
from urllib.request import HTTPRedirectHandler, Request, build_opener

MAX_DOWNLOAD_BYTES = 600 * 1024 * 1024
ALLOWED_DOWNLOAD_HOSTS = {
    "github.com",
    "objects.githubusercontent.com",
    "release-assets.githubusercontent.com",
}


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


class SafeRedirectHandler(HTTPRedirectHandler):
    """Allow redirects only when the destination remains in the allow-list."""

    def redirect_request(self, req, fp, code, msg, headers, newurl):  # type: ignore[no-untyped-def]
        parsed = urlparse(newurl)
        if parsed.scheme != "https" or parsed.hostname not in ALLOWED_DOWNLOAD_HOSTS:
            raise ValueError(
                f"Refusing redirect to {parsed.scheme}://{parsed.hostname}; "
                f"allowed HTTPS hosts: {sorted(ALLOWED_DOWNLOAD_HOSTS)}"
            )
        return super().redirect_request(req, fp, code, msg, headers, newurl)


def download_verified(url: str, destination: Path, expected_sha256: str) -> None:
    parsed = urlparse(url)
    if parsed.scheme != "https" or parsed.hostname not in ALLOWED_DOWNLOAD_HOSTS:
        raise ValueError(
            f"Refusing download from {parsed.scheme}://{parsed.hostname}; "
            f"allowed HTTPS hosts: {sorted(ALLOWED_DOWNLOAD_HOSTS)}"
        )
    expected = expected_sha256.lower().strip()
    if len(expected) != 64 or any(c not in "0123456789abcdef" for c in expected):
        raise ValueError("--sha256 must be a 64-character hexadecimal digest")

    destination.parent.mkdir(parents=True, exist_ok=True)
    request = Request(url, headers={"User-Agent": "AD-static-analysis/1.0"})
    received = 0
    opener = build_opener(SafeRedirectHandler)
    with opener.open(request, timeout=90) as response, destination.open("wb") as handle:
        while True:
            chunk = response.read(1024 * 1024)
            if not chunk:
                break
            received += len(chunk)
            if received > MAX_DOWNLOAD_BYTES:
                raise ValueError(f"download exceeds {MAX_DOWNLOAD_BYTES} bytes")
            handle.write(chunk)

    actual = sha256_file(destination)
    if actual != expected:
        destination.unlink(missing_ok=True)
        raise ValueError(f"SHA-256 mismatch: expected {expected}, got {actual}")


def read_u16(data: bytes, offset: int) -> int:
    return struct.unpack_from("<H", data, offset)[0]


def read_u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def parse_pe(path: Path) -> dict[str, Any]:
    data = path.read_bytes()
    result: dict[str, Any] = {
        "is_pe": False,
        "file_size": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
        "markers": {},
    }
    if len(data) < 0x40 or data[:2] != b"MZ":
        result["error"] = "missing MZ signature"
        return result
    pe_offset = read_u32(data, 0x3C)
    result["dos_header"] = {"e_lfanew": pe_offset}
    if pe_offset + 24 > len(data) or data[pe_offset : pe_offset + 4] != b"PE\0\0":
        result["error"] = "missing PE signature"
        return result

    machine = read_u16(data, pe_offset + 4)
    number_of_sections = read_u16(data, pe_offset + 6)
    timestamp = read_u32(data, pe_offset + 8)
    size_of_optional_header = read_u16(data, pe_offset + 20)
    characteristics = read_u16(data, pe_offset + 22)
    optional_offset = pe_offset + 24
    optional_magic = read_u16(data, optional_offset)
    entrypoint = read_u32(data, optional_offset + 16)
    image_base = (
        struct.unpack_from("<Q", data, optional_offset + 24)[0]
        if optional_magic == 0x20B
        else read_u32(data, optional_offset + 28)
    )
    section_offset = optional_offset + size_of_optional_header
    sections: list[dict[str, Any]] = []
    max_raw_end = 0
    for index in range(number_of_sections):
        offset = section_offset + index * 40
        if offset + 40 > len(data):
            break
        name = data[offset : offset + 8].rstrip(b"\0").decode("ascii", "replace")
        virtual_size = read_u32(data, offset + 8)
        virtual_address = read_u32(data, offset + 12)
        raw_size = read_u32(data, offset + 16)
        raw_pointer = read_u32(data, offset + 20)
        section = {
            "name": name,
            "virtual_size": virtual_size,
            "virtual_address": f"0x{virtual_address:08x}",
            "raw_size": raw_size,
            "raw_pointer": raw_pointer,
        }
        sections.append(section)
        max_raw_end = max(max_raw_end, raw_pointer + raw_size)

    result.update(
        {
            "is_pe": True,
            "machine": f"0x{machine:04x}",
            "number_of_sections": number_of_sections,
            "timestamp_unix": timestamp,
            "optional_header_magic": f"0x{optional_magic:04x}",
            "entrypoint_rva": f"0x{entrypoint:08x}",
            "image_base": f"0x{image_base:x}",
            "characteristics": f"0x{characteristics:04x}",
            "sections": sections,
            "overlay_offset": max_raw_end,
            "overlay_size": max(0, len(data) - max_raw_end),
        }
    )
    return result


def add_pefile_details(path: Path, result: dict[str, Any]) -> None:
    try:
        import pefile  # type: ignore
    except ImportError:
        result["pefile"] = {"available": False}
        return

    result["pefile"] = {"available": True}
    pe = None
    try:
        pe = pefile.PE(str(path), fast_load=False)
        imports: list[dict[str, Any]] = []
        for entry in getattr(pe, "DIRECTORY_ENTRY_IMPORT", []):
            dll = entry.dll.decode("utf-8", "replace") if entry.dll else ""
            names = []
            for item in entry.imports:
                if item.name:
                    names.append(item.name.decode("utf-8", "replace"))
                else:
                    names.append(f"ordinal:{item.ordinal}")
            imports.append({"dll": dll, "symbols": names[:500]})
        result["imports"] = imports
        directories = []
        for index, directory in enumerate(pe.OPTIONAL_HEADER.DATA_DIRECTORY):
            if directory.VirtualAddress or directory.Size:
                directories.append(
                    {
                        "index": index,
                        "rva": f"0x{directory.VirtualAddress:08x}",
                        "size": directory.Size,
                    }
                )
        result["data_directories"] = directories
        com_directory = pe.OPTIONAL_HEADER.DATA_DIRECTORY[14]
        result["dotnet"] = bool(com_directory.VirtualAddress and com_directory.Size)
    except Exception as exc:  # parser errors are recorded, not fatal to the report
        result["pefile"]["error"] = f"{type(exc).__name__}: {exc}"
    finally:
        if pe is not None:
            pe.close()


def extract_strings(data: bytes, minimum: int = 4) -> list[str]:
    output: list[str] = []
    current = bytearray()
    for byte in data:
        if 0x20 <= byte <= 0x7E:
            current.append(byte)
        else:
            if len(current) >= minimum:
                output.append(current.decode("ascii", "replace"))
            current.clear()
    if len(current) >= minimum:
        output.append(current.decode("ascii", "replace"))

    # Scan both byte parities because a UTF-16LE string can begin at an odd
    # offset when it is adjacent to an ASCII or binary field.
    for start_offset in (0, 1):
        wide_current: list[int] = []
        for index in range(start_offset, len(data) - 1, 2):
            # Windows UTF-16LE ASCII text has a zero high byte; this
            # condition avoids treating arbitrary two-byte binary values as
            # characters when scanning both parities.
            value = data[index] | (data[index + 1] << 8)
            is_wide_ascii = data[index + 1] == 0 and 0x20 <= value <= 0x7E
            if is_wide_ascii and not wide_current and index > start_offset and data[index - 1] != 0:
                is_wide_ascii = False
            if is_wide_ascii:
                wide_current.append(value)
            else:
                if len(wide_current) >= minimum:
                    output.append("".join(map(chr, wide_current)))
                wide_current.clear()
        if len(wide_current) >= minimum:
            output.append("".join(map(chr, wide_current)))
    return sorted(set(output), key=lambda value: (value.lower(), value))


def scan_markers(data: bytes) -> dict[str, list[int]]:
    marker_list = [
        b"app.asar",
        b"resources\\app.asar",
        b"resources/app.asar",
        b"node_modules",
        b"BSJB",
        b"7zXZ",
        b"PK\x03\x04",
        b"UPX!",
    ]
    result: dict[str, list[int]] = {}
    for marker in marker_list:
        offsets: list[int] = []
        start = 0
        while len(offsets) < 50:
            found = data.find(marker, start)
            if found < 0:
                break
            offsets.append(found)
            start = found + 1
        result[marker.decode("latin-1")] = offsets
    return result


def list_zip_members(path: Path) -> list[str]:
    try:
        with zipfile.ZipFile(path) as archive:
            return archive.namelist()[:5000]
    except (OSError, zipfile.BadZipFile):
        return []


def persist_report(report: dict[str, Any], analysis_dir: Path) -> None:
    (analysis_dir / "report.json").write_text(
        json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8"
    )
    write_summary(report, analysis_dir / "SUMMARY.md")


def unpack_with_7z(sample: Path, analysis_dir: Path, report: dict[str, Any]) -> None:
    """Attempt archive extraction with 7z; never invokes the extracted files."""
    extractor = shutil.which("7z")
    if not extractor:
        report["tools"]["7z_extract"] = "skipped: 7z not installed"
        persist_report(report, analysis_dir)
        return

    output = analysis_dir / "unpacked"
    output.mkdir(parents=True, exist_ok=True)
    result = command_output(
        [extractor, "x", str(sample), f"-o{output}", "-y"], timeout=600
    )
    report["tools"]["7z_extract"] = result

    inventory: list[dict[str, Any]] = []
    candidates: list[str] = []
    for path in sorted(output.rglob("*")):
        if not path.is_file():
            continue
        relative = path.relative_to(output).as_posix()
        item = {"path": relative, "size": path.stat().st_size}
        inventory.append(item)
        if path.suffix.lower() in {".asar", ".dll", ".exe", ".js", ".json", ".pdb"}:
            candidates.append(relative)
        if len(inventory) >= 10000:
            break
    report["unpacked_inventory"] = inventory
    report["unpacked_candidates"] = candidates
    persist_report(report, analysis_dir)


def command_output(command: list[str], timeout: int = 120) -> dict[str, Any]:
    try:
        completed = subprocess.run(
            command,
            capture_output=True,
            text=True,
            timeout=timeout,
            check=False,
        )
        return {
            "command": command,
            "returncode": completed.returncode,
            "stdout": completed.stdout[:20000],
            "stderr": completed.stderr[:20000],
        }
    except FileNotFoundError:
        return {"command": command, "available": False}
    except subprocess.TimeoutExpired as exc:
        return {"command": command, "timeout": timeout, "stdout": str(exc.stdout or "")[:20000]}


def write_summary(report: dict[str, Any], destination: Path) -> None:
    pe = report.get("pe", {})
    lines = [
        "# Static analysis summary",
        "",
        "> This report was produced by reading the sample as bytes. The sample was not executed.",
        "",
        "## Sample identity",
        "",
        f"- **Path:** `{report['sample']['path']}`",
        f"- **Size:** `{report['sample']['size']} bytes`",
        f"- **SHA-256:** `{report['sample']['sha256']}`",
        f"- **Expected SHA-256:** `{report['sample'].get('expected_sha256', 'not supplied')}`",
        "",
        "## PE fingerprint",
        "",
        f"- **PE:** `{pe.get('is_pe')}`",
        f"- **Machine:** `{pe.get('machine', 'unknown')}`",
        f"- **Sections:** `{pe.get('number_of_sections', 'unknown')}`",
        f"- **Entry point RVA:** `{pe.get('entrypoint_rva', 'unknown')}`",
        f"- **Overlay:** `{pe.get('overlay_size', 0)} bytes` starting at `{pe.get('overlay_offset', 0)}`",
        f"- **.NET/CLR marker:** `{pe.get('dotnet', 'unknown')}`",
        f"- **Unpacked files:** `{len(report.get('unpacked_inventory', []))}`",
        "",
        "## Static indicators",
        "",
        "| Indicator | Offsets / result |",
        "| --- | --- |",
    ]
    for marker, offsets in pe.get("markers", {}).items():
        lines.append(f"| `{marker}` | `{', '.join(map(str, offsets[:10])) or 'not found'}` |")
    lines.extend(
        [
            "",
            "## Tool status",
            "",
            "| Tool | Result |",
            "| --- | --- |",
        ]
    )
    for name, value in report.get("tools", {}).items():
        if isinstance(value, dict):
            if value.get("available") is False:
                summary = "not installed"
            elif "returncode" in value:
                summary = f"returncode={value['returncode']}"
            else:
                summary = "completed"
        else:
            summary = str(value)
        lines.append(f"| `{name}` | {summary} |")
    lines.extend(
        [
            "",
            "## Interpretation boundary",
            "",
            "The outputs are triage artifacts, not proof of runtime behavior. Any dynamic analysis, network interaction, credential use, anti-analysis bypass, or execution of the sample is intentionally outside this workflow.",
            "",
        ]
    )
    destination.write_text("\n".join(lines), encoding="utf-8")


def analyze(sample: Path, analysis_dir: Path, expected_sha256: str | None = None) -> dict[str, Any]:
    analysis_dir.mkdir(parents=True, exist_ok=True)
    data = sample.read_bytes()
    sample_report: dict[str, Any] = {
        "path": str(sample),
        "size": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
    }
    if expected_sha256:
        sample_report["expected_sha256"] = expected_sha256.lower()
        if sample_report["sha256"] != expected_sha256.lower():
            raise ValueError("sample hash does not match expected SHA-256")

    pe_report = parse_pe(sample)
    add_pefile_details(sample, pe_report)
    pe_report["markers"] = scan_markers(data)
    strings = extract_strings(data)
    (analysis_dir / "strings.txt").write_text("\n".join(strings) + "\n", encoding="utf-8")

    report: dict[str, Any] = {"sample": sample_report, "pe": pe_report, "tools": {}}
    report["tools"]["file"] = command_output(["file", str(sample)])
    report["tools"]["sha256sum"] = command_output(["sha256sum", str(sample)])
    report["tools"]["7z"] = command_output(["7z", "l", "-slt", str(sample)], timeout=180)

    if sample.suffix.lower() == ".zip":
        report["zip_members"] = list_zip_members(sample)

    (analysis_dir / "report.json").write_text(
        json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8"
    )
    write_summary(report, analysis_dir / "SUMMARY.md")
    return report


def run_optional_decompilers(sample: Path, analysis_dir: Path, report: dict[str, Any]) -> None:
    pe = report.get("pe", {})
    if not pe.get("dotnet"):
        report["tools"]["ilspycmd"] = "skipped: CLR directory not detected"
    else:
        ilspy = shutil.which("ilspycmd")
        if not ilspy:
            report["tools"]["ilspycmd"] = "skipped: ilspycmd not installed"
        else:
            output = analysis_dir / "decompiled"
            output.mkdir(exist_ok=True)
            result = command_output(
                [ilspy, "-o", str(output), "--project", str(sample)], timeout=900
            )
            report["tools"]["ilspycmd"] = result

    # An Electron ASAR is an archive, not executable JavaScript. We only extract
    # it when a separate app.asar file is supplied in the analysis workspace.
    asar = shutil.which("asar") or shutil.which("npx")
    candidate = analysis_dir / "app.asar"
    if candidate.exists() and asar:
        output = analysis_dir / "asar-extracted"
        output.mkdir(exist_ok=True)
        command = [asar, "extract", str(candidate), str(output)]
        report["tools"]["asar"] = command_output(command, timeout=600)
    else:
        report["tools"]["asar"] = "skipped: no separate analysis/app.asar supplied"

    (analysis_dir / "report.json").write_text(
        json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8"
    )
    write_summary(report, analysis_dir / "SUMMARY.md")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Non-executing static PE analysis and optional decompilation workflow"
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    fetch = subparsers.add_parser("fetch", help="download and verify a sample")
    fetch.add_argument("--url", required=True)
    fetch.add_argument("--sha256", required=True)
    fetch.add_argument("--output", type=Path, required=True)

    full = subparsers.add_parser("full", help="fetch, fingerprint, and optionally decompile")
    full.add_argument("--url", required=True)
    full.add_argument("--sha256", required=True)
    full.add_argument("--sample", type=Path, required=True)
    full.add_argument("--analysis-dir", type=Path, required=True)
    full.add_argument("--unpack", action="store_true", help="attempt read-only 7z extraction")
    full.add_argument("--decompile", action="store_true")

    local = subparsers.add_parser("local", help="analyze an already downloaded sample")
    local.add_argument("--sample", type=Path, required=True)
    local.add_argument("--analysis-dir", type=Path, required=True)
    local.add_argument("--sha256")
    local.add_argument("--unpack", action="store_true", help="attempt read-only 7z extraction")
    local.add_argument("--decompile", action="store_true")

    args = parser.parse_args()
    try:
        if args.command == "fetch":
            download_verified(args.url, args.output, args.sha256)
            print(json.dumps({"path": str(args.output), "sha256": sha256_file(args.output)}))
            return 0

        if args.command == "full":
            download_verified(args.url, args.sample, args.sha256)
            report = analyze(args.sample, args.analysis_dir, args.sha256)
            if args.unpack:
                unpack_with_7z(args.sample, args.analysis_dir, report)
            if args.decompile:
                run_optional_decompilers(args.sample, args.analysis_dir, report)
            print(json.dumps(report, ensure_ascii=False, indent=2))
            return 0

        report = analyze(args.sample, args.analysis_dir, args.sha256)
        if args.unpack:
            unpack_with_7z(args.sample, args.analysis_dir, report)
        if args.decompile:
            run_optional_decompilers(args.sample, args.analysis_dir, report)
        print(json.dumps(report, ensure_ascii=False, indent=2))
        return 0
    except Exception as exc:
        print(f"error: {type(exc).__name__}: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
