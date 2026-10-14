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
    first_name, last_name, username, id = 'Ada', 'Lovelace', 'ada', 4242


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
        from unittest import mock
        self._emit = mock.patch.object(telegram, 'emit', lambda event: self.events.append(event))
        self._emit.start()
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)
        self.events = []
        self.clients = []

    def tearDown(self):
        self._emit.stop()
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


# --- Choosing sources and syncing them -------------------------------------

class DocumentAttributeAudio:
    def __init__(self, title='', performer='', duration=0, voice=False):
        self.title, self.performer, self.duration, self.voice = title, performer, duration, voice


class DocumentAttributeFilename:
    def __init__(self, file_name): self.file_name = file_name


class Document:
    def __init__(self, mime='audio/mpeg', attributes=(), size=1000):
        self.mime_type, self.attributes, self.size = mime, list(attributes), size


class Media:
    def __init__(self, document): self.document = document


class Message:
    def __init__(self, id, document=None, date=None):
        self.id, self.media, self.date = id, Media(document) if document else None, date


def music(id, title='T', artist='A', seconds=100):
    return Message(id, Document('audio/mpeg', [DocumentAttributeAudio(title, artist, seconds), DocumentAttributeFilename('x.mp3')]))


class Channel:
    broadcast = True; forum = False; username = 'chan'
class Megagroup:
    pass
Megagroup.__name__ = 'Channel'
class User:
    pass


class Dialog:
    def __init__(self, id, name, entity): self.id, self.name, self.entity = id, name, entity


class FloodWaitError(Exception):
    def __init__(self, seconds): self.seconds = seconds


