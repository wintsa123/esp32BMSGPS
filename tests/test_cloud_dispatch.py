import base64
import importlib.util
import json
import unittest
from pathlib import Path
from unittest import mock


SCRIPT = Path(__file__).resolve().parents[1] / "scripts" / "dispatch-cloud-build.py"
SPEC = importlib.util.spec_from_file_location("cloud_dispatch", SCRIPT)
CLOUD_DISPATCH = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(CLOUD_DISPATCH)


class CloudDispatchTest(unittest.TestCase):
    def test_github_ssh_origin(self):
        self.assertEqual(
            CLOUD_DISPATCH.github_repository("git@github.com:wintsa123/esp32BMSGPS.git"),
            "wintsa123/esp32BMSGPS",
        )

    def test_non_github_origin_is_rejected(self):
        with self.assertRaises(CLOUD_DISPATCH.DispatchError):
            CLOUD_DISPATCH.github_repository("git@gitlab.com:wintsa123/esp32BMSGPS.git")

    def test_pushed_branch_requires_matching_head(self):
        values = {
            ("rev-parse", "--abbrev-ref", "HEAD"): "main",
            ("rev-parse", "HEAD"): "a" * 40,
            ("ls-remote", "--heads", "origin", "refs/heads/main"): "b" * 40 + "\trefs/heads/main",
        }
        with mock.patch.object(CLOUD_DISPATCH, "git_output", side_effect=lambda arguments: values[tuple(arguments)]):
            with self.assertRaises(CLOUD_DISPATCH.DispatchError):
                CLOUD_DISPATCH.pushed_branch()

    def test_pushed_branch_returns_matching_head(self):
        head = "a" * 40
        values = {
            ("rev-parse", "--abbrev-ref", "HEAD"): "main",
            ("rev-parse", "HEAD"): head,
            ("ls-remote", "--heads", "origin", "refs/heads/main"): head + "\trefs/heads/main",
        }
        with mock.patch.object(CLOUD_DISPATCH, "git_output", side_effect=lambda arguments: values[tuple(arguments)]):
            self.assertEqual(CLOUD_DISPATCH.pushed_branch(), ("main", head))

    def test_dispatch_request_contains_only_encoded_configuration(self):
        configuration = b"SCHEMA_VERSION=1\nPROFILE=golden\n"
        request = CLOUD_DISPATCH.dispatch_request("wintsa123/esp32BMSGPS", "main", configuration, "secret-token")
        payload = json.loads(request.data)
        self.assertEqual(payload["ref"], "main")
        self.assertEqual(base64.b64decode(payload["inputs"]["firmware_env_base64"]), configuration)
        self.assertNotIn("secret-token", request.data.decode("utf-8"))

    def test_successful_dispatch_requires_204(self):
        request = CLOUD_DISPATCH.dispatch_request("wintsa123/esp32BMSGPS", "main", b"x", "secret-token")

        class Response:
            status = 204

            def __enter__(self):
                return self

            def __exit__(self, *_):
                return False

        CLOUD_DISPATCH.send_dispatch(request, opener=lambda *_args, **_kwargs: Response())


if __name__ == "__main__":
    unittest.main()
