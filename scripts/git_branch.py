"""
PlatformIO pre-build script: compute CROSSPOINT_VERSION for the environments
whose version cannot be a literal in the ini.

  default          1.1.0-dev-<branch>-<sha>   (git state)
  gh_release_rc    1.5.0-BNY-rc+<hash>        (CROSSPOINT_RC_HASH)

The RC env is here rather than in the ini because of B-004. It used to read
  -DCROSSPOINT_VERSION=\"${crosspoint.version}-rc+${sysenv.CROSSPOINT_RC_HASH}\"
and interpolating a sysenv value into build_flags makes the RESOLVED CONFIG
depend on that variable -- so setting or unsetting it changed
.pio/build/project.checksum, and the next `pio run` cleaned EVERY environment's
build directory, not just this one. Measured: a stamped gh_release_rc build
deleted .pio/build/simulator/program, and a later headless simulator run died
with "no such file or directory". A pre-script reads the same variable without
the ini text ever changing, so the checksum is stable.

Doing it in Python also lets the value be CHECKED, which an interpolation could
not. An unset variable used to stamp a trailing "+" and ship an unidentifiable
build (B-006); it now says so loudly and stamps a marker you can grep for.

The other release environments set a literal version in the ini and are
unaffected.
"""

import configparser
import os
import subprocess
import sys


def warn(msg):
    print(f'WARNING [git_branch.py]: {msg}', file=sys.stderr)


def run_git_value(project_dir, args, label):
    try:
        value = subprocess.check_output(
            ['git', *args],
            text=True, stderr=subprocess.PIPE, cwd=project_dir
        ).strip()
        # Strip characters that would break a C string literal
        return ''.join(c for c in value if c not in '"\\')
    except FileNotFoundError:
        warn(f'git not found on PATH; {label} suffix will be "unknown"')
        return 'unknown'
    except subprocess.CalledProcessError as e:
        warn(
            f'git command failed (exit {e.returncode}): '
            f'{e.stderr.strip()}; {label} suffix will be "unknown"'
        )
        return 'unknown'
    except OSError as e:
        warn(
            f'OS error reading git {label}: {e}; '
            f'{label} suffix will be "unknown"'
        )
        return 'unknown'
    except Exception as e:  # pylint: disable=broad-exception-caught
        warn(
            f'Unexpected error reading git {label}: {e}; '
            f'{label} suffix will be "unknown"'
        )
        return 'unknown'


def get_git_branch(project_dir):
    branch = run_git_value(
        project_dir, ['rev-parse', '--abbrev-ref', 'HEAD'], 'branch'
    )
    # Detached HEAD has no branch name.
    if branch == 'HEAD':
        return 'detached'
    return branch


def get_git_short_sha(project_dir):
    return run_git_value(
        project_dir, ['rev-parse', '--short', 'HEAD'], 'short SHA'
    )


def get_base_version(project_dir):
    ini_path = os.path.join(project_dir, 'platformio.ini')
    if not os.path.isfile(ini_path):
        warn(f'platformio.ini not found at {ini_path}; base version will be "0.0.0"')
        return '0.0.0'
    config = configparser.ConfigParser()
    config.read(ini_path)
    if not config.has_option('crosspoint', 'version'):
        warn('No [crosspoint] version in platformio.ini; base version will be "0.0.0"')
        return '0.0.0'
    return config.get('crosspoint', 'version')


def get_rc_hash():
    """The RC stamp, or a loud marker when the variable is unset.

    Empty is the dangerous case: it stamps a trailing '+' that looks like a
    truncated hash, feeds the OTA version comparison, and makes the running
    build unidentifiable after the fact (B-006). Better to be obviously wrong
    than subtly wrong, so an unset variable is named as such.
    """
    value = (os.environ.get('CROSSPOINT_RC_HASH') or '').strip()
    if not value:
        warn(
            'CROSSPOINT_RC_HASH is not set. Stamping "unset" so the build is '
            'still identifiable -- do NOT ship this. Re-run as: '
            'CROSSPOINT_RC_HASH=<short-sha> pio run -e gh_release_rc'
        )
        return 'unset'
    return ''.join(c for c in value if c not in '"\\')


def inject_version(env):
    # Envs whose version is a literal in the ini are left alone.
    pioenv = env['PIOENV']
    if pioenv not in ('default', 'gh_release_rc'):
        return

    project_dir = env['PROJECT_DIR']
    base_version = get_base_version(project_dir)

    if pioenv == 'gh_release_rc':
        version_string = f'{base_version}-rc+{get_rc_hash()}'
    else:
        branch = get_git_branch(project_dir)
        short_sha = get_git_short_sha(project_dir)
        version_string = f'{base_version}-dev-{branch}-{short_sha}'

    env.Append(CPPDEFINES=[('CROSSPOINT_VERSION', f'\\"{version_string}\\"')])
    print(f'CrossPoint build version: {version_string}')


# PlatformIO/SCons entry point — Import and env are SCons builtins injected at runtime.
# When run directly with Python (e.g. for validation), a lightweight fake env is used
# so the git/version logic can be exercised without a full build.
try:
    Import('env')           # noqa: F821  # type: ignore[name-defined]
    inject_version(env)     # noqa: F821  # type: ignore[name-defined]
except NameError:
    class _Env(dict):
        def Append(self, **_): pass

    _project_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    inject_version(_Env({'PIOENV': 'default', 'PROJECT_DIR': _project_dir}))
