"""helper/telegram.py's sign-in, against a stand-in for Telethon's client.

Nothing here talks to Telegram. The fake client raises errors whose class names
are Telethon's, which is all the helper looks at, and records what it was asked.
"""
import asyncio
import importlib.util
import json
import os
import stat
import sys
import tempfile
import unittest
from pathlib import Path

spec = importlib.util.spec_from_file_location('musix_telegram', Path(__file__).resolve().parent.parent / 'helper' / 'telegram.py')
telegram = importlib.util.module_from_spec(spec)
spec.loader.exec_module(telegram)


def error(name, **attributes):
    return type(name, (Exception,), {})(*(), **{}) if not attributes else _with(name, attributes)


def _with(name, attributes):
    e = type(name, (Exception,), {})()
    for key, value in attributes.items():
        setattr(e, key, value)
    return e


class SentCodeTypeApp: pass
class SentCodeTypeSms: pass


class Sent:
    def __init__(self, kind=SentCodeTypeApp):
        self.phone_code_hash = 'hash-1'
        self.type = kind()


class Me:
    first_name, last_name, username = 'Ada', 'Lovelace', 'ada'


class FakeClient:
    def __init__(self, authorised=False):
        self.authorised = authorised
        self.calls = []
        self.fail = {}
        self.password_needed = False
        self.sent_kind = SentCodeTypeApp

    def _maybe(self, name):
        if name in self.fail:
            raise self.fail[name]

    async def connect(self): self._maybe('connect')
    async def disconnect(self): self.calls.append('disconnect')
    async def is_user_authorized(self): return self.authorised
    async def get_me(self): return Me()

    async def send_code_request(self, phone):
        self.calls.append(('send_code_request', phone)); self._maybe('send_code_request')
        return Sent(self.sent_kind)

    async def sign_in(self, phone=None, code=None, password=None, phone_code_hash=None):
        self.calls.append(('sign_in', phone, code, password, phone_code_hash)); self._maybe('sign_in')
        if password is None and self.password_needed:
            raise error('SessionPasswordNeededError')
        self.authorised = True

    async def log_out(self):
        self.calls.append('log_out'); self._maybe('log_out'); self.authorised = False

    async def __call__(self, request):
        raise RuntimeError('not available in the fake')


def run(coro):
    return asyncio.run(coro)


class Case(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)
        self.events = []
        self.clients = []

    def tearDown(self):
        self.tmp.cleanup()

    def account(self, **client):
        def make():
            # Only the first connection has a session; after a log-out the
            # files are gone and a new client starts signed out, as Telethon's would.
            c = FakeClient(**(client if not self.clients else {}))
            self.clients.append(c)
            return c
        return telegram.Account(make, self.dir, self.events.append)

    def states(self):
        return [e['state'] for e in self.events if e.get('event') == 'state']

    def last(self):
        return self.events[-1]


class SignIn(Case):
    def test_a_new_account_waits_for_a_phone_number(self):
        a = self.account(); run(a.start())
        self.assertEqual(self.states(), ['wait-phone'])

    def test_a_remembered_session_goes_straight_to_ready(self):
        a = self.account(authorised=True); run(a.start())
        self.assertEqual(self.last()['state'], 'ready')
        self.assertEqual(self.last()['name'], 'Ada Lovelace')
        self.assertEqual(json.loads((self.dir / 'account.json').read_text())['username'], 'ada')

    def test_phone_then_code_signs_in_with_the_hash_telegram_gave(self):
        a = self.account(); run(a.start())
        run(a.handle({'cmd': 'phone', 'phone': '0049 151 234-5678'}))
        self.assertEqual(self.last(), {'event': 'state', 'state': 'wait-code', 'codeKind': 'app'})
        run(a.handle({'cmd': 'code', 'code': '1 2345'}))
        self.assertEqual(self.last()['state'], 'ready')
        c = self.clients[0]
        self.assertIn(('send_code_request', '+491512345678'), c.calls)
        self.assertIn(('sign_in', '+491512345678', '12345', None, 'hash-1'), c.calls)

    def test_the_code_kind_says_where_telegram_sent_it(self):
        a = self.account(); run(a.start())
        a.client.sent_kind = SentCodeTypeSms
        run(a.handle({'cmd': 'phone', 'phone': '+4915112345'}))
        self.assertEqual(self.last()['codeKind'], 'sms')

    def test_two_step_verification_asks_for_the_password(self):
        a = self.account(); run(a.start())
        a.client.password_needed = True
        run(a.handle({'cmd': 'phone', 'phone': '+4915112345'}))
        run(a.handle({'cmd': 'code', 'code': '12345'}))
        self.assertEqual(self.last()['state'], 'wait-password')
        run(a.handle({'cmd': 'password', 'password': 'hunter2'}))
        self.assertEqual(self.last()['state'], 'ready')
        self.assertIn(('sign_in', None, None, 'hunter2', None), a.client.calls)

    def test_a_wrong_code_is_refused_and_the_step_stays(self):
        a = self.account(); run(a.start())
        run(a.handle({'cmd': 'phone', 'phone': '+4915112345'}))
        a.client.fail['sign_in'] = error('PhoneCodeInvalidError')
        with self.assertRaises(telegram.Rejected) as caught:
            run(a.handle({'cmd': 'code', 'code': '00000'}))
        self.assertIn('isn’t right', str(caught.exception))
        self.assertEqual(a.state, 'wait-code')

    def test_an_expired_code_sends_the_person_back_to_the_phone_number(self):
        a = self.account(); run(a.start())
        run(a.handle({'cmd': 'phone', 'phone': '+4915112345'}))
        a.client.fail['sign_in'] = error('PhoneCodeExpiredError')
        run(a.handle({'cmd': 'code', 'code': '12345'}))
        self.assertEqual(self.last()['state'], 'wait-phone')
        self.assertIn('expired', self.last()['error'])

    def test_a_wrong_password_is_refused_and_the_step_stays(self):
        a = self.account(); run(a.start())
        a.client.password_needed = True
        run(a.handle({'cmd': 'phone', 'phone': '+4915112345'}))
        run(a.handle({'cmd': 'code', 'code': '12345'}))
        a.client.fail['sign_in'] = error('PasswordHashInvalidError')
        with self.assertRaises(telegram.Rejected):
            run(a.handle({'cmd': 'password', 'password': 'nope'}))
        self.assertEqual(a.state, 'wait-password')

    def test_steps_out_of_order_are_refused(self):
        a = self.account(); run(a.start())
        with self.assertRaises(telegram.Rejected):
            run(a.handle({'cmd': 'code', 'code': '12345'}))
        self.assertEqual(a.client.calls, [])

    def test_back_forgets_the_pending_code(self):
        a = self.account(); run(a.start())
        run(a.handle({'cmd': 'phone', 'phone': '+4915112345'}))
        run(a.handle({'cmd': 'back'}))
        self.assertEqual(self.last()['state'], 'wait-phone')
        self.assertIsNone(a.code_hash)

    def test_no_connection_is_offline_with_a_reason(self):
        a = self.account(); a.make_client = lambda: self._failing()
        run(a.start())
        self.assertEqual(self.last()['state'], 'offline')
        self.assertIn('reach Telegram', self.last()['error'])

    def _failing(self):
        c = FakeClient(); c.fail['connect'] = ConnectionError('no route'); return c

    def test_status_repeats_the_current_state(self):
        a = self.account(authorised=True); run(a.start())
        run(a.handle({'cmd': 'status'}))
        self.assertEqual(self.states(), ['ready', 'ready'])
        self.assertEqual(self.last()['name'], 'Ada Lovelace')


