#!/usr/bin/env python3
"""Musix's Telegram account, kept signed in for as long as Musix wants it.

catalog.py answers one request and exits. A Telegram login cannot work that
way: the code Telegram sends belongs to a connection and a hash that only this
process holds, and a second step (the two-step password) follows the first on
the same session. So this one stays up, reads one JSON object per line on
stdin and writes one per line on stdout. Nothing else may print to stdout; the
C++ side parses every line it gets. Telethon's own logging goes to stderr and
never carries a phone number, a code or a password, and neither does anything
written here.

Requests   {"id": 1, "cmd": "phone", "phone": "+49 151 2345678"}
           cmd: status | phone | code | password | back | logout
                dialogs | topics | resolve | sync | cancel   (signed in only)
Replies    {"id": 1, "ok": true[, "result": {...}]}   or   {"id": 1, "ok": false, "error": "..."}
Events     {"event": "state", "state": "...", ...}
           {"event": "songs", "job": n, "songs": [...]}   one batch of a sync
           {"event": "progress", "job": n, "scanned": n}
           {"event": "wait", "job": n, "seconds": n}      Telegram asked for a pause; 0 when over
States     starting, offline, wait-phone, wait-code, wait-password, ready

Data requests run beside each other (a sync of a big channel must not stop the
chat list from loading); the sign-in steps run one at a time, in order.

The session is a file in MUSIX_TELEGRAM_DIR, readable by its owner only: it is
as good as a password for the account, so it is not copied anywhere else.
"""
import asyncio
import json
import os
import platform
import re
import sys
from pathlib import Path

ACCOUNT_FILE = 'account.json'
SESSION_NAME = 'session'
BATCH = 100
MAX_SONGS = 20000
# A pause Telegram asks for (FLOOD_WAIT) is waited out when it is short, with
# the person told; a longer one ends the request and the person is told when to
# come back. Telethon would sleep silently for anything under a minute.
FLOOD_LIMIT = 180
AUDIO_EXTENSIONS = ('.mp3', '.flac', '.m4a', '.ogg', '.opus', '.wav', '.aac', '.wma', '.alac', '.aiff', '.ape')
DATA_COMMANDS = ('dialogs', 'topics', 'resolve', 'sync')


class Rejected(Exception):
    """A request that fails in a way the person can act on."""


def describe(error):
    """What to tell the person about a Telegram error, without echoing it.

    Matched on the class name so this file imports nothing from Telethon until
    it has to: the messages are the same whichever Telethon raised them.
    """
    name = type(error).__name__
    plain = {
        'PhoneNumberInvalidError': 'That phone number isn’t valid. Use the international form, like +49 151 2345678.',
        'PhoneNumberBannedError': 'Telegram has banned this phone number.',
        'PhoneNumberFloodError': 'Too many attempts with this number. Try again later.',
        'PhoneNumberUnoccupiedError': 'There is no Telegram account for this number.',
        'PhoneCodeInvalidError': 'That code isn’t right.',
        'PhoneCodeEmptyError': 'Enter the code Telegram sent you.',
        'PhoneCodeExpiredError': 'That code has expired. Ask for a new one.',
        'PasswordHashInvalidError': 'That password isn’t right.',
        'ApiIdInvalidError': 'Telegram refused the API credentials this build carries.',
        'AuthKeyUnregisteredError': 'Telegram ended this session. Sign in again.',
        'SessionRevokedError': 'Telegram ended this session. Sign in again.',
        'SessionExpiredError': 'Telegram ended this session. Sign in again.',
        'UserDeactivatedError': 'This Telegram account has been deactivated.',
        'UserDeactivatedBanError': 'This Telegram account has been banned.',
    }
    if name in plain:
        return plain[name]
    if name in ('FloodWaitError', 'FloodError', 'SlowModeWaitError'):
        return 'Too many attempts. Try again in %s.' % wait_text(getattr(error, 'seconds', 0))
    if isinstance(error, (ConnectionError, TimeoutError, asyncio.TimeoutError, OSError)):
        return 'Couldn’t reach Telegram. Check your connection and try again.'
    return 'Telegram returned an error (%s).' % name


