# SPDX-License-Identifier: MIT
# Copyright (c) 2026 The RadioFoundry Authors
# Copyright (c) 2026 MKME Media
"""Local firmware gate. Use the approved Linux build runtime with its pinned C-SKY compiler."""
import argparse
import hashlib
import json
import os
import pathlib
import subprocess
import sys
import shutil

ROOT = pathlib.Path(__file__).resolve().parents[2]


def validate(output, cross='/opt/xsuite/csky-elf/bin/csky-elf-', upstream=True):
    output = pathlib.Path(output).resolve()
    if output.exists() and any(output.iterdir()):
        raise RuntimeError('Use a new or verified empty task-owned output directory; existing output is preserved')
    output.mkdir(parents=True, exist_ok=True)
    node = shutil.which('node') or shutil.which('node.exe')
    if not node:
        raise RuntimeError('The established XTOC Node build runtime is required for canonical codec fixtures')
    generator = str(ROOT / 'tools/xsuite/generate-templates.mjs')
    if node.endswith('.exe') and sys.platform != 'win32':
        generator = subprocess.check_output(['wslpath', '-w', generator], text=True).strip()
    commands = [
        [node, generator, '--check'],
        [sys.executable, 'tools/i18n.py', '--check'],
        [sys.executable, 'tests/host/xsuite_packet_test.py', '--output', str(output / 'packet')],
        ['cc', '-std=gnu99', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', '-Icore', '-Ihal', '-Idrivers', '-DCONFIG_XSUITE=1',
         'tests/host/xsuiteRuntimeTest.c', 'core/xsuite/xsuite.c', 'core/xsuite/xsuite_packet.c', 'core/xsuite/xsuite_host.c',
         'core/debuglink/dl_fmt.c', 'core/aprs/ax25.c', '-o', str(output / 'xsuiteRuntimeTest')],
        [str(output / 'xsuiteRuntimeTest')],
        [sys.executable, 'tests/host/xsuiteInstallerTest.py', '--output', str(output / 'installer')],
        [sys.executable, 'tools/serial_loader.py', '--self-test'],
        ['make', '-C', 'tests/host', '-j4', 'xsuiteUiTest', 'PYTHON=' + sys.executable],
        [str(ROOT / 'tests/host/xsuiteUiTest')],
    ]
    if upstream:
        commands.append(['make', '-C', 'tests/host', '-j4', 'PYTHON=' + sys.executable])
    common = ['make', '-j4', 'BOARD=dm32', 'VARIANT=xsuite', 'CROSS=' + cross, 'RFOUNDRY_PYTHON=' + sys.executable]
    commands += [common + ['BUILD=' + str(output / 'on')], common + ['BUILD=' + str(output / 'on'), 'stack-check'],
                 common + ['CONFIG_XSUITE=n', 'BUILD=' + str(output / 'off')]]
    env = {**os.environ, 'PYTHONDONTWRITEBYTECODE': '1'}
    results = []
    for index, command in enumerate(commands):
        print('CHECK', index + 1, ' '.join(command), flush=True)
        log = output / ('%02d.log' % (index + 1))
        with log.open('wb') as target:
            completed = subprocess.run(command, cwd=ROOT, env=env, stdout=target, stderr=subprocess.STDOUT)
        results.append(dict(command=command, exit=completed.returncode, log=str(log)))
        if completed.returncode:
            (output / 'result.json').write_text(json.dumps(dict(passed=False, checks=results), indent=2) + '\n')
            raise RuntimeError('Local check failed; preserved log: ' + str(log))
    image = output / 'on/rfoundry_dm32.bin'
    result = dict(passed=True, source=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
                  image=str(image), sha256=hashlib.sha256(image.read_bytes()).hexdigest(), checks=results)
    (output / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=pathlib.Path, required=True)
    parser.add_argument('--cross', default='/opt/xsuite/csky-elf/bin/csky-elf-')
    args = parser.parse_args()
    print(json.dumps(validate(args.output, args.cross), indent=2))
