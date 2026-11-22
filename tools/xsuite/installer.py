# SPDX-License-Identifier: MIT
# Copyright (c) 2026 The RadioFoundry Authors
# Copyright (c) 2026 MKME Media
"""Offline application-facing installer using RadioFoundry's existing backup/codec/flash implementations."""
import hashlib
import importlib.util
import json
import pathlib
import secrets
import struct
import sys
import time
import zlib

RESOURCE_ROOT = pathlib.Path(__file__).resolve().parents[2]


def _libraries():
    sys.path.insert(0, str(RESOURCE_ROOT / 'tools/rfoundry'))
    from rfoundry import boards, serialflash
    return boards.BOARDS['dm32'], serialflash


def _emit(progress, phase, **values):
    if progress:
        progress(dict(phase=phase, **values))


def _hash(data):
    return hashlib.sha256(data).hexdigest()


def _monitor(port, board, sf, progress):
    found = sf.probe(port)
    if found and board.options_for_name(found.get('board', '')) is None:
        raise RuntimeError('The connected radio is not the expected DM-32UV')
    st = sf.tool('stubtool')
    cable = sf.open_port(port)
    radio = st.Radio(cable, log=lambda message: _emit(progress, 'radio', message=str(message)))
    try:
        if found:
            radio.rfoundry_enter(board='dm32')
        else:
            try:
                radio.attach()
                radio.configure('dm32')
            except st.DumpError:
                _emit(progress, 'needs-boot-rom', message='Switch the radio off, then switch it on; keep its programming cable connected')
                radio.start(board='dm32')
        return radio, cable, found
    except BaseException:
        cable.close()
        raise


def _same_radio(radio, saved, progress):
    # The original stock codeplug and calibration below 7 MB are never written by this installer.
    # Compare device-computed sector CRCs before any erase, rather than trusting the COM port name.
    for start in range(0, 0x700000, 16 * 4096):
        actual = radio.sums('q128', start, 16)
        expected = [zlib.crc32(saved[a:a + 4096]) & 0xffffffff for a in range(start, start + 16 * 4096, 4096)]
        if [x[0] for x in actual] != expected:
            raise RuntimeError('The connected radio does not match this backup; its calibration and saved data were preserved')
        _emit(progress, 'checking-radio', done=start + 16 * 4096, total=0x700000)


def firmware_info(resource_root=None):
    root = pathlib.Path(resource_root or RESOURCE_ROOT)
    path = root / 'release/xsuite-dm32uv.json'
    if not path.is_file():
        raise RuntimeError('The bundled firmware manifest is missing')
    result = json.loads(path.read_text(encoding='utf-8'))
    if result.get('board') != 'dm32' or result.get('variant') != 'xsuite':
        raise RuntimeError('This firmware package is not the DM-32UV X Suite edition')
    image = (path.parent / result['image']).resolve()
    if image.parent != path.parent.resolve() or _hash(image.read_bytes()) != result.get('sha256'):
        raise RuntimeError('The bundled firmware image failed its manifest check')
    return result


def backup(port, output_dir, progress=None):
    out = pathlib.Path(output_dir)
    if out.exists() and any(out.iterdir()):
        raise RuntimeError('Choose a new radio backup directory; existing backups are preserved')
    board, sf = _libraries()
    found = sf.probe(port)
    if found and board.options_for_name(found.get('board', '')) is None:
        raise RuntimeError('The connected radio is not the expected DM-32UV')
    out.mkdir(parents=True, exist_ok=True)
    radio, cable, found = _monitor(port, board, sf, progress)
    try:
        # Read-only memories after the RAM monitor starts. No flash erase/write occurs in this operation.
        files = {}
        for name, memory, size in [('program-memory.bin', 'progmem', 0x100000), ('data-memory.bin', 'q128', 0x1000000)]:
            _emit(progress, 'backup', file=name, total=size)
            path = out / name
            with path.open('xb') as target:
                radio.dump(target, memory, 0, size,
                           progress=lambda done, total: _emit(progress, 'backup', file=name, done=done, total=total))
            data = path.read_bytes()
            if len(data) != size:
                raise RuntimeError('The radio backup was incomplete: ' + name)
            files[name] = dict(bytes=size, sha256=_hash(data))
        metadata = {'format': 'xsuite-radio-backup/1', 'board': 'dm32', 'files': files,
                    'source': found or {'mode': 'boot-rom'}, 'complete': True}
        (out / 'backup.json').write_text(json.dumps(metadata, indent=2) + '\n', encoding='utf-8')
        # Keep the read-only RAM monitor ready for the automatic conversion/install step.
        # Switching the radio off safely returns to its previous firmware.
        _emit(progress, 'backup-complete', bytes=0x1100000)
        return metadata
    finally:
        cable.close()


def _verified_backup(directory):
    root = pathlib.Path(directory)
    metadata = json.loads((root / 'backup.json').read_text(encoding='utf-8'))
    if metadata.get('format') != 'xsuite-radio-backup/1' or metadata.get('board') != 'dm32' or metadata.get('complete') is not True:
        raise RuntimeError('A complete DM-32UV backup is required before firmware installation')
    data = {}
    for name, size in [('program-memory.bin', 0x100000), ('data-memory.bin', 0x1000000)]:
        value = (root / name).read_bytes()
        info = metadata.get('files', {}).get(name, {})
        if len(value) != size or info.get('bytes') != size or _hash(value) != info.get('sha256'):
            raise RuntimeError('Radio backup validation failed: ' + name)
        data[name] = value
    return metadata, data


