#!/usr/bin/env python3
"""Independent Azahar 2126.1.2 instances for FUNCTIONAL tests, never FPS evidence.

macOS frontend changes cwd to the bundle parent before SetUserPath(). Each new
instance therefore owns a copied bundle and a sibling portable user/ directory.
No HOME override, user profile/firmware copy, global process search or global kill.
The existing legacy SD lock is held shared/read-only to exclude run.py (which
uses an exclusive lock and terminates every matching Azahar at cleanup).
"""
import argparse
from contextlib import contextmanager
import ctypes
import errno
import fcntl
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import sys
import threading
import time
import uuid

from azahar_config import model_failure
from run_result import assess_run

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_APP = Path(os.environ.get('AZAHAR_APP', str(Path.home() / 'Downloads/azahar-macos-universal-2126.1.2/Azahar.app')))
GLOBAL_PROFILE = Path.home() / 'Library/Application Support/Azahar'
LEGACY_LOCK = GLOBAL_PROFILE / 'sdmc/sm64ds/.test.lock'


# Global emulator pool: every isolated run (all agents, all scripts importing
# this module) holds one of POOL_SIZE kernel file locks while its emulator is
# alive. Locks vanish with the holding process, so crashes never leak slots.
POOL_DIR = ROOT / 'build3ds/.emulator-pool'
POOL_SIZE = int(os.environ.get('SM64DS_EMULATOR_POOL', '4'))


@contextmanager
def emulator_slot(poll=1.0):
    POOL_DIR.mkdir(exist_ok=True)
    announced = False
    while True:
        for index in range(POOL_SIZE):
            handle = open(POOL_DIR / f'slot{index}.lock', 'a+')
            try:
                fcntl.flock(handle, fcntl.LOCK_EX | fcntl.LOCK_NB)
            except BlockingIOError:
                handle.close()
                continue
            try:
                handle.seek(0); handle.truncate()
                handle.write(f'{os.getpid()} {time.time():.0f} {" ".join(sys.argv)[:300]}\n'); handle.flush()
                yield index
            finally:
                handle.seek(0); handle.truncate(); handle.flush()
                fcntl.flock(handle, fcntl.LOCK_UN)
                handle.close()
            return
        if not announced:
            sys.stderr.write(f'emulator pool: all {POOL_SIZE} slots busy, waiting\n'); sys.stderr.flush()
            announced = True
        time.sleep(poll)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def inside(path, parent):
    return path == parent or parent in path.parents


def environment(items, model, save_name=None):
    values = {}
    for item in items:
        key, sep, value = item.partition('=')
        if not sep or len(item.encode()) > 254 or not re.fullmatch(r'[A-Za-z_][A-Za-z_0-9]*', key) or any(c in item for c in '\r\n\0'):
            raise ValueError('Expected a single-line KEY=VALUE')
        if key in values:
            raise ValueError('Duplicate environment key: ' + key)
        values[key] = value
    required = 'SM64DS_REQUIRE_3DS_MODEL'
    if required in values and values[required] != model:
        raise ValueError('Conflicting guest model requirement')
    values[required] = model
    if save_name is not None:
        if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._-]{0,127}', save_name):
            raise ValueError('Save name must be one filename')
        value = 'sdmc:/sm64ds/' + save_name
        if 'SM64DS_SAVE_PATH' in values and values['SM64DS_SAVE_PATH'] != value:
            raise ValueError('Conflicting save path')
        values['SM64DS_SAVE_PATH'] = value
    # All guest SD access resolves within this instance regardless of save name.
    return [key + '=' + value for key, value in values.items()]