class Songs(Case):
    def ready(self):
        a = self.account(authorised=True); run(a.start()); self.events.clear(); return a

    def test_audio_is_a_song_but_a_voice_note_is_not(self):
        self.assertEqual(telegram.song_from(music(7, 'Song', 'Band', 200))['artist'], 'Band')
        self.assertEqual(telegram.song_from(music(7, 'Song', 'Band', 200))['seconds'], 200)
        self.assertIsNone(telegram.song_from(Message(1, Document('audio/ogg', [DocumentAttributeAudio(voice=True)]))))
        self.assertIsNone(telegram.song_from(Message(2)))
        self.assertIsNone(telegram.song_from(Message(3, Document('application/pdf', [DocumentAttributeFilename('a.pdf')]))))

    def test_a_document_is_a_song_by_its_type_or_its_name(self):
        by_mime = telegram.song_from(Message(4, Document('audio/flac', [DocumentAttributeFilename('Some Song.flac')])))
        self.assertEqual(by_mime['title'], 'Some Song')
        by_name = telegram.song_from(Message(5, Document('application/octet-stream', [DocumentAttributeFilename('Other.MP3')])))
        self.assertEqual(by_name['title'], 'Other')

    def test_public_names_come_from_at_names_and_links(self):
        for text in ('@some_channel', 'some_channel', 'https://t.me/some_channel', 't.me/s/some_channel', 'https://t.me/some_channel/123'):
            self.assertEqual(telegram.public_name(text), 'some_channel', text)
        for bad in ('', '@a', 'two words', 'https://t.me/+AbCdEf', 'https://t.me/joinchat/AbCd'):
            with self.assertRaises(telegram.Rejected, msg=bad):
                telegram.public_name(bad)

    def test_the_chat_list_has_saved_messages_then_channels_groups_and_forums_but_no_people(self):
        a = self.ready()
        forum = Megagroup(); forum.broadcast, forum.forum, forum.username = False, True, ''
        group = Megagroup(); group.broadcast, group.forum, group.username = False, False, ''
        a.client.iter_dialogs = lambda limit=None: self._aiter([Dialog(-1001, 'News', Channel()), Dialog(-1002, 'Friends', group),
                                                                  Dialog(-1003, 'Forum', forum), Dialog(55, 'Bob', User())])
        found = run(a.handle({'cmd': 'dialogs'}))['dialogs']
        self.assertEqual([(d['title'], d['kind']) for d in found],
                         [('Saved Messages', 'saved'), ('News', 'channel'), ('Friends', 'group'), ('Forum', 'forum')])
        self.assertEqual(found[0]['chatId'], 4242)  # Saved Messages is the account's own chat

    @staticmethod
    def _aiter(items):
        async def gen():
            for item in items:
                yield item
        return gen()

    def sync(self, account, pages, request=None, **extra):
        calls = []

        async def page(peer, kind, offset, topic):
            calls.append((kind, offset, topic))
            queue = pages.get(kind, [])
            return queue.pop(0) if queue else []
        account.page = page
        account.client.get_input_entity = lambda chat: _done('peer')
        result = run(account.handle(dict({'cmd': 'sync', 'chatId': 5, 'job': 1}, **(request or {}))))
        return result, calls

    def songs(self):
        return [s for e in self.events if e.get('event') == 'songs' for s in e['songs']]

    def test_a_sync_pages_through_music_then_documents_and_sends_each_song_once(self):
        a = self.ready()
        doc = Message(2, Document('audio/flac', [DocumentAttributeFilename('Doc.flac')]))
        result, calls = self.sync(a, {'music': [[music(9), music(8)], [music(7)]],
                                      'document': [[music(8), doc, Message(1, Document('application/pdf'))]]})
        self.assertEqual(result, {'count': 4, 'complete': True})
        self.assertEqual([s['messageId'] for s in self.songs()], [9, 8, 7, 2])
        self.assertEqual(calls, [('music', 0, 0), ('music', 8, 0), ('music', 7, 0), ('document', 0, 0), ('document', 1, 0)])

    def test_a_topic_is_searched_by_its_own_id(self):
        a = self.ready()
        _, calls = self.sync(a, {'music': [[music(3)]]}, {'topicId': 77})
        self.assertEqual({c[2] for c in calls}, {77})

    def test_a_short_flood_wait_is_waited_out_and_reported(self):
        a = self.ready()
        slept = []

        async def fast(seconds): slept.append(seconds)
        attempts = []

        async def page(peer, kind, offset, topic):
            if kind == 'document':
                return []
            attempts.append(1)
            if len(attempts) == 1:
                raise FloodWaitError(30)
            return [music(4)] if len(attempts) == 2 else []
        a.page = page
        a.client.get_input_entity = lambda chat: _done('peer')
        from unittest import mock
        with mock.patch.object(telegram.asyncio, 'sleep', fast):
            result = run(a.handle({'cmd': 'sync', 'chatId': 5, 'job': 1}))
        self.assertEqual(result['count'], 1)
        waits = [e['seconds'] for e in self.events if e.get('event') == 'wait']
        self.assertEqual(waits, [30, 0])
        self.assertIn(31, slept)

    def test_a_long_flood_wait_ends_the_sync_but_keeps_what_arrived(self):
        a = self.ready()
        calls = []

        async def page(peer, kind, offset, topic):
            calls.append(1)
            if len(calls) == 1:
                return [music(6), music(5)]
            raise FloodWaitError(7200)
        a.page = page
        a.client.get_input_entity = lambda chat: _done('peer')
        with self.assertRaises(telegram.Rejected) as caught:
            run(a.handle({'cmd': 'sync', 'chatId': 5, 'job': 1}))
        self.assertIn('2 hours', str(caught.exception))
        self.assertEqual([s['messageId'] for s in self.songs()], [6, 5])

    def test_data_requests_need_a_signed_in_account(self):
        a = self.account(); run(a.start())
        for cmd in ('dialogs', 'topics', 'resolve', 'sync'):
            with self.assertRaises(telegram.Rejected, msg=cmd):
                run(a.handle({'cmd': cmd, 'chatId': 1, 'query': '@abcd'}))

    def test_a_public_channel_is_found_by_link(self):
        a = self.ready()
        entity = Channel(); entity.id, entity.title = 123456, 'Public Music'
        asked = []

        async def get_entity(name): asked.append(name); return entity
        a.client.get_entity = get_entity
        found = run(a.handle({'cmd': 'resolve', 'query': 'https://t.me/public_music'}))['chat']
        self.assertEqual(asked, ['public_music'])
        self.assertEqual(found, {'chatId': -100123456, 'title': 'Public Music', 'kind': 'channel', 'username': 'chan'})

    def test_a_person_is_not_a_channel(self):
        a = self.ready()
        a.client.get_entity = lambda name: _done(User())
        with self.assertRaises(telegram.Rejected) as caught:
            run(a.handle({'cmd': 'resolve', 'query': '@someone'}))
        self.assertIn('person or a bot', str(caught.exception))

    def test_an_unknown_channel_says_so(self):
        a = self.ready()

        async def missing(name): raise type('UsernameNotOccupiedError', (Exception,), {})()
        a.client.get_entity = missing
        with self.assertRaises(telegram.Rejected) as caught:
            run(a.handle({'cmd': 'resolve', 'query': 't.me/nothere'}))
        self.assertIn('@nothere', str(caught.exception))

    def test_a_running_sync_can_be_cancelled(self):
        a = self.ready()

        async def scenario():
            started = asyncio.Event()

            async def page(peer, kind, offset, topic):
                started.set()
                await asyncio.sleep(30)
            a.page = page
            a.client.get_input_entity = lambda chat: _done('peer')
            task = asyncio.create_task(telegram.answer(a, {'id': 9, 'cmd': 'sync', 'chatId': 5, 'job': 4}))
            await started.wait()
            await telegram.answer(a, {'id': 10, 'cmd': 'cancel', 'job': 4})
            await task
        run(scenario())
        replies = {e['id']: e for e in self.events if 'id' in e}
        self.assertFalse(replies[9]['ok'])
        self.assertEqual(replies[9]['error'], 'Cancelled.')
        self.assertTrue(replies[10]['ok'])