def _own_codec(board, sf, program, app):
    codec = sf.tool('hrc7000_codec')
    offset = board.codec_slot[0] - board.rom_base
    existing = program[offset:board.codec_slot[1] - board.rom_base]
    parsed = codec.parse_slot(existing)
    if parsed:
        # A valid prior RadioFoundry slot was read from this radio's verified private backup.
        return existing[:parsed['length']]
    # A stock program-memory read is verified by the upstream profile/map before its codec is carved.
    return sf.slot_from_file(board, app, program)[0]


def install(port, image_path, backup_dir, progress=None):
    manifest = firmware_info()
    image = pathlib.Path(image_path).read_bytes()
    if _hash(image) != manifest['sha256']:
        raise RuntimeError('The selected image is not the verified bundled firmware')
    metadata, files = _verified_backup(backup_dir)
    converted = None
    if metadata.get('source', {}).get('mode') == 'boot-rom':
        directory = pathlib.Path(backup_dir)
        converted = (directory / 'converted-codeplug.bin').read_bytes()
        record = json.loads((directory / 'converted-codeplug.json').read_text(encoding='utf-8'))
        if (record.get('sourceSha256') != metadata['files']['data-memory.bin']['sha256'] or
                record.get('sha256') != _hash(converted) or record.get('bytes') != len(converted) or
                len(converted) != 0x120000):
            raise RuntimeError('Stock channel conversion is missing or does not match the saved radio backup')
    board, sf = _libraries()
    # All content and codec validation happens before either flash route can erase a sector.
    slot = _own_codec(board, sf, files['program-memory.bin'], image)
    raw = sf.contiguous(board, image, slot)
    sf.tool('serial_loader').check_image(raw, sf.tool('serial_loader').MODELS['dm32'])
    radio, cable, found = _monitor(port, board, sf, progress)
    try:
        _same_radio(radio, files['data-memory.bin'], progress)
        radio.write_image(raw, progress=lambda done, total: _emit(progress, 'install', done=done, total=total))
        radio.reset_chip()
    finally:
        cable.close()
    verified = sf.wait_for_rfoundry(port)
    if not verified or board.options_for_name(verified.get('board', '')) is None or not board.codec_present(verified.get('codec')):
        raise RuntimeError('Firmware was written; restart the radio and reconnect to finish verification')
    if converted is not None:
        _write_codeplug(port, converted, sf, progress)
    _initialize_epoch(port)
    _emit(progress, 'verified', board='dm32', firmware=verified)
    return {'board': 'dm32', 'installed': True, 'verified': verified, 'sha256': manifest['sha256'], 'backup': str(backup_dir)}


def restore(port, backup_dir, progress=None):
    metadata, files = _verified_backup(backup_dir)
    board, sf = _libraries()
    program = files['program-memory.bin']
    raw = program[board.app_base - board.rom_base:]
    sf.tool('serial_loader').check_image(raw, sf.tool('serial_loader').MODELS['dm32'])
    found = sf.probe(port)
    if found and board.options_for_name(found.get('board', '')) is None:
        raise RuntimeError('The connected radio is not the expected DM-32UV')
    _emit(progress, 'restore', message='Restoring the application and its codec; factory calibration and data flash stay preserved')
    radio, cable, found = _monitor(port, board, sf, progress)
    try:
        _same_radio(radio, files['data-memory.bin'], progress)
        written, same = radio.write_image(raw, progress=lambda done, total: _emit(progress, 'restore', done=done, total=total))
        radio.reset_chip()
    finally:
        cable.close()
    return {'board': 'dm32', 'restoredApplication': True, 'verifiedBy': 'upstream-sector-crc',
            'sectorsWritten': written, 'sectorsUnchanged': same, 'backup': str(backup_dir)}


def _write_codeplug(port, converted, sf, progress):
    from rfoundry import debuglink
    link = debuglink.Link(port)
    try:
        region = link.request(0x0f, b'\x00')
        if region and region[0] == 2:
            # Explicit install follows a verified full backup; preserve that data before claiming our own settings area.
            uid = link.chip_uid()
            if not uid or len(uid) != 24:
                raise RuntimeError('The radio identity could not be verified before preparing settings')
            link.request(0x0f, b'\x02' + bytes.fromhex(uid)[:4])
            for _ in range(100):
                time.sleep(0.1)
                response = link.request(0x0f, b'\x00')
                if not response:
                    raise RuntimeError('The radio did not confirm its settings storage state')
                if response[0] != 2:
                    break
            else:
                raise RuntimeError('Radio settings preparation did not finish')
    finally:
        link.close()
    path = RESOURCE_ROOT / 'tools/cps_client.py'
    spec = importlib.util.spec_from_file_location('xsuite_cps_client', path)
    cps = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(cps)
    client = cps.open_cps(port, offset=0x700000)
    try:
        regions = cps.REGIONS['radio']
        before = client.read_image(regions)
        client.write_image(bytearray(converted), regions, reboot=False, base=before,
                           progress=lambda done, total: _emit(progress, 'channels', done=done, total=total))
        after = client.read_image(regions)
        if cps.compare(after, converted, regions):
            raise RuntimeError('The radio channel read-back differs from the converted backup')
        client.control(6, b'\x00')
    finally:
        client.close()


def _initialize_epoch(port):
    from rfoundry import debuglink
    link = debuglink.Link(port)
    try:
        status = link.request(0x27, b'\x00').decode('ascii')
        if 'xsuite.proto=1\n' not in status:
            raise RuntimeError('The installed radio did not identify its X Suite interface')
        if 'xsuite.epoch_ready=0\n' in status:
            link.request(0x27, b'\x08' + struct.pack('<I', secrets.randbits(32) or 1))
            status = link.request(0x27, b'\x00').decode('ascii')
        if 'xsuite.epoch_ready=1\n' not in status:
            raise RuntimeError('The radio packet identity was not saved; reconnect to complete setup')
    finally:
        link.close()
