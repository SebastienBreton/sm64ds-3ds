import struct
import unittest
from azahar_movie import edit, parse
from run_result import movie_playback_failure


class MovieTests(unittest.TestCase):
    def test_every_ds_touch_coordinate_roundtrips_through_native_bridge(self):
        baseline = self.baseline()
        for x in range(256):
            result = edit(baseline, [{'start': 0, 'end': 1, 'touch_ds': [x, 191]}])
            px, py, valid = struct.unpack_from('<HHB', result, 264)
            self.assertEqual((px * 256 // 320, py * 192 // 240, valid), (x, 191, 1))
        for y in range(192):
            result = edit(baseline, [{'start': 0, 'end': 1, 'touch_ds': [255, y]}])
            px, py, valid = struct.unpack_from('<HHB', result, 264)
            self.assertEqual((px * 256 // 320, py * 192 // 240, valid), (255, y, 1))
        with self.assertRaises(ValueError):
            edit(baseline, [{'start': 0, 'end': 1, 'touch_ds': [128, 88], 'touch': [160, 110]}])

    def test_playback_requires_loaded_and_synchronized_movie(self):
        loaded = 'Movie <Info> core/movie.cpp:StartPlayback:550: Loaded Movie, ID: 123'
        self.assertIsNone(movie_playback_failure(loaded))
        self.assertEqual(movie_playback_failure(''), 'movie_playback_not_confirmed')
        for diagnostic in ['Expected to read type 4, but found 0. Your playback will be out of sync',
                           'This movie was created on a different version of Citra, playback may desync',
                           'Failed to playback movie: Unable to open file',
                           'Playback file does not have valid header']:
            self.assertEqual(movie_playback_failure(loaded + diagnostic), 'movie_playback_invalid')

    def baseline(self):
        header = bytearray(256)
        header[:4] = b'CTM\x1b'
        struct.pack_into('<Q', header, 84, 3)
        records = [struct.pack('<BHhh', 0, 0, 0, 0),
                   struct.pack('<BHHBB', 1, 0, 0, 0, 0),
                   struct.pack('<BhhBB', 4, 23, -19, 1, 0)]
        return bytes(header) + b''.join(records * 3)

    def test_edit_preserves_other_services_and_header(self):
        baseline = self.baseline()
        result = edit(baseline, [{'start': 1, 'end': 2, 'buttons': 8,
                                 'circle': [-156, 77], 'touch': [319, 239]}])
        self.assertEqual(result[:277], baseline[:277])
        self.assertEqual(result[277:284], struct.pack('<BHhh', 0, 8, -156, 77))
        self.assertEqual(result[284:291], struct.pack('<BHHBB', 1, 319, 239, 1, 0))
        self.assertEqual(result[291:], baseline[291:])
        self.assertEqual(parse(result)['samples'], 3)

    def test_null_touch_is_release(self):
        touched = edit(self.baseline(), [{'start': 0, 'end': 3, 'touch': [2, 3]}])
        restored = edit(touched, [{'start': 0, 'end': 3, 'touch': None}])
        self.assertEqual(restored, self.baseline())

    def test_reject_corrupt_header_or_stream(self):
        baseline = self.baseline()
        cases = [baseline[:-1], b'nope' + baseline[4:],
                 baseline[:84] + struct.pack('<Q', 4) + baseline[92:],
                 baseline[:256] + b'\xff' + baseline[257:]]
        for case in cases:
            with self.subTest(case=case[:100]), self.assertRaises(ValueError):
                parse(case)
        # Same record counts, but sensor order violates the HID service format.
        reordered = baseline[:256] + baseline[263:270] + baseline[256:263] + baseline[270:]
        with self.assertRaises(ValueError):
            edit(reordered, [])

    def test_reject_ambiguous_or_unrepresentable_events(self):
        cases = [[{'start': 0, 'end': 4}],
                 [{'start': 0, 'end': 1, 'touch': [320, 0]}],
                 [{'start': 0, 'end': 1, 'buttons': 65536}],
                 [{'start': 0, 'end': 1, 'circle': [-157, 0]}],
                 [{'start': 0, 'end': 1, 'typo': 1}],
                 [{'start': 0, 'end': 2}, {'start': 1, 'end': 3}]]
        for case in cases:
            with self.subTest(case=case), self.assertRaises(ValueError):
                edit(self.baseline(), case)


if __name__ == '__main__':
    unittest.main()
