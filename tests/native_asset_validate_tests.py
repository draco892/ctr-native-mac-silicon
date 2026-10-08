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

    def test_authored_instance_draw(self):
        lev = bytearray(self.lev) + bytearray(128 + 24 + 64 + 20 + 40 + 4)
        definition = len(self.lev)
        model = definition + 128
        header, commands = model + 24, model + 24 + 64
        frame, colors = commands + 20, commands + 20 + 40
        put32(lev, 0xc, 2)
        put32(lev, 0x10, definition)
        put32(lev, definition + 0x10, model)
        struct.pack_into('<hhh', lev, definition + 0x14, 4096, 8192, 2048)
        struct.pack_into('<hhh', lev, definition + 0x30, 10, 20, 100)
        struct.pack_into('<hhh', lev, definition + 0x36, 0, 1024, 0)
        struct.pack_into('<hhI', lev, model + 16, 27, 1, header)
        struct.pack_into('<hhh', lev, header + 24, 4096, 4096, 4096)
        put32(lev, header + 32, commands)
        put32(lev, header + 36, frame)
        put32(lev, header + 44, colors)
        for offset, command in [(4, 0x80010000), (8, 0x00020000), (12, 0x00030000), (16, 0xffffffff)]:
            put32(lev, commands + offset, command)
        put32(lev, frame + 24, 28)
        lev[frame + 28:frame + 37] = bytes([0, 0, 0, 16, 0, 0, 0, 8, 16])
        put32(lev, colors, 0x112233)
        lev[definition + 64:definition + 128] = lev[definition:definition + 64]
        struct.pack_into('<hhh', lev, definition + 64 + 0x30, 522, 20, 100)
        slots = [0, 0x210, 0x10, definition + 16, definition + 80, model + 20, header + 32, header + 36, header + 44]
        ptr = ptr_map(slots)
        source = bytes(lev)
        file = self.write('authored.lev', lev)
        text = self.run_tool('lev', file, self.write('authored.ptr', ptr))
        self.assertIn('Instance draw OK: 2 definitions, 2 projected triangles', text)
        self.assertEqual(pathlib.Path(file).read_bytes(), source)

        # Broad, unrotated triangles make connectivity a stable raster assertion.
        for index in range(2):
            start = definition + index * 64
            struct.pack_into('<hhh', lev, start + 20, 4096, 4096, 4096)
            struct.pack_into('<hhh', lev, start + 54, 0, 0, 0)
        self.write('authored.lev', lev)
        vram = bytearray(22)
        put32(vram, 0, 0x10)
        struct.pack_into('<HHHHH', vram, 12, 0, 0, 1, 1, 0)
        vram_file = self.write('scene.vrm', vram)
        output = self.root / 'scene.ppm'
        args = ['scene', file, self.root / 'authored.ptr', vram_file, '0', '2', output, 'front']
        self.assertIn('rendered 2 instances', self.run_tool(*args))
        prefix = b'P6\n512 512\n255\n'
        image = output.read_bytes()
        self.assertTrue(image.startswith(prefix))
        self.assertEqual(len(image), len(prefix) + 512 * 512 * 3)
        # Separate silhouettes prove both instances share one uncleared target.
        pixels = {i for i in range(512 * 512) if image[len(prefix) + i * 3:len(prefix) + i * 3 + 3] != bytes([24, 28, 36])}
        components = 0
        while pixels:
            components += 1
            stack = [pixels.pop()]
            while stack:
                point = stack.pop()
                x, y = point % 512, point // 512
                for dx, dy in [(-1, 0), (1, 0), (0, -1), (0, 1), (-1, -1), (-1, 1), (1, -1), (1, 1)]:
                    nx, ny = x + dx, y + dy
                    neighbor = ny * 512 + nx
                    if 0 <= nx < 512 and 0 <= ny < 512 and neighbor in pixels:
                        pixels.remove(neighbor)
                        stack.append(neighbor)
        self.assertEqual(components, 2)
        dram = struct.pack('<I', len(lev)) + lev + ptr
        self.run_tool('scene-dram', self.write('scene.dram', dram), vram_file,
                      '0', '2', output, 'front')
        self.assertEqual(output.read_bytes(), image)
        self.assertEqual(pathlib.Path(file).read_bytes(), lev)
        put32(lev, definition + 0x20, 0x800)  # Custom matrix path is counted/skipped.
        self.write('authored.lev', lev)
        self.assertIn('rendered 1 instances, unsupported 1', self.run_tool(*args))
        output.unlink()
        put32(lev, definition + 64 + 0x20, 0x400)
        self.write('authored.lev', lev)
        self.run_tool(*args, expected=1)
        self.assertFalse(output.exists())
        put32(lev, definition + 0x20, 0)
        put32(lev, definition + 64 + 0x20, 0)
        # Overlapping red near model and green far model, reversed traversal:
        # the nearest surface must win regardless of instance iteration order.
        second_model = len(lev)
        delta = second_model - model
        lev.extend(lev[model:])
        second_header = header + delta
        put32(lev, second_model + 20, second_header)
        for field, target in [(32, commands), (36, frame), (44, colors)]:
            put32(lev, second_header + field, target + delta)
        put32(lev, colors, 0x0000ff)
        put32(lev, colors + delta, 0x00ff00)
        put32(lev, definition + 80, second_model)
        slots.extend([second_model + 20, second_header + 32, second_header + 36, second_header + 44])
        for index, z in enumerate([100, 300]):
            start = definition + index * 64
            struct.pack_into('<hhh', lev, start + 20, 4096, 4096, 4096)
            struct.pack_into('<hhh', lev, start + 48, 0, 0, z)
            struct.pack_into('<hhh', lev, start + 54, 0, 0, 0)
        self.write('authored.ptr', ptr_map(slots))
        self.write('authored.lev', lev)
        args[-1] = 'front'
        self.run_tool(*args)
        occluded = output.read_bytes()
        rgb = occluded[len(prefix):]
        self.assertIn(bytes([255, 0, 0]), rgb)
        self.assertFalse(any(rgb[i:i + 3] == bytes([0, 255, 0]) for i in range(0, len(rgb), 3)))
        first_record = bytes(lev[definition:definition + 64])
        lev[definition:definition + 64] = lev[definition + 64:definition + 128]
        lev[definition + 64:definition + 128] = first_record
        self.write('authored.lev', lev)
        self.run_tool(*args)
        self.assertEqual(output.read_bytes(), occluded)
        # Blue coarse terrain lies between the red near and green far model.
        # Near model GTE depth is x4 and must be normalized before comparison.
        quads = len(lev)
        vertices = quads + 0x5c
        lev.extend(bytes(0x5c + 4 * 16))
        put32(lev, 0x200, 1)
        put32(lev, 0x204, 4)
        put32(lev, 0x20c, quads)
        put32(lev, 0x210, vertices)
        slots.append(0x20c)
        struct.pack_into('<9H', lev, quads, 0, 1, 2, 3, 0, 0, 0, 0, 0)
        for index, position in enumerate([(0, 0, 200), (64, 0, 200), (0, 64, 232), (64, 64, 232)]):
            struct.pack_into('<hhh', lev, vertices + index * 16, *position)
            put32(lev, vertices + index * 16 + 8, 0xff0000)
        self.write('authored.ptr', ptr_map(slots))
        self.write('authored.lev', lev)
        terrain_args = ['scene-terrain', *args[1:]]
        self.assertIn('Terrain OK: 1 quad blocks, 2 coarse triangles', self.run_tool(*terrain_args))
        terrain_image = output.read_bytes()
        rgb = terrain_image[len(prefix):]
        colors = {rgb[i:i + 3] for i in range(0, len(rgb), 3)}
        self.assertIn(bytes([255, 0, 0]), colors)
        self.assertIn(bytes([0, 0, 255]), colors)
        self.assertNotIn(bytes([0, 255, 0]), colors)
        snapshot = bytes(lev)
        self.assertEqual(pathlib.Path(file).read_bytes(), snapshot)
        # Both PTR forms must produce identical terrain composition.
        dram = struct.pack('<I', len(lev)) + lev + ptr_map(slots)
        self.run_tool('scene-dram-terrain', self.write('terrain.dram', dram), vram_file,
                      '0', '2', output, 'front')
        self.assertEqual(output.read_bytes(), terrain_image)
        # Exclude the red model, so terrain alone must hide the green model.
        # Put green beyond the far threshold to also exercise mixed depth units.
        put32(lev, definition + 64 + 0x20, 0x800)
        struct.pack_into('<hhh', lev, definition + 48, 0, 0, 5000)
        self.write('authored.lev', lev)
        terrain_args[4] = '1'  # Anchor remains at the excluded near model.
        self.assertIn('rendered 1 instances, unsupported 1', self.run_tool(*terrain_args))
        rgb = output.read_bytes()[len(prefix):]
        colors = {rgb[i:i + 3] for i in range(0, len(rgb), 3)}
        self.assertIn(bytes([0, 0, 255]), colors)
        self.assertNotIn(bytes([0, 255, 0]), colors)
        lev[:] = snapshot
        terrain_args[4] = '0'
        # Reversing instance traversal also preserves mixed terrain/model depth.
        lev[definition:definition + 64], lev[definition + 64:definition + 128] = (
            lev[definition + 64:definition + 128], lev[definition:definition + 64])
        self.write('authored.lev', lev)
        self.run_tool(*terrain_args)
        self.assertEqual(output.read_bytes(), terrain_image)
        output.unlink()
        struct.pack_into('<H', lev, quads + 16, 4)  # Invalid unused midpoint.
        self.write('authored.lev', lev)
        self.run_tool(*terrain_args, expected=1)
        self.assertFalse(output.exists())
        struct.pack_into('<H', lev, quads + 16, 0)
        put32(lev, 0x204, 0x7fffffff)
        self.write('authored.lev', lev)
        self.run_tool(*terrain_args, expected=1)
        self.assertFalse(output.exists())
        put32(lev, 0x204, 4)
        self.write('authored.lev', lev)
        args[5] = '3'
        self.run_tool(*args, expected=1)
        self.assertFalse(output.exists())
        args[5] = '0'
        self.run_tool(*args, expected=2)
        args[5] = '257'
        self.run_tool(*args, expected=2)
        args[5] = '2'
        put32(lev, header + 32, len(lev))  # Corrupt later model command pointer.
        self.write('authored.lev', lev)
        self.run_tool(*args, expected=1)
        self.assertFalse(output.exists())

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

    def test_vram_rectangle_files_and_truncation(self):
        data = bytearray(24)
        put32(data, 0, 0x10)
        struct.pack_into('<HHHH', data, 12, 1022, 511, 2, 1)
        struct.pack_into('<HH', data, 20, 0x001f, 0x8000)
        file = self.write('texture.vrm', data)
        self.assertIn('1 rectangles, 2 uploaded words', self.run_tool('vram', file))
        self.assertEqual(pathlib.Path(file).read_bytes(), data)
        self.run_tool('vram', self.write('short.vrm', data[:-1]), expected=1)
        packed = struct.pack('<II', 0x20, 24) + data + struct.pack('<I', 0)
        self.assertIn('VRAM OK:', self.run_tool('vram', self.write('packed.vrm', packed)))
        self.run_tool('vram', self.write('unterminated.vrm', packed[:-4]), expected=1)
        struct.pack_into('<H', data, 16, 3)
        self.run_tool('vram', self.write('outside.vrm', data), expected=1)

    def test_connected_draw_pipeline(self):
        payload = bytearray(320)
        put32(payload, 4, 32)
        struct.pack_into('<hhI', payload, 48, 7, 1, 64)
        struct.pack_into('<hhh', payload, 88, 4096, 4096, 4096)
        put32(payload, 96, 128)
        put32(payload, 100, 160)
        put32(payload, 108, 220)
        for offset, command in [(132, 0x80010000), (136, 0x00020000), (140, 0x00030000), (144, 0xffffffff)]:
            put32(payload, offset, command)
        put32(payload, 184, 28)
        payload[188:197] = bytes([0, 0, 0, 16, 0, 0, 0, 0, 16])
        put32(payload, 220, 0x112233)
        relocations = ptr_map([4, 52, 96, 100, 108])
        def write_draw(data):
            return self.write('draw.mpk', struct.pack('<I', len(data)) + data + relocations)
        self.assertIn('Draw pipeline OK: 1 projected triangles', self.run_tool('mpk', write_draw(payload)))
        # Untextured fixture: exercise the entire file -> projected RGB path.
        payload[195] = 8  # Stored Y is depth; stored Z (16) is GTE/model height.
        model_file = write_draw(payload)
        vram = bytearray(22)
        put32(vram, 0, 0x10)
        struct.pack_into('<HHHHH', vram, 12, 0, 0, 1, 1, 0)
        vram_file = self.write('preview.vrm', vram)
        output = self.root / 'preview.ppm'
        args = ['preview', model_file, vram_file, '0', '0', 'static', '0', output]
        self.assertIn('Preview OK:', self.run_tool(*args))
        image = output.read_bytes()
        prefix = b'P6\n512 512\n255\n'
        self.assertTrue(image.startswith(prefix))
        self.assertEqual(len(image), len(prefix) + 512 * 512 * 3)
        self.assertNotEqual(image[len(prefix):], bytes([24, 28, 36]) * (512 * 512))
        # Explicit camera-axis regression: this pixel is covered only when
        # stored Z remains image height, rather than being swapped with depth.
        pixel = len(prefix) + (275 * 512 + 247) * 3
        self.assertEqual(image[pixel:pixel + 3], bytes([0x33, 0x22, 0x11]))
        self.assertIn('view top', self.run_tool(*args, 'top'))
        self.assertNotEqual(output.read_bytes(), image)
        self.assertEqual(pathlib.Path(model_file).read_bytes(), struct.pack('<I', len(payload)) + payload + relocations)
        self.assertEqual(pathlib.Path(vram_file).read_bytes(), vram)
        output.unlink()
        args[4] = '1'  # Invalid header cannot publish an image.
        self.run_tool(*args, expected=1)
        self.assertFalse(output.exists())
        args[4] = '0'
        self.run_tool(*args, 'invalid-view', expected=2)
        self.assertFalse(output.exists())
        put32(payload, 132, 0x84010000)  # Cached vertex before any write.
        self.run_tool('mpk', write_draw(payload), expected=1)
        self.run_tool(*args, expected=1)
        self.assertFalse(output.exists())

    def test_animation_sequence_with_fixed_camera(self):
        payload = bytearray(512)
        put32(payload, 4, 32)
        struct.pack_into('<hhI', payload, 48, 7, 1, 64)
        struct.pack_into('<hhh', payload, 88, 4096, 4096, 4096)
        put32(payload, 96, 128)
        put32(payload, 108, 220)
        put32(payload, 116, 1)
        put32(payload, 120, 320)
        for offset, command in [(132, 0x80010000), (136, 0x00020000), (140, 0x00030000), (144, 0xffffffff)]:
            put32(payload, offset, command)
        put32(payload, 220, 0x112233)
        put32(payload, 320, 324)
        payload[324:329] = b'moveX'
        struct.pack_into('<HH', payload, 340, 0x8005, 40)
        for frame in range(3):
            start = 348 + frame * 40
            struct.pack_into('<h', payload, start, frame * 8)
            put32(payload, start + 24, 28)
            payload[start + 28:start + 37] = bytes([0, 0, 0, 16, 0, 0, 0, 8, 16])
        relocations = ptr_map([4, 52, 96, 108, 120, 320])
        def write_model():
            return self.write('sequence.mpk', struct.pack('<I', len(payload)) + payload + relocations)
        model_file = write_model()
        vram = bytearray(22)
        put32(vram, 0, 0x10)
        struct.pack_into('<HHHHH', vram, 12, 0, 0, 1, 1, 0)
        vram_file = self.write('sequence.vrm', vram)
        prefix = self.root / 'motion'
        args = ['sequence', model_file, vram_file, '0', '0', '0', '0', '5', prefix]
        self.assertIn('moveX logical=5 stored=3 interpolated=1', self.run_tool('animations', model_file, '0', '0'))
        text = self.run_tool(*args)
        self.assertIn('shared across 5 frames, translation -64 32 256', text)
        images = [self.root / f'motion-{index:06d}.ppm' for index in range(5)]
        contents = [image.read_bytes() for image in images]
        self.assertEqual(len(set(contents)), 5)  # Includes distinct halfway frames.
        self.run_tool(*args, expected=1)  # Existing exports are preserved.
        self.assertEqual([image.read_bytes() for image in images], contents)
        for image in images:
            image.unlink()
        images[-1].write_bytes(b'preserve-me')  # Preflight a later collision.
        self.run_tool(*args, expected=1)
        self.assertFalse(images[0].exists())
        self.assertEqual(images[-1].read_bytes(), b'preserve-me')
        images[-1].unlink()
        args[7] = '6'  # Sequence ranges reject clamping/duplicate tail frames.
        self.run_tool(*args, expected=1)
        self.assertFalse(images[0].exists())
        args[7] = '257'
        self.run_tool(*args, expected=2)
        args[7] = '5'
        put32(payload, 428 + 24, 41)  # Invalid later frame: no output yet.
        write_model()
        self.run_tool(*args, expected=1)
        self.assertFalse(images[0].exists())

    def test_vram_upload_index_list_validation(self):
        output = self.root / 'rejected.ppm'
        for indices in ['', ',0', '0,', '0,,258', '-1', '4294967296',
                        '0,1x', '0, 258', ','.join(['0'] * 17)]:
            self.run_tool('disc-preview', self.root, '260', indices,
                          '36', '0', 'auto', '0', output, expected=2)
            self.assertFalse(output.exists())


if __name__ == '__main__':
    unittest.main()