class Fetching(Case):
    """Downloading one song whole, by the ids the library kept for it."""
    def setUp(self):
        super().setUp()
        self.audio = self.dir / 'audio'
        self.audio.mkdir()

    def ready(self, messages, size=None):
        a = self.account(authorised=True); run(a.start()); self.events.clear()
        a.client.downloaded = []

        async def entity(chat): return 'peer'

        async def get_messages(peer, ids=None):
            return messages.get(ids)

        async def download_file(document, out, file_size=None, progress_callback=None):
            a.client.downloaded.append(document)
            for step in (1, 2):
                out.write(b'RIFF' if step == 1 else b'')
                if progress_callback: progress_callback(step * 50, 100)
            if getattr(document, 'payload', None) is not None:
                out.seek(0); out.write(document.payload); out.truncate()
        a.client.get_input_entity, a.client.get_messages, a.client.download_file = entity, get_messages, download_file
        return a

    @staticmethod
    def sized_unknown():
        return Message(7, Document('audio/mpeg', [DocumentAttributeAudio('T', 'A', 100), DocumentAttributeFilename('x.mp3')], size=0))

    def fetch(self, a, **extra):
        return run(a.handle(dict({'cmd': 'fetch', 'chatId': -1001, 'messageId': 7, 'job': 3, 'name': 'tg_abc',
                                  'directory': str(self.audio)}, **extra)))

    @staticmethod
    def wav(path_name, tags=True):
        import io, wave
        buffer = io.BytesIO()
        w = wave.open(buffer, 'wb'); w.setnchannels(1); w.setsampwidth(2); w.setframerate(8000); w.writeframes(b'\0\0' * 16000); w.close()
        return buffer.getvalue()

    def test_the_song_is_downloaded_under_its_name_with_progress(self):
        doc = Document('audio/mpeg', [DocumentAttributeFilename('Some Song.mp3')], size=0)
        doc.payload = b'ID3-not-really-audio'
        a = self.ready({7: Message(7, doc)})
        result = self.fetch(a)
        self.assertEqual(Path(result['file']), self.audio / 'tg_abc.mp3')
        self.assertEqual((self.audio / 'tg_abc.mp3').read_bytes(), b'ID3-not-really-audio')
        self.assertEqual(list(self.audio.iterdir()), [self.audio / 'tg_abc.mp3'])
        progress = [e for e in self.events if e.get('event') == 'progress']
        self.assertEqual([(e['job'], e['received'], e['total']) for e in progress], [(3, 50, 100), (3, 100, 100)])

    def test_a_file_over_the_cap_is_refused_before_anything_is_downloaded(self):
        big = Message(7, Document('audio/flac', [DocumentAttributeFilename('Album.flac')], size=201 * 1048576))
        a = self.ready({7: big})
        with self.assertRaises(telegram.Rejected) as caught:
            self.fetch(a)
        self.assertIn('201 MB', str(caught.exception))
        self.assertIn('200 MB', str(caught.exception))
        self.assertEqual(a.client.downloaded, [])
        self.assertEqual(list(self.audio.glob('tg_abc*')), [])

    def test_a_lossless_file_under_the_cap_is_fine(self):
        doc = Document('audio/flac', [DocumentAttributeFilename('Album.flac')], size=150 * 1048576)
        doc.payload = b'x'
        a = self.ready({7: Message(7, doc)})
        with self.assertRaises(telegram.Rejected) as caught:   # the fake delivers 1 byte, not 150 MB
            self.fetch(a)
        self.assertIn('incomplete', str(caught.exception))
        self.assertEqual(list(self.audio.glob('tg_abc*')), [])

    def test_a_deleted_message_or_a_non_song_is_said_plainly(self):
        a = self.ready({7: Message(7, Document('application/pdf', [DocumentAttributeFilename('a.pdf')]))})
        for message_id in (7, 8):
            with self.assertRaises(telegram.Rejected) as caught:
                self.fetch(a, messageId=message_id)
            self.assertEqual(str(caught.exception), 'This song is no longer in the chat.')

    def test_a_name_that_could_leave_the_folder_is_refused(self):
        a = self.ready({7: self.sized_unknown()})
        for bad in ('../x', 'a/b', '', 'x.mp3'):
            with self.assertRaises(telegram.Rejected, msg=bad):
                self.fetch(a, name=bad)

    def test_a_cancelled_download_leaves_no_partial_file(self):
        a = self.ready({7: self.sized_unknown()})

        async def scenario():
            started = asyncio.Event()

            async def download_file(document, out, file_size=None, progress_callback=None):
                out.write(b'half'); started.set(); await asyncio.sleep(30)
            a.client.download_file = download_file
            task = asyncio.create_task(telegram.answer(a, {'id': 9, 'cmd': 'fetch', 'chatId': 5, 'messageId': 7, 'job': 4,
                                                           'name': 'tg_abc', 'directory': str(self.audio)}))
            await started.wait()
            await telegram.answer(a, {'id': 10, 'cmd': 'cancel', 'job': 4})
            await task
        run(scenario())
        reply = [e for e in self.events if e.get('id') == 9][0]
        self.assertEqual(reply['error'], 'Cancelled.')
        self.assertEqual(list(self.audio.glob('tg_abc*')), [])

    def test_a_flood_wait_during_a_download_is_waited_out_and_the_download_starts_again(self):
        a = self.ready({7: self.sized_unknown()})
        attempts = []

        async def sleeping(seconds): pass
        real = asyncio.sleep
        telegram.asyncio.sleep = sleeping
        self.addCleanup(setattr, telegram.asyncio, 'sleep', real)

        async def download_file(document, out, file_size=None, progress_callback=None):
            attempts.append(1)
            if len(attempts) == 1:
                out.write(b'old'); raise FloodWaitError(20)
            out.write(b'fresh')
        a.client.download_file = download_file
        result = self.fetch(a)
        self.assertEqual(Path(result['file']).read_bytes(), b'fresh')
        waits = [e['seconds'] for e in self.events if e.get('event') == 'wait']
        self.assertEqual(waits, [20, 0])

    def test_a_chat_the_session_has_not_seen_yet_is_found_through_the_chat_list(self):
        a = self.ready({7: self.sized_unknown()})
        seen = []

        async def entity(chat):
            if not seen: raise ValueError('not cached')
            return 'peer'
        a.client.get_input_entity = entity
        a.client.iter_dialogs = lambda limit=None: self._chats(seen)
        result = self.fetch(a)
        self.assertEqual(seen, ['listed'])
        self.assertTrue(Path(result['file']).is_file())

    @staticmethod
    def _chats(seen):
        async def gen():
            seen.append('listed')
            if False: yield
        return gen()

    @unittest.skipUnless(importlib.util.find_spec('mutagen'), 'mutagen is in the project runtime (helper/requirements.txt), not the system Python')
    def test_the_files_own_tags_and_length_are_read_for_the_app_to_fill_in(self):
        import wave
        from mutagen.id3 import TIT2, TPE1
        from mutagen.wave import WAVE
        path = self.dir / 'tagged.wav'
        w = wave.open(str(path), 'wb'); w.setnchannels(1); w.setsampwidth(2); w.setframerate(8000); w.writeframes(b'\0\0' * 16000); w.close()
        f = WAVE(str(path)); f.add_tags(); f.tags.add(TIT2(encoding=3, text='Tagged')); f.tags.add(TPE1(encoding=3, text='Band')); f.save()
        self.assertEqual(telegram.read_tags(path), {'seconds': 2, 'title': 'Tagged', 'artist': 'Band'})

    def test_a_file_mutagen_cannot_read_has_no_tags_rather_than_an_error(self):
        path = self.dir / 'junk.mp3'
        path.write_bytes(b'not audio at all')
        self.assertEqual(telegram.read_tags(path), {})
        self.assertEqual(telegram.read_tags(self.dir / 'missing.mp3'), {})

    def test_the_extension_follows_the_name_then_the_type(self):
        self.assertEqual(telegram.audio_extension('A.FLAC', 'application/octet-stream'), '.flac')
        self.assertEqual(telegram.audio_extension('noext', 'audio/mpeg'), '.mp3')
        self.assertEqual(telegram.audio_extension('', 'audio/x-weird'), '.audio')


async def _done(value):
    return value


if __name__ == '__main__':
    unittest.main()
