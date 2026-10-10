"""Portable filesystem/lock/process-group isolation without starting Azahar."""
import fcntl
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0,str(Path(__file__).resolve().parent))
import run_isolated as runner


class IsolationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.app = self.root / 'source/Azahar.app'
        executable = self.app / 'Contents/MacOS/azahar'
        executable.parent.mkdir(parents=True)
        executable.write_text('#!/bin/sh\nexit 0\n');executable.chmod(0o755)
        framework = self.app / 'Contents/Frameworks/Dummy.framework'
        (framework / 'Versions/A').mkdir(parents=True)
        (framework / 'Versions/A/dummy').write_bytes(b'framework')
        (framework / 'Versions/Current').symlink_to('A')
        (framework / 'dummy').symlink_to('Versions/Current/dummy')
        self.binary = self.root / 'smoke.3dsx';self.binary.write_bytes(b'3dsx')

    def tearDown(self):
        self.temp.cleanup()

    def prepare(self, name):
        return runner.prepare(self.binary, self.root / name, self.app, ['INSTANCE='+name], 'old', save_name='game.sav')

    def test_independent_bundle_user_settings_and_files(self):
        original = runner.digest(self.app / 'Contents/MacOS/azahar')
        a,b = self.prepare('a'),self.prepare('b')
        self.assertNotEqual(a['executable'],b['executable'])
        self.assertNotEqual(a['user_directory'],b['user_directory'])
        self.assertNotEqual(os.stat(a['executable']).st_ino, os.stat(b['executable']).st_ino)
        for plan in (a,b):
            cfg=Path(plan['config']).read_text()
            self.assertIn('use_custom_storage=true',cfg)
            self.assertIn(plan['user_directory']+'/sdmc/',cfg)
            self.assertNotIn(str(runner.GLOBAL_PROFILE),cfg)
            self.assertFalse(plan['home_override'])
            self.assertFalse(plan['performance_admissible'])
        sa,sb=Path(a['sd_directory']),Path(b['sd_directory'])
        (sa/'same.bmp').write_bytes(b'A');(sb/'same.bmp').write_bytes(b'B')
        (sa/'game.sav').write_bytes(b'saveA');(sb/'game.sav').write_bytes(b'saveB')
        self.assertEqual((sa/'same.bmp').read_bytes(),b'A')
        self.assertEqual((sb/'game.sav').read_bytes(),b'saveB')
        Path(a['executable']).write_bytes(b'private executable mutation')
        self.assertEqual(runner.digest(Path(b['executable'])),original)
        self.assertEqual(runner.digest(self.app/'Contents/MacOS/azahar'),original)

    def test_refuse_stale_directory_and_bad_environment(self):
        self.prepare('a')
        with self.assertRaises(FileExistsError):self.prepare('a')
        for item in ['bad','A=1\nB=2','A=\0','1A=x']:
            with self.assertRaises(ValueError):runner.environment([item],'old')
        with self.assertRaises(ValueError):runner.environment(['SM64DS_REQUIRE_3DS_MODEL=new'],'old')
        with self.assertRaises(ValueError):runner.environment([], 'old', '../escape')

    def test_no_global_profile_and_no_escaping_symlink(self):
        with self.assertRaises(ValueError):runner.prepare(self.binary,runner.GLOBAL_PROFILE/'instance',self.app,[],'old')
        (self.app/'Contents/escape').symlink_to(self.binary)
        with self.assertRaises(ValueError):runner.check_bundle(self.app)

    def test_save_input_is_copied_not_shared(self):
        save=self.root/'fixture.sav';save.write_bytes(b'original')
        p=runner.prepare(self.binary,self.root/'a',self.app,[],'old',save,'private.sav')
        copied=Path(p['sd_directory'])/'private.sav';copied.write_bytes(b'new')
        self.assertEqual(save.read_bytes(),b'original')

    def test_shared_isolated_locks_block_legacy_exclusive(self):
        path=self.root/'legacy.lock';path.write_bytes(b'keep');before=path.stat().st_mtime_ns
        with runner.legacy_exclusion(path),runner.legacy_exclusion(path),path.open('rb') as legacy:
            with self.assertRaises(BlockingIOError):fcntl.flock(legacy,fcntl.LOCK_EX|fcntl.LOCK_NB)
        with path.open('rb') as legacy:
            fcntl.flock(legacy,fcntl.LOCK_EX|fcntl.LOCK_NB)
            with self.assertRaises(RuntimeError):
                with runner.legacy_exclusion(path):pass
        self.assertEqual(path.read_bytes(),b'keep');self.assertEqual(path.stat().st_mtime_ns,before)
        with self.assertRaises(RuntimeError):
            with runner.legacy_exclusion(self.root/'absent'):pass

    def test_owned_group_stop_never_stops_other_instance(self):
        a=subprocess.Popen([sys.executable,'-c','import time;time.sleep(60)'],start_new_session=True)
        b=subprocess.Popen([sys.executable,'-c','import time;time.sleep(60)'],start_new_session=True)
        try:
            runner.stop_owned(a,a.pid,grace=.5)
            self.assertIsNone(b.poll());self.assertTrue(runner.group_exists(b.pid))
            with self.assertRaises(RuntimeError):runner.stop_owned(b,os.getpgrp())
        finally:
            runner.stop_owned(b,b.pid,grace=.5)

    def test_orphan_identity_change_refuses_signal(self):
        identity={'pid':12345,'pgid':12345,'start_sec':100,'start_usec':1}
        orphan=runner.OwnedOrphan(identity)
        with patch.object(runner,'process_identity',return_value={**identity,'start_usec':2}),patch.object(runner.os,'killpg') as kill:
            with self.assertRaises(RuntimeError):runner.stop_owned(orphan,12345)
            kill.assert_not_called()

    def test_run_assessment_and_output_are_instance_local(self):
        plan=self.prepare('a');sd=Path(plan['sd_directory']);user=Path(plan['user_directory'])
        # Stand-in executable proves runner lifecycle/output; not an emulator.
        executable=Path(plan['executable'])
        executable.write_text('#!'+sys.executable+'\nfrom pathlib import Path\nimport time\n'
            'p=Path.cwd()/"user"\n(p/"log/azahar_log.txt").write_text("fresh diagnostic")\n'
            '(p/"sdmc/sm64ds/log.txt").write_text("CPU mode: model_query=00000000 new3ds=0 speedup_requested=0\\nPASS\\n")\n'
            '(p/"sdmc/sm64ds/same.bmp").write_bytes(b"capture")\n'
            'time.sleep(10)\n')
        lock=self.root/'legacy.lock';lock.touch()
        with patch.object(runner,'legacy_exclusion',lambda: runner_legacy(lock)):
            result=runner.execute(plan,3)
        self.assertEqual(result['result'],'PASS');self.assertTrue(result['owned_process_group_stopped'])
        self.assertEqual((Path(plan['instance'])/'result/same.bmp').read_bytes(),b'capture')
        self.assertEqual(json.loads((Path(plan['instance'])/'result/run.json').read_text())['instance'],plan['instance'])


runner_legacy=runner.legacy_exclusion
if __name__=='__main__':unittest.main()