def portable_config(user, model):
    # Fresh settings, not a copied personal Qt profile. Explicit custom paths
    # also defend against future portable-default changes. No CPU overclock.
    return f'''[Core]
cpu_clock_percentage=100
cpu_clock_percentage\\default=false
[System]
is_new_3ds={'true' if model == 'new' else 'false'}
is_new_3ds\\default=false
[Data Storage]
use_virtual_sd=true
use_virtual_sd\\default=false
use_custom_storage=true
use_custom_storage\\default=false
nand_directory={user / 'nand'}/
nand_directory\\default=false
sdmc_directory={user / 'sdmc'}/
sdmc_directory\\default=false
[Debugging]
use_gdbstub=false
use_gdbstub\\default=false
enable_rpc_server=false
enable_rpc_server\\default=false
[UI]
confirm_before_closing=false
confirm_before_closing\\default=false
'''


def check_bundle(app):
    executable = app / 'Contents/MacOS/azahar'
    if not executable.is_file() or not os.access(executable, os.X_OK):
        raise ValueError('Missing executable app bundle: ' + str(app))
    # Preserve framework-relative links; reject links escaping into the source
    # bundle or another writable global directory. No writable hard links.
    for path in app.rglob('*'):
        if path.is_symlink() and not inside(path.resolve(strict=True), app):
            raise ValueError('Bundle symlink escapes app: ' + str(path))
    return executable


def independent_copy(source, destination):
    # APFS copy-on-write clone: different inode and independent later writes,
    # unlike a hard link. Avoid three full 265MiB copies on a nearly full disk.
    if sys.platform == 'darwin':
        library = ctypes.CDLL('/usr/lib/libSystem.B.dylib', use_errno=True)
        clone = library.clonefile
        clone.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint32]
        clone.restype = ctypes.c_int
        if clone(os.fsencode(source), os.fsencode(destination), 0) == 0:
            return str(destination)
        code = ctypes.get_errno()
        if code not in (errno.EXDEV, errno.ENOTSUP, errno.EINVAL):
            raise OSError(code, os.strerror(code), str(destination))
    return shutil.copy2(source, destination)


def prepare(binary, instance, app, env, model, input_save=None, save_name=None):
    binary = binary.resolve(strict=True)
    instance = instance.resolve()
    app = app.resolve(strict=True)
    if any(c in str(instance) for c in '\r\n\0'):
        raise ValueError('Instance path must not contain control characters')
    if inside(instance, GLOBAL_PROFILE.resolve()) or inside(instance, app):
        raise ValueError('Instance must not be inside the personal profile or original app')
    elf = binary.with_suffix('.elf')
    if binary.suffix == '.3dsx' and elf.exists() and elf.stat().st_mtime_ns > binary.stat().st_mtime_ns:
        raise ValueError('Stale 3DSX: corresponding ELF is newer')
    check_bundle(app)
    overrides = environment(env, model, save_name)
    instance.mkdir(parents=True, exist_ok=False)  # Never reuse stale evidence.
    user = instance / 'user'
    for name in ('config', 'cache', 'sdmc/sm64ds', 'nand', 'sysdata', 'log'):
        (user / name).mkdir(parents=True, exist_ok=True)
    config = user / 'config/qt-config.ini'
    config.write_text(portable_config(user, model))
    sd = user / 'sdmc/sm64ds'
    (sd / 'env.txt').write_text('\n'.join(overrides) + '\n')
    (sd / 'autoexit').touch()
    if input_save:
        if not save_name:
            raise ValueError('Input save requires --save-name')
        shutil.copy2(input_save, sd / save_name)
    # A full independent copy preserves signed app/framework layout and dylibs.
    # Large and intentionally outside performance windows; retain for diagnosis.
    copied_app = instance / 'Azahar.app'
    shutil.copytree(app, copied_app, symlinks=True, copy_function=independent_copy)
    executable = check_bundle(copied_app)
    assert digest(executable) == digest(app / 'Contents/MacOS/azahar')
    plan = {'binary': str(binary), 'sha256': digest(binary), 'instance': str(instance),
            'app': str(copied_app), 'executable': str(executable), 'app_executable_sha256': digest(executable),
            'user_directory': str(user), 'sd_directory': str(sd), 'config': str(config),
            'config_initial_sha256': digest(config), 'requested_3ds_model': model,
            'environment_overrides': overrides, 'input_save_sha256': digest(input_save) if input_save else None,
            'save_name': save_name, 'performance_admissible': False, 'scope': 'functional only',
            'launch': [str(executable), str(binary)], 'cwd': str(instance),
            'isolation_strategy': 'macOS bundle-parent portable user; independent bundle copy',
            'legacy_lock': str(LEGACY_LOCK), 'stop_strategy': 'owned setsid group; Darwin orphan PID plus kernel start identity',
            'home_override': False, 'profile_or_firmware_copied': False}
    (instance / 'plan.json').write_text(json.dumps(plan, indent=2) + '\n')
    return plan


