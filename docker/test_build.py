"""Exercise backend routing and Docker mounts without a daemon or game data."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parent.parent


class BuildTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name) / "repo with spaces, commas"
        for directory in ("docker", "build3ds", "bin"):
            (self.root / directory).mkdir(parents=True, exist_ok=True)
        for name in ("build.sh", "docker/build.sh", "docker/Dockerfile", "docker/requirements.txt"):
            shutil.copy2(ROOT / name, self.root / name)
        tool_script = self.root / "build3ds/build_cia_tools.sh"
        tool_script.write_text("#!/bin/sh\necho native-tools\n")
        tool_script.chmod(0o755)
        self.log = self.root / "calls.jsonl"
        docker = self.root / "bin/docker"
        docker.write_text(
            "#!" + shutil.which("python3") + "\n"
            "import json, os, sys\n"
            "with open(os.environ['MOCK_LOG'], 'a') as f:\n"
            "    f.write(json.dumps(sys.argv[1:]) + '\\n')\n"
            "if sys.argv[1:3] == ['image', 'inspect']:\n"
            "    sys.exit(int(os.environ.get('MOCK_IMAGE_MISSING', '0')))\n"
        )
        docker.chmod(0o755)
        self.rom = Path(self.tmp.name) / "dump (En,Fr).nds"
        self.rom.write_bytes(b"fake cartridge fixture")
        self.env = {**os.environ, "PATH": str(self.root / "bin") + os.pathsep + os.environ["PATH"],
                    "MOCK_LOG": str(self.log), "SM64DS_ROM": str(self.rom),
                    "SM64DS_BUILD_BACKEND": "docker"}

    def run_build(self, *args, **env):
        return subprocess.run(["bash", str(self.root / "build.sh"), *args], cwd=self.root,
                              env={**self.env, **env}, capture_output=True, text=True)

    def calls(self):
        return [json.loads(line) for line in self.log.read_text().splitlines()] if self.log.exists() else []

    def test_default_docker_mounts_external_rom_and_isolates_caches(self):
        result = self.run_build(JOBS="3")
        self.assertEqual(result.returncode, 0, result.stderr)
        run = self.calls()[-1]
        self.assertEqual(run[0], "run")
        self.assertIn(str(self.rom) + ":/rom/baserom.eu.nds:ro", run)
        self.assertIn(str(self.root) + ":/work", run)
        for directory in ("cmake", "user-build", "menu-compile", "tools-src", "tools-bin", "cia-build"):
            self.assertIn(f"{self.root}/build3ds/docker/{directory}:/work/build3ds/{directory}", run)
        self.assertIn("JOBS=3", run)
        self.assertEqual(run[-3:], ["./build.sh", "--native", "all"])
        self.assertFalse(any(call[0] == "build" for call in self.calls()))

    def test_missing_image_is_built_before_running(self):
        result = self.run_build("tools", MOCK_IMAGE_MISSING="1", SM64DS_ROM="/missing.nds")
        self.assertEqual(result.returncode, 0, result.stderr)
        calls = self.calls()
        self.assertEqual([call[0] for call in calls], ["info", "image", "build", "run"])
        self.assertEqual(calls[2][-1], str(self.root))
        self.assertFalse(any("/rom/" in arg for arg in calls[-1]))

    def test_missing_rom_fails_before_building_image(self):
        result = self.run_build(SM64DS_ROM="/missing.nds")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("put your own Europe dump", result.stderr)
        self.assertFalse(any(call[0] in ("run", "build") for call in self.calls()))

    def test_native_option_bypasses_docker(self):
        result = self.run_build("--native", "tools")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("native-tools", result.stdout)
        self.assertEqual(self.calls(), [])

    def test_help_and_invalid_command_do_not_start_docker(self):
        self.assertEqual(self.run_build("--help").returncode, 0)
        self.assertEqual(self.run_build("invalid").returncode, 2)
        self.assertEqual(self.run_build("all", "extra").returncode, 2)
        self.assertEqual(self.calls(), [])


if __name__ == "__main__":
    unittest.main()
