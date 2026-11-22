# SPDX-License-Identifier: MIT
# Copyright (c) 2026 The RadioFoundry Authors
# Copyright (c) 2026 MKME Media
"""Installer preflight/backup/identity/codec tests. Hardware services are deterministic stand-ins."""
import argparse
import hashlib
import importlib.util
import json
import pathlib
import types
import unittest
from unittest.mock import patch
import zlib

ROOT = pathlib.Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('xsuite_installer', ROOT / 'tools/xsuite/installer.py')
installer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(installer)
parser = argparse.ArgumentParser()
parser.add_argument('--output', type=pathlib.Path, required=True)
args = parser.parse_args()
OUT = args.output
if OUT.exists() and any(OUT.iterdir()):
    raise SystemExit('Use a new or verified empty task-owned test output directory')
OUT.mkdir(parents=True, exist_ok=True)
BACKUP = OUT / 'backup'
BACKUP.mkdir()
RELEASE = OUT / 'release'
RELEASE.mkdir()
IMAGE = RELEASE / 'xsuite-dm32uv.bin'
IMAGE.write_bytes(b'validated fixture application')
MANIFEST = dict(board='dm32', variant='xsuite', image=IMAGE.name, sha256=hashlib.sha256(IMAGE.read_bytes()).hexdigest())
(RELEASE / 'xsuite-dm32uv.json').write_text(json.dumps(MANIFEST))
FILES = {}
for name, size in [('program-memory.bin', 0x100000), ('data-memory.bin', 0x1000000)]:
    value = bytes(size)
    (BACKUP / name).write_bytes(value)
    FILES[name] = dict(bytes=size, sha256=hashlib.sha256(value).hexdigest())
META = dict(format='xsuite-radio-backup/1', board='dm32', complete=True, files=FILES, source={'board': 'dm32'})


class Board:
    rom_base = 0
    app_base = 0
    codec_slot = (0, 16)
    def options_for_name(self, name): return {} if name == 'dm32' else None
    def codec_present(self, value): return value == 'verified-own-codec'


class Radio:
    def __init__(self): self.writes = 0; self.reset = 0; self.mismatch = False
    def sums(self, memory, start, count):
        value = zlib.crc32(bytes(4096)) & 0xffffffff
        return [(value ^ (1 if self.mismatch else 0), 0)] * count
    def write_image(self, image, progress=None): self.writes += 1; return 1, 2
    def reset_chip(self): self.reset += 1


class Tests(unittest.TestCase):
    def setUp(self):
        (BACKUP / 'backup.json').write_text(json.dumps(META))
        self.radio = Radio()
        self.sf = types.SimpleNamespace(
            tool=lambda name: types.SimpleNamespace(parse_slot=lambda value: None) if name == 'hrc7000_codec' else types.SimpleNamespace(check_image=lambda *a: None, MODELS={'dm32': {}}),
            slot_from_file=lambda *a: (b'own codec', 'verified'), contiguous=lambda board, image, slot: image + slot,
            wait_for_rfoundry=lambda port: dict(board='dm32', codec='verified-own-codec'), probe=lambda port: {'board': 'dm32'})
        self.monitor = unittest.mock.Mock(return_value=(self.radio, types.SimpleNamespace(close=lambda: None), {'board': 'dm32'}))
        self.patches = [patch.object(installer, 'RESOURCE_ROOT', OUT), patch.object(installer, '_libraries', return_value=(Board(), self.sf)),
                        patch.object(installer, '_monitor', self.monitor), patch.object(installer, '_initialize_epoch')]
        for p in self.patches: p.start()
    def tearDown(self):
        for p in reversed(self.patches): p.stop()
    def install(self): return installer.install('SIMULATED-NO-COM', IMAGE, BACKUP)
    def test_update_installs_only_after_backup_codec_and_identity_checks(self):
        result = self.install()
        self.assertTrue(result['installed']); self.assertEqual(self.radio.writes, 1); self.assertEqual(self.radio.reset, 1)
    def test_wrong_image_refused_before_open(self):
        with patch.object(installer, 'firmware_info', return_value={**MANIFEST, 'sha256': 'invalid'}):
            with self.assertRaises(RuntimeError): self.install()
        self.monitor.assert_not_called(); self.assertEqual(self.radio.writes, 0)
    def test_corrupt_or_wrong_model_backup_refused_before_open(self):
        for changes in [dict(board='other'), dict(complete=False), dict(files={})]:
            (BACKUP / 'backup.json').write_text(json.dumps({**META, **changes}))
            with self.assertRaises(RuntimeError): self.install()
        self.monitor.assert_not_called(); self.assertEqual(self.radio.writes, 0)
    def test_unverified_own_codec_refused_before_open(self):
        self.sf.slot_from_file = unittest.mock.Mock(side_effect=RuntimeError('unverified codec'))
        with self.assertRaises(RuntimeError): self.install()
        self.monitor.assert_not_called(); self.assertEqual(self.radio.writes, 0)
    def test_different_connected_radio_refused_before_erase(self):
        self.radio.mismatch = True
        with self.assertRaisesRegex(RuntimeError, 'does not match'): self.install()
        self.assertEqual(self.radio.writes, 0); self.assertEqual(self.radio.reset, 0)
    def test_initial_stock_requires_source_bound_channel_conversion(self):
        (BACKUP / 'backup.json').write_text(json.dumps({**META, 'source': {'mode': 'boot-rom'}}))
        with self.assertRaises((FileNotFoundError, RuntimeError)): self.install()
        self.monitor.assert_not_called(); self.assertEqual(self.radio.writes, 0)
    def test_initial_stock_invalid_conversion_refused_before_open(self):
        (BACKUP / 'backup.json').write_text(json.dumps({**META, 'source': {'mode': 'boot-rom'}}))
        (BACKUP / 'converted-codeplug.bin').write_bytes(b'invalid')
        (BACKUP / 'converted-codeplug.json').write_text(json.dumps(dict(sourceSha256=FILES['data-memory.bin']['sha256'], sha256='invalid', bytes=7)))
        with self.assertRaises(RuntimeError): self.install()
        self.monitor.assert_not_called(); self.assertEqual(self.radio.writes, 0)
    def test_manifest_path_escape_and_hash_refused(self):
        for altered in [dict(image='../backup/program-memory.bin'), dict(sha256='invalid'), dict(board='other')]:
            (RELEASE / 'xsuite-dm32uv.json').write_text(json.dumps({**MANIFEST, **altered}))
            with self.assertRaises(RuntimeError): installer.firmware_info(OUT)
        (RELEASE / 'xsuite-dm32uv.json').write_text(json.dumps(MANIFEST))
    def test_restore_preserves_calibration_and_verifies_same_radio(self):
        result = installer.restore('SIMULATED-NO-COM', BACKUP)
        self.assertTrue(result['restoredApplication']); self.assertEqual(self.radio.writes, 1)


if __name__ == '__main__':
    unittest.main(argv=['xsuiteInstallerTest'], verbosity=2)