def wait_text(seconds):
    seconds = int(seconds or 0)
    if seconds >= 3600:
        hours = (seconds + 3599) // 3600
        return '%d hour%s' % (hours, '' if hours == 1 else 's')
    if seconds >= 60:
        minutes = (seconds + 59) // 60
        return '%d minute%s' % (minutes, '' if minutes == 1 else 's')
    return '%d second%s' % (max(seconds, 1), '' if seconds == 1 else 's')


def clean_phone(raw):
    digits = re.sub(r'[\s\-().]', '', str(raw or ''))
    if digits.startswith('00'):
        digits = '+' + digits[2:]
    elif digits and not digits.startswith('+'):
        digits = '+' + digits
    if not re.fullmatch(r'\+\d{7,15}', digits):
        raise Rejected('That phone number isn’t valid. Use the international form, like +49 151 2345678.')
    return digits


def clean_code(raw):
    code = re.sub(r'[\s\-]', '', str(raw or ''))
    if not re.fullmatch(r'\d{4,8}', code):
        raise Rejected('The code is the 5 digits Telegram sent you.')
    return code


def code_kind(sent):
    kind = type(getattr(sent, 'type', None)).__name__
    return {'SentCodeTypeApp': 'app', 'SentCodeTypeSms': 'sms', 'SentCodeTypeCall': 'call',
            'SentCodeTypeFlashCall': 'call'}.get(kind, 'other')


def attribute(document, name):
    for found in getattr(document, 'attributes', None) or []:
        if type(found).__name__ == name:
            return found
    return None


def song_from(message):
    """A song row for a message that carries audio, or None. Voice notes are not songs."""
    document = getattr(getattr(message, 'media', None), 'document', None)
    if document is None:
        return None
    audio = attribute(document, 'DocumentAttributeAudio')
    if audio is not None and getattr(audio, 'voice', False):
        return None
    name = getattr(attribute(document, 'DocumentAttributeFilename'), 'file_name', '') or ''
    mime = getattr(document, 'mime_type', '') or ''
    if audio is None and not (mime.startswith('audio/') or mime == 'application/ogg' or name.lower().endswith(AUDIO_EXTENSIONS)):
        return None
    date = getattr(message, 'date', None)
    return {
        'messageId': message.id,
        'title': (getattr(audio, 'title', '') or Path(name).stem or '').strip() or 'Untitled',
        'artist': (getattr(audio, 'performer', '') or '').strip(),
        'seconds': int(getattr(audio, 'duration', 0) or 0),
        'mime': mime,
        'size': int(getattr(document, 'size', 0) or 0),
        'date': int(date.timestamp()) if date else 0,
        'fileName': name,
    }


def public_name(query):
    """The username in @name, a bare name, or a t.me link; invite links are not public."""
    text = str(query or '').strip()
    if re.search(r't(?:elegram)?\.me/(?:\+|joinchat/)', text, re.I) or text.startswith('+'):
        raise Rejected('That’s an invite link. Join the chat in Telegram, then pick it from your chats.')
    found = re.search(r't(?:elegram)?\.me/(?:s/)?([A-Za-z][A-Za-z0-9_]{3,})', text, re.I)
    if found:
        return found.group(1)
    text = text.lstrip('@')
    if re.fullmatch(r'[A-Za-z][A-Za-z0-9_]{3,}', text):
        return text
    raise Rejected('Enter a channel as @name or a t.me link.')


def marked_id(entity):
    """The id a chat goes by everywhere else: channels and supergroups are -100…, small groups -…"""
    if type(entity).__name__ == 'Channel':
        return int('-100%d' % entity.id)
    return -int(entity.id)


def chat_kind(entity):
    name = type(entity).__name__
    if name == 'Channel':
        if getattr(entity, 'broadcast', False):
            return 'channel'
        return 'forum' if getattr(entity, 'forum', False) else 'group'
    if name == 'Chat':
        return 'group'
    return ''


