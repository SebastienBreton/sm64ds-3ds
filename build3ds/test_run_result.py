import unittest
from pathlib import Path
from run_result import assess_run, movie_playback_failure

class RunResultTests(unittest.TestCase):
    def test_actual_false_pass_is_rejected(self):
        text=(Path(__file__).parent/'validation/actor-base-teardown/azahar-log.txt').read_text()
        self.assertEqual(assess_run('PASS',text,True),('FAIL','unmapped_memory_access',21))

    def test_actual_clean_run_is_accepted(self):
        text=(Path(__file__).parent/'validation/audio-clock-smoke_raw_pcm/azahar-log.txt').read_text()
        self.assertEqual(assess_run('PASS',text,True),('PASS',None,0))

    def test_old_or_missing_log_cannot_validate_pass(self):
        self.assertEqual(assess_run('PASS','',False),('FAIL','missing_or_stale_emulator_log',0))
        self.assertEqual(assess_run('PASS','UnmappedAccess:',False),('FAIL','missing_or_stale_emulator_log',0))

    def test_original_failure_is_retained(self):
        self.assertEqual(assess_run('TIMEOUT','',False)[0],'TIMEOUT')
        self.assertEqual(assess_run('FAIL','',True)[0],'FAIL')

    def test_real_incompatible_movie_is_rejected(self):
        text=(Path(__file__).parent/'validation/interactive-recroom-mmio-background-cache/azahar-log.txt').read_text()
        self.assertEqual(movie_playback_failure(text),'movie_playback_invalid')
        self.assertEqual(movie_playback_failure('Loaded Movie, ID: 00000000'),None)
        self.assertEqual(movie_playback_failure(''),'movie_playback_not_confirmed')

if __name__=='__main__':unittest.main()
