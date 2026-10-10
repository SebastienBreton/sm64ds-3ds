import unittest
from azahar_config import model_failure, model_override


class ModelValidation(unittest.TestCase):
    def test_preserves_other_sections_and_default_flags(self):
        original = b'[Core]\r\ncpu_clock_percentage=100\r\n[System]\r\nis_new_3ds=true\r\nis_new_3ds\\default=true\r\n[Other]\r\nis_new_3ds=true\r\n'
        actual = model_override(original, 'old')
        self.assertEqual(actual, original.replace(b'is_new_3ds=true\r\nis_new_3ds\\default=true', b'is_new_3ds=false\r\nis_new_3ds\\default=false'))
        self.assertIn(b'cpu_clock_percentage=100', actual)

    def test_new_model_requires_nondefault_override(self):
        original = b'[System]\nis_new_3ds=false\nis_new_3ds\\default=true'
        self.assertEqual(model_override(original, 'new'), b'[System]\nis_new_3ds=true\nis_new_3ds\\default=false')

    def test_missing_or_duplicate_setting_rejected(self):
        for original in (b'[Other]\nis_new_3ds=true\n', b'[System]\nis_new_3ds=true\n',
                         b'[System]\nis_new_3ds=true\nis_new_3ds=false\nis_new_3ds\\default=false\n'):
            with self.assertRaises(ValueError):
                model_override(original, 'old')

    def test_model_verification_requires_successful_guest_query(self):
        old = 'CPU mode: model_query=00000000 new3ds=0 speedup_requested=0\n'
        new = 'CPU mode: model_query=00000000 new3ds=1 speedup_requested=1\n'
        self.assertIsNone(model_failure(old, 'old'))
        self.assertIsNone(model_failure(new, 'new'))
        self.assertEqual(model_failure(new, 'old'), 'model_mismatch')
        self.assertEqual(model_failure(old+new, 'old'), 'model_mismatch')
        self.assertEqual(model_failure(old.replace('00000000', 'd8e007f7'), 'old'), 'model_mismatch')
        self.assertEqual(model_failure('PASS\n', 'old'), 'model_not_verified_by_guest')


if __name__ == '__main__':
    unittest.main()