class Account:
    def __init__(self, make_client, directory, emit):
        self.make_client = make_client
        self.directory = Path(directory)
        self.emit = emit
        self.client = None
        self.phone = None
        self.code_hash = None
        self.kind = ''
        self.hint = ''
        self.state = 'starting'
        self.extra = {}
        self.me = {}
        self.jobs = {}

    def set_state(self, state, **extra):
        self.state = state
        self.extra = extra
        event = {'event': 'state', 'state': state}
        if state == 'ready':
            event.update(self.me)
        elif state == 'wait-code':
            event['codeKind'] = self.kind
        elif state == 'wait-password':
            event['passwordHint'] = self.hint
        event.update(extra)
        self.emit(event)

    def forget_files(self):
        for name in (ACCOUNT_FILE, SESSION_NAME + '.session', SESSION_NAME + '.session-journal'):
            try:
                (self.directory / name).unlink()
            except FileNotFoundError:
                pass

    async def start(self):
        self.client = self.make_client()
        try:
            # Pauses are handled here, where the person can be told (guarded).
            self.client.flood_sleep_threshold = 0
        except Exception:
            pass
        try:
            await self.client.connect()
        except Exception as error:
            self.set_state('offline', error=describe(error))
            return
        try:
            authorised = await self.client.is_user_authorized()
        except Exception as error:
            self.set_state('offline', error=describe(error))
            return
        if authorised:
            await self.ready()
        else:
            self.set_state('wait-phone')

    async def ready(self):
        me = await self.client.get_me()
        name = ' '.join(part for part in (getattr(me, 'first_name', ''), getattr(me, 'last_name', '')) if part)
        self.me = {'name': name or getattr(me, 'username', '') or 'Telegram', 'username': getattr(me, 'username', '') or '',
                   'userId': getattr(me, 'id', 0) or 0}
        (self.directory / ACCOUNT_FILE).write_text(json.dumps(self.me, ensure_ascii=False))
        self.set_state('ready')

    async def handle(self, request):
        cmd = request.get('cmd')
        if cmd == 'status':
            self.set_state(self.state, **self.extra)
        elif cmd == 'phone':
            await self.send_phone(clean_phone(request.get('phone')))
        elif cmd == 'code':
            await self.send_code(clean_code(request.get('code')))
        elif cmd == 'password':
            await self.send_password(str(request.get('password') or ''))
        elif cmd == 'back':
            self.phone = self.code_hash = None
            self.set_state('wait-phone')
        elif cmd == 'logout':
            await self.logout()
        elif cmd == 'cancel':
            task = self.jobs.get(request.get('job'))
            if task is not None:
                task.cancel()
        elif cmd in DATA_COMMANDS:
            self.require('ready')
            return await getattr(self, 'do_' + cmd)(request)
        else:
            raise Rejected('Unknown request.')
        return None

    async def guarded(self, call, job=None):
        """Run a request, waiting out a short FLOOD_WAIT and reporting it."""
        for attempt in range(6):
            try:
                return await call()
            except Exception as error:
                if type(error).__name__ not in ('FloodWaitError', 'SlowModeWaitError', 'FloodError'):
                    raise
                seconds = int(getattr(error, 'seconds', 0) or 0)
                if seconds > FLOOD_LIMIT or attempt == 5:
                    raise Rejected('Telegram asks to wait %s before more requests. Try again then.' % wait_text(seconds))
                self.emit({'event': 'wait', 'job': job, 'seconds': seconds})
                await asyncio.sleep(seconds + 1)
                self.emit({'event': 'wait', 'job': job, 'seconds': 0})

    async def do_dialogs(self, request):
        me = await self.guarded(self.client.get_me)
        rows = [{'chatId': me.id, 'title': 'Saved Messages', 'kind': 'saved', 'username': ''}]

        async def collect():
            found = []
            async for dialog in self.client.iter_dialogs(limit=400):
                kind = chat_kind(dialog.entity)
                if kind:
                    found.append({'chatId': dialog.id, 'title': dialog.name or 'Untitled', 'kind': kind,
                                  'username': getattr(dialog.entity, 'username', '') or ''})
            return found
        return {'dialogs': rows + await self.guarded(collect)}

    async def do_topics(self, request):
        from telethon.tl import functions
        peer = await self.client.get_input_entity(int(request['chatId']))
        topics, offset_date, offset_id, offset_topic = [], None, 0, 0
        while len(topics) < 500:
            result = await self.guarded(lambda: self.client(functions.messages.GetForumTopicsRequest(
                peer=peer, offset_date=offset_date, offset_id=offset_id, offset_topic=offset_topic, limit=100)))
            batch = [t for t in result.topics if type(t).__name__ == 'ForumTopic']
            topics += [{'topicId': t.id, 'title': t.title} for t in batch]
            if len(result.topics) < 100 or not batch:
                break
            last = batch[-1]
            top = {m.id: m for m in result.messages}.get(last.top_message)
            if top is None:
                break
            offset_date, offset_id, offset_topic = top.date, last.top_message, last.id
        return {'topics': topics}

    async def do_resolve(self, request):
        name = public_name(request.get('query'))
        try:
            entity = await self.guarded(lambda: self.client.get_entity(name))
        except Rejected:
            raise
        except Exception as error:
            if type(error).__name__ in ('UsernameNotOccupiedError', 'UsernameInvalidError', 'ValueError'):
                raise Rejected('There is no public channel called @%s.' % name)
            raise
        kind = chat_kind(entity)
        if not kind:
            raise Rejected('@%s is a person or a bot, not a channel or group.' % name)
        return {'chat': {'chatId': marked_id(entity), 'title': getattr(entity, 'title', '') or name,
                         'kind': kind, 'username': getattr(entity, 'username', '') or name}}

    async def page(self, peer, kind, offset, topic):
        """One page of messages, newest first, from `offset` (a message id) on."""
        from telethon.tl import functions, types
        found = types.InputMessagesFilterMusic() if kind == 'music' else types.InputMessagesFilterDocument()
        result = await self.client(functions.messages.SearchRequest(
            peer=peer, q='', filter=found, min_date=None, max_date=None, offset_id=offset, add_offset=0,
            limit=BATCH, max_id=0, min_id=0, hash=0, top_msg_id=topic or None))
        return list(getattr(result, 'messages', None) or [])

    async def do_sync(self, request):
        """Every song in a chat, or in one forum topic, sent a page at a time.

        Music-tagged audio first, then documents that are audio by type or
        name; a song can turn up in both, and is sent once.
        """
        job = request.get('job')
        topic = int(request.get('topicId') or 0)
        peer = await self.client.get_input_entity(int(request['chatId']))
        seen, total, scanned, complete = set(), 0, 0, True
        for kind in ('music', 'document'):
            offset = 0
            while complete:
                messages = await self.guarded(lambda: self.page(peer, kind, offset, topic), job)
                if not messages:
                    break
                scanned += len(messages)
                songs = []
                for message in messages:
                    if message.id in seen:
                        continue
                    seen.add(message.id)
                    song = song_from(message)
                    if song:
                        songs.append(song)
                offset = messages[-1].id
                total += len(songs)
                if songs:
                    self.emit({'event': 'songs', 'job': job, 'songs': songs})
                self.emit({'event': 'progress', 'job': job, 'scanned': scanned})
                if total >= MAX_SONGS:
                    complete = False
                await asyncio.sleep(0.15)
        return {'count': total, 'complete': complete}

    def require(self, *states):
        if self.state not in states:
            raise Rejected('That isn’t what Telegram is waiting for now.')

    async def send_phone(self, phone):
        self.require('wait-phone')
        try:
            sent = await self.client.send_code_request(phone)
        except Exception as error:
            raise Rejected(describe(error))
        self.phone, self.code_hash, self.kind = phone, sent.phone_code_hash, code_kind(sent)
        self.set_state('wait-code')

    async def send_code(self, code):
        self.require('wait-code')
        try:
            await self.client.sign_in(self.phone, code, phone_code_hash=self.code_hash)
        except Exception as error:
            name = type(error).__name__
            if name == 'SessionPasswordNeededError':
                self.hint = await self.password_hint()
                self.set_state('wait-password')
                return
            if name in ('PhoneCodeExpiredError', 'PhoneCodeEmptyError'):
                self.phone = self.code_hash = None
                self.set_state('wait-phone', error=describe(error))
                return
            raise Rejected(describe(error))
        await self.ready()

    async def password_hint(self):
        try:
            from telethon.tl import functions
            return (await self.client(functions.account.GetPasswordRequest())).hint or ''
        except Exception:
            return ''

    async def send_password(self, password):
        self.require('wait-password')
        if not password:
            raise Rejected('Enter your Telegram password.')
        try:
            await self.client.sign_in(password=password)
        except Exception as error:
            raise Rejected(describe(error))
        await self.ready()

    async def logout(self):
        for task in list(self.jobs.values()):
            task.cancel()
        warning = ''
        try:
            if self.client is not None:
                await self.client.log_out()
        except Exception:
            warning = ('Couldn’t reach Telegram, so this Mac forgot the session but Telegram still lists it. '
                       'End it under Settings > Devices in Telegram.')
        try:
            await self.client.disconnect()
        except Exception:
            pass
        self.forget_files()
        self.me = {}
        self.phone = self.code_hash = None
        await self.start()
        if warning and self.state == 'wait-phone':
            self.set_state('wait-phone', error=warning)

    async def stop(self):
        try:
            if self.client is not None:
                await self.client.disconnect()
        except Exception:
            pass


