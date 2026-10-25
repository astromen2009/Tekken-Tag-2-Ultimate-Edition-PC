"""The importer's Windows parts, checked on GitHub's Windows runner (windows-fighters.yml): finding Steam and the
processes, and the format and pack tests. A failure is reported as a GitHub annotation (::error::) with its output, so
that it shows on the run's page.

    python windows/launcher/ci_check.py      (from the repository's root)
"""
import io
import os
import sys
import traceback
import unittest

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
sys.path.insert(0, os.path.join(ROOT, 'tools', 'characters'))


def annotate(title, text):
    text = text[-6000:].replace('%', '%25').replace('\r', '').replace('\n', '%0A')
    print('::error title=%s::%s' % (title, text), flush=True)


def check(title, function):
    try:
        function()
        print('ok: %s' % title, flush=True)
        return True
    except BaseException:
        annotate(title, traceback.format_exc())
        return False


def windows_parts():
    from ttt2char.tekken7 import autodump
    print('steam:', autodump.steams())
    print('processes:', len(autodump._windows_processes()))
    print('tekken 7 running:', autodump.tekken7_running(None))
    print('movesets:', autodump.MOVESETS, 'cache:', autodump.CACHE)
    lock = autodump._lock(autodump._lock_path(ROOT), timeout=5)
    lock.close()
    from ttt2char import pack, signals
    print('packs:', pack.default_folder(), 'signals:', signals.SIGNALS)


def tests(names):
    def run():
        out = io.StringIO()
        suite = unittest.defaultTestLoader.loadTestsFromNames(names)
        result = unittest.TextTestRunner(stream=out, verbosity=1).run(suite)
        print(out.getvalue())
        if not result.wasSuccessful():
            raise AssertionError(out.getvalue())
    return run


os.chdir(os.path.join(ROOT, 'tools', 'characters'))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'characters', 'tests'))
ok = check('Windows parts', windows_parts)
ok = check('format tests', tests(['test_formats'])) and ok
ok = check('pack tests', tests(['test_packs'])) and ok
sys.exit(0 if ok else 1)