@contextmanager
def legacy_exclusion(path=LEGACY_LOCK):
    # Existing file only: no mkdir, creation or content/mtime changes at home.
    try:
        lock = path.open('rb')
    except FileNotFoundError:
        raise RuntimeError('Legacy lock missing; establish legacy coordination before using this runner')
    try:
        try:
            fcntl.flock(lock, fcntl.LOCK_SH | fcntl.LOCK_NB)
        except BlockingIOError:
            raise RuntimeError('Legacy run.py owns Azahar; isolated launch blocked')
        yield
    finally:
        lock.close()


def group_exists(group):
    try:
        os.killpg(group, 0)
        return True
    except ProcessLookupError:
        return False
    except PermissionError:
        # Darwin can report EPERM for a terminated but not yet reaped leader.
        # It is not proof of disappearance; stop_owned reaps before testing.
        return True


class BsdInfo(ctypes.Structure):
    # Exact Darwin sys/proc_info.h proc_bsdinfo (MAXCOMLEN=16).
    _fields_ = [(name, ctypes.c_uint32) for name in (
        'flags', 'status', 'xstatus', 'pid', 'ppid', 'uid', 'gid', 'ruid',
        'rgid', 'svuid', 'svgid', 'reserved')]+[
        ('comm', ctypes.c_char*16), ('name', ctypes.c_char*32)]+[
        (name, ctypes.c_uint32) for name in ('nfiles', 'pgid', 'jobc', 'tdev', 'tpgid', 'nice')]+[
        ('start_sec', ctypes.c_uint64), ('start_usec', ctypes.c_uint64)]


def process_identity(pid):
    library = ctypes.CDLL('/usr/lib/libproc.dylib', use_errno=True)
    info = BsdInfo()
    fn = library.proc_pidinfo
    fn.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_uint64, ctypes.c_void_p, ctypes.c_int]
    fn.restype = ctypes.c_int
    if fn(pid, 3, 0, ctypes.byref(info), ctypes.sizeof(info)) != ctypes.sizeof(info):
        return None
    return {'pid': info.pid, 'pgid': info.pgid, 'start_sec': info.start_sec, 'start_usec': info.start_usec}


class OwnedOrphan:
    """Known child handed off by helper; never waitpid() a non-child."""
    def __init__(self, identity):
        self.identity = identity
        self.pid = identity['pid']
        self.finished = False

    def validate(self):
        current = process_identity(self.pid)
        if current is not None and current != self.identity:
            raise RuntimeError('Owned emulator PID identity changed; refusing signal')
        return current

    def poll(self):
        if self.finished or self.validate() is None:
            self.finished = True
            return 0  # Exited; exact non-child exit status unavailable.
        return None

    def wait(self, timeout):
        deadline = time.monotonic()+timeout
        while self.poll() is None and time.monotonic()<deadline:
            time.sleep(.025)
        if self.poll() is None:
            raise subprocess.TimeoutExpired('owned emulator', timeout)
        return 0