def emit(event):
    sys.stdout.write(json.dumps(event, ensure_ascii=False) + '\n')
    sys.stdout.flush()


def real_client_factory(directory, api_id, api_hash):
    from telethon import TelegramClient

    def make():
        return TelegramClient(
            str(directory / SESSION_NAME), api_id, api_hash,
            device_model='Musix', system_version=platform.mac_ver()[0] or platform.release(),
            app_version=os.environ.get('MUSIX_VERSION', 'dev'), lang_code='en', system_lang_code='en',
            connection_retries=2, retry_delay=1, request_retries=2, timeout=15)
    return make


async def answer(account, request):
    reply = {'id': request.get('id')}
    job = request.get('job')
    if job is not None:
        account.jobs[job] = asyncio.current_task()
    try:
        result = await account.handle(request)
        reply['ok'] = True
        if result is not None:
            reply['result'] = result
    except Rejected as error:
        reply.update(ok=False, error=str(error))
    except asyncio.CancelledError:
        reply.update(ok=False, error='Cancelled.')
    except Exception as error:
        reply.update(ok=False, error=describe(error))
    finally:
        if job is not None:
            account.jobs.pop(job, None)
    emit(reply)


async def serve(account, lines):
    running = set()
    async for line in lines:
        try:
            request = json.loads(line)
        except ValueError:
            continue
        if request.get('cmd') in DATA_COMMANDS:
            task = asyncio.create_task(answer(account, request))
            running.add(task)
            task.add_done_callback(running.discard)
        else:
            await answer(account, request)
    for task in list(running):
        task.cancel()
    if running:
        await asyncio.gather(*running, return_exceptions=True)


