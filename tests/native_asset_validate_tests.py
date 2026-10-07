"""Exercise the production validator against files and BIGFILE entries on disk."""
import pathlib
import struct
import subprocess
import sys
import tempfile
import unittest

VALIDATOR = str(pathlib.Path(sys.argv.pop(1)).resolve())


def put32(data, offset, value):
    struct.pack_into('<I', data, offset, value)


def ptr_map(slots):
    return struct.pack('<I', len(slots) * 4) + b''.join(struct.pack('<I', slot) for slot in slots)


def fixtures():
    mpk = bytearray(128)
    put32(mpk, 4, 32)
    mpk[32:37] = b'crash'
    struct.pack_into('<hhI', mpk, 48, 7, 1, 64)
    mpk_map = ptr_map([4, 52])
    lev = bytearray(0x234)
    put32(lev, 0, 0x200)
    put32(lev, 0x204, 1)  # One vertex, zero quads and BSP nodes.
    put32(lev, 0x210, 0x224)
    lev_map = ptr_map([0, 0x210])
    return struct.pack('<I', len(mpk)) + mpk + mpk_map, lev, lev_map


def bigfile(entries):
    data = bytearray(0x800)
    put32(data, 4, len(entries))
    for index, entry in enumerate(entries):
        sector = len(data) // 0x800
        struct.pack_into('<II', data, 8 + index * 8, sector, len(entry))
        data.extend(entry)
        data.extend(bytes((-len(data)) % 0x800))
    return data


class ValidatorTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = pathlib.Path(self.temp.name)
        self.mpk, self.lev, self.ptr = fixtures()
        self.write('pack.mpk', self.mpk)
        self.write('track.lev', self.lev)
        self.write('track.ptr', self.ptr)

    def write(self, name, data):
        path = self.root / name
        path.write_bytes(data)
        return str(path)

    def run_tool(self, *args, expected=0):
        result = subprocess.run([VALIDATOR, *map(str, args)], text=True, capture_output=True)
        self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
        self.assertNotIn('AddressSanitizer', result.stderr)
        self.assertNotIn('runtime error:', result.stderr)
        return result.stdout + result.stderr

    def test_extracted_files_and_immutability(self):
        self.assertIn('1 models', self.run_tool('mpk', self.root / 'pack.mpk'))
        text = self.run_tool('lev', self.root / 'track.lev', self.root / 'track.ptr')
        self.assertIn('1 vertices', text)
        self.assertEqual((self.root / 'pack.mpk').read_bytes(), self.mpk)
        self.assertEqual((self.root / 'track.lev').read_bytes(), self.lev)
        self.assertEqual((self.root / 'track.ptr').read_bytes(), self.ptr)

    def test_embedded_lev(self):
        dram = struct.pack('<I', len(self.lev)) + self.lev + self.ptr
        self.assertIn('LEV OK', self.run_tool('lev-dram', self.write('track.dram', dram)))

    def test_bigfile_entries_and_exact_lengths(self):
        dram = struct.pack('<I', len(self.lev)) + self.lev + self.ptr
        external = struct.pack('<I', 0x80000000 | len(self.lev)) + self.lev
        archive = bigfile([self.mpk, dram, external, self.ptr])
        path = self.write('BIGFILE.BIG', archive)
        self.assertIn('MPK OK', self.run_tool('big-mpk', path, 0))
        self.assertIn('LEV OK', self.run_tool('big-lev', path, 1))
        self.assertIn('LEV OK', self.run_tool('big-lev-ptr', path, 2, 3))
        self.assertEqual(pathlib.Path(path).read_bytes(), archive)
        # Padding contains sufficient extra bytes; the entry size must win.
        put32(archive, 12, len(self.mpk) - 1)
        self.run_tool('big-mpk', self.write('short.big', archive), 0, expected=1)

    def test_bad_files_maps_and_nested_spans(self):
        for length in [0, 3, len(self.mpk) - 1]:
            self.run_tool('mpk', self.write('short.mpk', self.mpk[:length]), expected=1)
        duplicate = bytearray(self.mpk)
        put32(duplicate, 140, 4)
        self.run_tool('mpk', self.write('duplicate.mpk', duplicate), expected=1)
        headers = bytearray(self.mpk)
        struct.pack_into('<h', headers, 54, 2)  # Second header would exceed payload.
        self.run_tool('mpk', self.write('headers.mpk', headers), expected=1)
        invalid = bytearray(self.lev)
        put32(invalid, 0x204, 2)
        self.run_tool('lev', self.write('mesh.lev', invalid), self.root / 'track.ptr', expected=1)
        self.run_tool('lev', self.root / 'track.lev', self.write('short.ptr', self.ptr[:-1]), expected=1)
        negative = struct.pack('<I', 0xffffffff) + self.lev
        self.assertIn('separate PTR', self.run_tool('lev-dram', self.write('external.dram', negative), expected=1))
        self.assertIn('LEV OK', self.run_tool('lev-external', self.root / 'external.dram', self.root / 'track.ptr'))

    def test_bad_archive_and_usage(self):
        archive = bigfile([self.mpk])
        path = self.write('BAD.BIG', archive)
        self.run_tool('big-mpk', path, 1, expected=1)
        put32(archive, 8, 0xffffffff)
        self.run_tool('big-mpk', self.write('offset.big', archive), 0, expected=1)
        put32(archive, 4, 0xffffffff)
        self.run_tool('big-mpk', self.write('count.big', archive), 0, expected=1)
        self.run_tool('big-mpk', path, '-1', expected=2)
        self.run_tool('big-mpk', path, '4294967296', expected=2)
        self.run_tool('unknown', path, expected=2)
        self.run_tool('mpk', self.root / 'missing', expected=1)

    def test_model_library_ids(self):
        for model_id, expected in [(-1, 0), (226, 0), (-2, 1), (227, 1)]:
            data = bytearray(self.mpk)
            struct.pack_into('<h', data, 52, model_id)
            text = self.run_tool('mpk', self.write('ids.mpk', data), expected=expected)
            if model_id == -1:
                self.assertIn('0 registered IDs', text)
            elif model_id == 226:
                self.assertIn('1 registered IDs', text)

    def test_instance_model_reference(self):
        lev = bytearray(self.lev) + bytearray(64 + 24)
        definition, model = len(self.lev), len(self.lev) + 64
        put32(lev, 0xc, 1)
        put32(lev, 0x10, definition)
        put32(lev, definition + 0x10, model)
        struct.pack_into('<h', lev, model + 0x10, 27)
        ptr = ptr_map([0, 0x210, 0x10, definition + 0x10])
        text = self.run_tool('lev', self.write('instance.lev', lev), self.write('instance.ptr', ptr))
        self.assertIn('1 instance definitions decoded', text)
        put32(lev, definition + 0x10, len(lev))
        self.run_tool('lev', self.write('instance.lev', lev), self.root / 'instance.ptr', expected=1)

    def test_animation_frames_from_file(self):
        payload = bytearray(self.mpk[4:132]) + bytearray(128)
        put32(payload, 116, 1)  # Header animation count.
        put32(payload, 120, 128)
        put32(payload, 128, 132)
        struct.pack_into('<HHI', payload, 148, 3, 32, 252)
        for frame in range(3):
            put32(payload, 156 + frame * 32 + 24, 28)
        put32(payload, 252, 0x12345678)
        ptr = ptr_map([4, 52, 120, 128, 152])

        def write_animation(data):
            return self.write('animated.mpk', struct.pack('<I', len(data)) + data + ptr)

        text = self.run_tool('mpk', write_animation(payload))
        self.assertIn('1 animations (0 interpolated), 3 stored frames', text)
        struct.pack_into('<H', payload, 148, 0x8005)
        self.assertIn('1 interpolated', self.run_tool('mpk', write_animation(payload)))
        for field, value in [(180, 33), (152, 256), (120, 256)]:
            malformed = bytearray(payload)
            put32(malformed, field, value)
            self.run_tool('mpk', write_animation(malformed), expected=1)
        struct.pack_into('<H', payload, 148, 0)
        self.run_tool('mpk', write_animation(payload), expected=1)


if __name__ == '__main__':
    unittest.main()