def orphan_helper(executable, binary, cwd, log):
    ready_read, ready_write = os.pipe()
    pid = os.fork()
    if pid == 0:
        os.close(ready_read)
        os.setsid()
        identity = process_identity(os.getpid())
        if identity is None:
            os._exit(2)
        os.write(ready_write, json.dumps(identity).encode())
        os.close(ready_write)
        # Azahar's IsRunningFromTerminal() is getppid()!=1 and would show a
        # modal warning before BootGame. Wait for helper handoff, not merely
        # launch from Popen (which still leaves Python as the parent).
        deadline=time.monotonic()+5
        while os.getppid()!=1 and time.monotonic()<deadline:
            time.sleep(.005)
        if os.getppid()!=1:
            os._exit(3)
        fd=os.open(log,os.O_WRONLY|os.O_CREAT|os.O_APPEND,0o600)
        os.dup2(fd,1);os.dup2(fd,2);os.close(fd)
        os.write(1,('Isolated launch '+json.dumps({**identity,'ppid_before_exec':os.getppid()})+'\n').encode())
        os.chdir(cwd)
        os.execv(executable,[executable,binary])
        os._exit(4)
    os.close(ready_write)
    payload=os.read(ready_read,4096);os.close(ready_read)
    if not payload:
        raise RuntimeError('Orphan launch did not publish start identity')
    sys.stdout.write(payload.decode()+'\n');sys.stdout.flush()
    os._exit(0)


def launch_owned(plan, launcher):
    if sys.platform!='darwin':
        return subprocess.Popen(plan['launch'],cwd=plan['cwd'],stdout=launcher,
                                stderr=subprocess.STDOUT,start_new_session=True)
    handoff=subprocess.run([sys.executable,str(Path(__file__).resolve()),'--orphan-launch',
                            *plan['launch'],plan['cwd'],launcher.name],
                           stdout=subprocess.PIPE,stderr=launcher,text=True,check=True,timeout=10)
    process=OwnedOrphan(json.loads(handoff.stdout))
    if process.identity['pgid']!=process.pid:
        raise RuntimeError('Orphan did not own its session group')
    return process


def stop_owned(process, group, grace=2):
    if group != process.pid or group == os.getpgrp():
        raise RuntimeError('Refusing non-owned process group')
    for signum in (signal.SIGTERM, signal.SIGKILL):
        if isinstance(process, OwnedOrphan):
            process.validate()
        try:
            os.killpg(group, signum)
        except ProcessLookupError:
            break
        deadline = time.monotonic() + grace
        while time.monotonic() < deadline:
            process.poll()  # Reap owned leader; never search other Azahar PIDs.
            if not group_exists(group):
                break
            time.sleep(.025)
        if not group_exists(group):
            break
    process.wait(timeout=grace)
    if group_exists(group):
        raise RuntimeError('Owned process group did not terminate')