class LogOut(Case):
    def test_logging_out_removes_the_session_and_asks_for_a_number_again(self):
        (self.dir / 'session.session').write_text('x'); (self.dir / 'account.json').write_text('{}')
        a = self.account(authorised=True); run(a.start())
        run(a.handle({'cmd': 'logout'}))
        self.assertEqual(self.last()['state'], 'wait-phone')
        self.assertFalse((self.dir / 'session.session').exists())
        self.assertFalse((self.dir / 'account.json').exists())
        self.assertIn('log_out', self.clients[0].calls)
        self.assertNotIn('error', self.last())

    def test_logging_out_offline_still_forgets_locally_and_says_what_is_left(self):
        (self.dir / 'session.session').write_text('x')
        a = self.account(authorised=True); run(a.start())
        a.client.fail['log_out'] = ConnectionError('offline')
        run(a.handle({'cmd': 'logout'}))
        self.assertFalse((self.dir / 'session.session').exists())
        self.assertEqual(self.last()['state'], 'wait-phone')
        self.assertIn('Devices', self.last()['error'])


class Wording(unittest.TestCase):
    def test_phone_numbers_are_made_international(self):
        self.assertEqual(telegram.clean_phone('+49 (151) 234-5678'), '+491512345678')
        self.assertEqual(telegram.clean_phone('0049151234567'), '+49151234567')
        self.assertEqual(telegram.clean_phone('49151234567'), '+49151234567')
        for bad in ('', 'abc', '+1', '+49 151 abc', '+' + '1' * 20):
            with self.assertRaises(telegram.Rejected, msg=bad):
                telegram.clean_phone(bad)

    def test_codes_are_digits(self):
        self.assertEqual(telegram.clean_code(' 12-345 '), '12345')
        for bad in ('', 'abcde', '12'):
            with self.assertRaises(telegram.Rejected, msg=bad):
                telegram.clean_code(bad)

    def test_flood_waits_say_how_long(self):
        self.assertIn('2 minutes', telegram.describe(_with('FloodWaitError', {'seconds': 100})))
        self.assertIn('2 hours', telegram.describe(_with('FloodWaitError', {'seconds': 3700})))
        self.assertIn('30 seconds', telegram.describe(_with('FloodWaitError', {'seconds': 30})))

    def test_an_unknown_error_names_its_class_and_nothing_else(self):
        text = telegram.describe(type('WeirdError', (Exception,), {})('+491512345678 secret'))
        self.assertIn('WeirdError', text)
        self.assertNotIn('+49', text)
        self.assertNotIn('secret', text)


class Process(unittest.TestCase):
    """The real script, started the way the app starts it, up to what needs Telegram."""

    def start(self, **env):
        import subprocess
        e = dict(os.environ, MUSIX_TELEGRAM_DIR=self.dir, **env)
        for key in ('MUSIX_TELEGRAM_API_ID', 'MUSIX_TELEGRAM_API_HASH'):
            if key not in env: e.pop(key, None)
        return subprocess.run([sys.executable, str(spec.origin)], input='', capture_output=True, text=True, env=e, timeout=30)

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(); self.dir = str(Path(self.tmp.name) / 'telegram')

    def tearDown(self):
        self.tmp.cleanup()

    def test_without_credentials_it_says_so_and_asks_for_no_restart(self):
        done = self.start()
        self.assertEqual(done.returncode, 2)
        event = json.loads(done.stdout.strip().splitlines()[-1])
        self.assertEqual(event['state'], 'offline')
        self.assertIn('credentials', event['error'])

    def test_the_session_folder_is_private(self):
        self.start()
        self.assertEqual(stat.S_IMODE(os.stat(self.dir).st_mode), 0o700)

    def test_stdout_carries_only_json(self):
        done = self.start()
        for line in done.stdout.splitlines():
            json.loads(line)


if __name__ == '__main__':
    unittest.main()
