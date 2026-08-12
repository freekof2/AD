import hashlib
import tempfile
import unittest
from pathlib import Path

from scripts.static_decompile import (
    ALLOWED_DOWNLOAD_HOSTS,
    extract_strings,
    parse_pe,
    sha256_file,
)


class StaticDecompileTests(unittest.TestCase):
    def test_non_pe_is_reported_without_execution(self):
        with tempfile.TemporaryDirectory() as directory:
            sample = Path(directory) / "sample.bin"
            sample.write_bytes(b"safe ASCII\x00A\x00S\x00A\x00R\x00")
            report = parse_pe(sample)
            self.assertFalse(report["is_pe"])
            self.assertEqual(report["error"], "missing MZ signature")

    def test_ascii_and_utf16_strings_are_collected(self):
        data = b"hello\x00world\x00" + "Electron".encode("utf-16le") + b"\x00\x00"
        strings = extract_strings(data)
        self.assertIn("hello", strings)
        self.assertIn("Electron", strings)

    def test_sha256_matches_file(self):
        with tempfile.TemporaryDirectory() as directory:
            sample = Path(directory) / "sample.bin"
            sample.write_bytes(b"static bytes")
            self.assertEqual(
                sha256_file(sample),
                hashlib.sha256(b"static bytes").hexdigest(),
            )

    def test_download_allow_list_is_https_only(self):
        self.assertIn("github.com", ALLOWED_DOWNLOAD_HOSTS)
        self.assertIn("release-assets.githubusercontent.com", ALLOWED_DOWNLOAD_HOSTS)
        self.assertNotIn("example.com", ALLOWED_DOWNLOAD_HOSTS)


if __name__ == "__main__":
    unittest.main()