def execute(plan, timeout):
    instance = Path(plan['instance']);sd = Path(plan['sd_directory'])
    output = instance / 'result';output.mkdir()
    log = sd / 'log.txt';emulator_log = Path(plan['user_directory']) / 'log/azahar_log.txt'
    application = 'TIMEOUT';early_reason = None;process = None;group = None
    started = time.time();stopped = False
    def interrupted(signum, frame):
        raise KeyboardInterrupt
    old_term = signal.signal(signal.SIGTERM, interrupted) if threading.current_thread() is threading.main_thread() else None
    try:
        with emulator_slot(), legacy_exclusion(), (instance / 'launcher.log').open('w') as launcher:
            started = time.time()  # slot wait is not part of the run
            try:
                process = launch_owned(plan, launcher)
                group = process.pid
                (instance / 'process.json').write_text(json.dumps({'pid': process.pid, 'pgid': group,
                                                                  'start_identity': getattr(process,'identity',None),
                                                                  'started_unix': started}, indent=2) + '\n')
                deadline = time.monotonic() + timeout
                while time.monotonic() < deadline:
                    content = log.read_text(errors='replace') if log.exists() else ''
                    diagnostics = emulator_log.read_text(errors='replace') if emulator_log.exists() else ''
                    if re.search(r'Failed to load ROM \(Error \d+\)|Undefined instruction|Segmentation fault', diagnostics, re.I):
                        application, early_reason = 'FAIL', 'emulator_load_or_crash_failure';break
                    if re.search(r'^FATAL:', content, re.M):
                        application, early_reason = 'FAIL', 'application_fatal';break
                    matches = re.findall(r'^(PASS|FAIL)$', content, re.M)
                    if matches:
                        application = matches[-1];break
                    if process.poll() is not None:
                        application, early_reason = 'FAIL', 'emulator_exited_before_verdict';break
                    time.sleep(.1)
            except KeyboardInterrupt:
                application, early_reason = 'FAIL', 'interrupted'
            finally:
                if process:
                    stop_owned(process, group)
                    stopped = True
    finally:
        if old_term is not None:
            signal.signal(signal.SIGTERM, old_term)
    content = log.read_text(errors='replace') if log.exists() else '(no log)'
    fresh = emulator_log.exists() and emulator_log.stat().st_mtime >= started
    diagnostics = emulator_log.read_text(errors='replace') if fresh else ''
    verdict, reason, unmapped = assess_run(application, diagnostics, fresh)
    model_reason = model_failure(content, plan['requested_3ds_model'])
    if verdict == 'PASS' and model_reason:
        verdict, reason = 'FAIL', model_reason
    if early_reason:
        reason = early_reason
    (output / 'log.txt').write_text(content)
    (output / 'azahar-log.txt').write_text(diagnostics)
    for path in [*sd.glob('*.bmp'), *sd.glob('stage-frame-*-camera.json')]:
        if path.stat().st_mtime >= started:
            shutil.copy2(path, output / path.name)
    if plan['save_name'] and (sd / plan['save_name']).exists():
        shutil.copy2(sd / plan['save_name'], output / 'after.sav')
    result = {**plan, 'result': verdict, 'application_result': application, 'failure_reason': reason,
              'model_verification_failure': model_reason, 'unmapped_access_count': unmapped,
              'emulator_log_fresh': fresh, 'owned_process_group_stopped': stopped,
              'pid': process.pid if process else None, 'pgid': group, 'started_unix': started,
              'finished_unix': time.time(), 'performance_admissible': False,
              'global_settings_modified': False}
    (output / 'run.json').write_text(json.dumps(result, indent=2) + '\n')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path)
    parser.add_argument('--instance', type=Path, help='New private directory (must not exist)')
    parser.add_argument('--app', type=Path, default=DEFAULT_APP)
    parser.add_argument('--timeout', type=float, default=180)
    parser.add_argument('--3ds-model', dest='model', choices=['old', 'new'], default='old')
    parser.add_argument('--env', action='append', default=[])
    parser.add_argument('--input-save', type=Path)
    parser.add_argument('--save-name', default='game.sav')
    parser.add_argument('--prepare-only', action='store_true', help='Copy/configure and write plan, never launch')
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error('Timeout must be positive')
    instance = args.instance or ROOT / 'build3ds/validation/isolated-runs' / uuid.uuid4().hex
    try:
        plan = prepare(args.binary, instance, args.app, args.env, args.model,
                       args.input_save.resolve(strict=True) if args.input_save else None, args.save_name)
        if args.prepare_only:
            print(json.dumps(plan, indent=2));return 0
        result = execute(plan, args.timeout)
        print('RESULT:', result['result'], '\nINSTANCE:', plan['instance'])
        return 0 if result['result'] == 'PASS' else 1
    except (ValueError, RuntimeError, OSError) as error:
        parser.exit(2, str(error) + '\n')


if __name__ == '__main__':
    if len(sys.argv)==6 and sys.argv[1]=='--orphan-launch':
        orphan_helper(*sys.argv[2:])
    raise SystemExit(main())
