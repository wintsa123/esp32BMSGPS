import base64
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("web_package", ROOT / "scripts/package-web-update.py")
PACKAGE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PACKAGE)


class WebUpdatePackageTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.dist = self.root / "dist"
        self.dist.mkdir()
        (self.dist / "index.html").write_text('<script src="/assets/main.js"></script>')
        (self.dist / "assets").mkdir()
        (self.dist / "assets/main.js").write_text("console.log('real fixture')")
        (self.dist / "sw.js").write_text("// worker")
        (self.dist / "web-version.json").write_text('{"version":"abc123"}')
        (self.dist / "apk").mkdir()
        (self.dist / "apk/应用.apk").write_bytes(b"not a Web resource")
        (self.dist / "firmware").mkdir()
        (self.dist / "firmware/esp32.bin").write_bytes(b"firmware")
        self.key = self.root / "private.pem"
        subprocess.run(["openssl", "genpkey", "-algorithm", "RSA", "-pkeyopt", "rsa_keygen_bits:2048", "-out", str(self.key)], check=True, capture_output=True)

    def package(self, output="release", **options):
        return PACKAGE.package_web(self.dist, self.root / output, "https://updates.example.test/stable", self.key,
                                   "abc123", options.pop("sequence", 5), 3, **options)

    def test_host_configuration_is_not_a_web_resource(self):
        for name in ("_headers", "_redirects"):
            (self.dist / name).write_text("host configuration")
        envelope = self.package()
        self.assertTrue({"_headers", "_redirects"}.isdisjoint(f["path"] for f in json.loads(envelope["payload"])["files"]))

    def test_signed_files_limits_archive_and_reproducibility(self):
        envelope = self.package()
        release = self.root / "release"
        payload = json.loads(envelope["payload"])
        self.assertEqual(payload["schema"], 1)
        self.assertEqual(payload["sequence"], 5)
        self.assertEqual({f["path"] for f in payload["files"]}, {"index.html", "assets/main.js", "sw.js", "web-version.json"})
        for entry in payload["files"]:
            data = (release / "web-5" / entry["path"]).read_bytes()
            self.assertEqual(entry["sha256"], hashlib.sha256(data).hexdigest())
            self.assertEqual(entry["size"], len(data))
            self.assertEqual(entry["url"], "https://updates.example.test/stable/web-5/" + entry["path"])
        config = json.loads((release / "dashboard-update.json").read_text())
        public = self.root / "public.der"
        public.write_bytes(base64.b64decode(config["publicKey"]))
        signature = self.root / "signature.bin"
        signature.write_bytes(base64.b64decode(envelope["signature"]))
        verify = ["openssl", "dgst", "-sha256", "-keyform", "DER", "-verify", str(public), "-signature", str(signature)]
        self.assertEqual(subprocess.run(verify, input=envelope["payload"].encode(), capture_output=True).returncode, 0)
        self.assertNotEqual(subprocess.run(verify, input=(envelope["payload"] + " ").encode(), capture_output=True).returncode, 0)
        self.package("same")
        self.assertEqual((release / "manifest.json").read_bytes(), (self.root / "same/manifest.json").read_bytes())
        self.assertEqual((release / "web-5.zip").read_bytes(), (self.root / "same/web-5.zip").read_bytes())
        with zipfile.ZipFile(release / "web-5.zip") as archive:
            self.assertNotIn("dashboard-update.json", archive.namelist())
            self.assertIn("index.html", archive.namelist())
        self.assertNotIn(self.key.read_text(), (release / "manifest.json").read_text())
        with self.assertRaises(ValueError):
            self.package()

    def test_reject_symlinks_trust_config_bad_paths_and_limits(self):
        for name in ["dashboard-update.json", "assets/unsafe%.js", "中文.js", ".hidden", "assets/.metadata"]:
            path = self.dist / name
            path.write_text("bad")
            with self.assertRaises(ValueError):
                self.package()
            path.unlink()
        linked = self.dist / "linked.js"
        linked.symlink_to(self.key)
        with self.assertRaises(ValueError):
            self.package()
        linked.unlink()
        large = self.dist / "large.bin"
        with large.open("wb") as output:
            output.truncate(PACKAGE.MAX_FILE + 1)
        with self.assertRaises(ValueError):
            self.package()
        large.unlink()
        (self.dist / "index.html").unlink()
        with self.assertRaises(ValueError):
            self.package()
        self.assertFalse((self.root / "release").exists())

    def test_reject_total_size_file_count_and_small_rsa_key(self):
        for index in range(5):
            with (self.dist / f"large-{index}.bin").open("wb") as output:
                output.truncate(PACKAGE.MAX_FILE)
        with self.assertRaises(ValueError):
            self.package()
        for path in self.dist.glob("large-*"):
            path.unlink()
        for index in range(PACKAGE.MAX_FILES):
            (self.dist / f"file-{index}").write_bytes(b"x")
        with self.assertRaises(ValueError):
            self.package()
        for path in self.dist.glob("file-*"):
            path.unlink()
        subprocess.run(["openssl", "genpkey", "-algorithm", "RSA", "-pkeyopt", "rsa_keygen_bits:1024", "-out", str(self.key)], check=True, capture_output=True)
        with self.assertRaises(ValueError):
            self.package()
        self.assertFalse((self.root / "release").exists())

    def test_native_manifest_boundaries(self):
        for sequence, minimum, version in [
                (PACKAGE.MAX_INTEGER + 1, 3, "v"), (1, PACKAGE.MAX_INTEGER + 1, "v"),
                (1, 0, "v"), (1.5, 3, "v"), (True, 3, "v"), (1, 3, "v" * 101), (1, 3, "🚲" * 51)]:
            with self.assertRaises(ValueError):
                PACKAGE.package_web(self.dist, self.root / "release", "https://updates.example.test", self.key,
                                    version, sequence, minimum)
        empty = self.dist / "empty.js"
        empty.touch()
        with self.assertRaises(ValueError):
            self.package()
        empty.unlink()
        nested = self.dist / "dashboard-update.json/resource.js"
        nested.parent.mkdir()
        nested.write_text("private trust path")
        with self.assertRaises(ValueError):
            self.package()
        nested.unlink()
        nested.parent.rmdir()
        long_path = self.dist / ("a" * 120) / ("b" * 121)
        long_path.parent.mkdir()
        long_path.write_text("too long")
        with self.assertRaises(ValueError):
            self.package()
        long_path.unlink()
        PACKAGE.package_web(self.dist, self.root / "release", "https://updates.example.test", self.key,
                            "v" * 100, PACKAGE.MAX_INTEGER, PACKAGE.MAX_INTEGER)

    def test_reject_unsafe_urls_sequences_and_failed_signing(self):
        for url in ["http://example.test", "https://user@example.test", "https://example.test/#fragment", "https://example.test/?query", "https://example.test/../stable", "https://example.test/%2e", "https://example.test:bad"]:
            with self.assertRaises(ValueError):
                PACKAGE.https_base(url)
        for sequence in [0, -1]:
            with self.assertRaises(ValueError):
                self.package(sequence=sequence)
        with self.assertRaises(ValueError):
            self.package(bundled_sequence=5)
        self.key.write_text("not a signing key")
        with self.assertRaises(subprocess.CalledProcessError):
            self.package()
        self.assertFalse((self.root / "release").exists())


if __name__ == "__main__":
    unittest.main()