async def stdin_lines():
    loop = asyncio.get_running_loop()
    reader = asyncio.StreamReader()
    await loop.connect_read_pipe(lambda: asyncio.StreamReaderProtocol(reader), sys.stdin)
    while True:
        line = await reader.readline()
        if not line:
            return
        yield line


async def main():
    os.umask(0o077)
    directory = Path(os.environ.get('MUSIX_TELEGRAM_DIR') or '')
    if not str(directory):
        emit({'event': 'state', 'state': 'offline', 'error': 'No place to keep the session.'})
        return 2
    directory.mkdir(parents=True, exist_ok=True)
    os.chmod(directory, 0o700)
    try:
        api_id = int(os.environ.get('MUSIX_TELEGRAM_API_ID') or 0)
    except ValueError:
        api_id = 0
    api_hash = os.environ.get('MUSIX_TELEGRAM_API_HASH') or ''
    if not api_id or not api_hash:
        emit({'event': 'state', 'state': 'offline', 'error': 'This build has no Telegram API credentials.'})
        return 2
    try:
        factory = real_client_factory(directory, api_id, api_hash)
    except ImportError:
        emit({'event': 'state', 'state': 'offline', 'error': 'Telegram support isn’t installed in this Python.'})
        return 2
    account = Account(factory, directory, emit)
    await account.start()
    try:
        await serve(account, stdin_lines())
    finally:
        await account.stop()
    return 0


if __name__ == '__main__':
    import logging
    logging.basicConfig(stream=sys.stderr, level=logging.WARNING)
    sys.exit(asyncio.run(main()))
