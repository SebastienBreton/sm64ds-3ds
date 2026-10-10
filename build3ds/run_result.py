"""A program PASS is accepted only with fresh, clean emulator diagnostics."""
import re


def movie_playback_failure(emulator_text):
    if re.search(r'playback will be out of sync|Playback file does not have valid header|Failed to playback movie|different version of Citra', emulator_text, re.I):
        return 'movie_playback_invalid'
    if 'Loaded Movie, ID:' not in emulator_text:
        return 'movie_playback_not_confirmed'
    return None

def assess_run(application_result, emulator_text, fresh):
    unmapped = emulator_text.count("UnmappedAccess:") if fresh else 0
    if application_result != "PASS":
        return application_result, "timeout" if application_result == "TIMEOUT" else "application_failure", unmapped
    if not fresh:
        return "FAIL", "missing_or_stale_emulator_log", unmapped
    if unmapped:
        return "FAIL", "unmapped_memory_access", unmapped
    return "PASS", None, 0
