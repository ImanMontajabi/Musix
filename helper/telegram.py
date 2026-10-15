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
                dialogs | topics | resolve | sync | fetch | stream | art | cancel   (signed in only)
Replies    {"id": 1, "ok": true[, "result": {...}]}   or   {"id": 1, "ok": false, "error": "..."}
Events     {"event": "state", "state": "...", ...}
           {"event": "songs", "job": n, "songs": [...]}   one batch of a sync
           {"event": "progress", "job": n, "scanned": n}     a sync
           {"event": "progress", "job": n, "received": n, "total": n}   a fetch
           {"event": "wait", "job": n, "seconds": n}      Telegram asked for a pause; 0 when over
States     starting, offline, wait-phone, wait-code, wait-password, ready

A fetch with "stream": true brings the song down in blocks, in the order the
player reads them as well as front to back, and a `stream` request naming its
job answers with a URL on a loopback HTTP server (127.0.0.1, a random port, a
random token in the path, Range supported) that serves the song from those
blocks while they arrive. The fetch ends, as any other, when the file is whole.

Data requests run beside each other (a sync of a big channel must not stop the
chat list from loading); the sign-in steps run one at a time, in order.

The session is a file in MUSIX_TELEGRAM_DIR, readable by its owner only: it is
as good as a password for the account, so it is not copied anywhere else.
"""
import asyncio
import hmac
import json
import mimetypes
import os
import platform
import re
import secrets
import sys
from pathlib import Path

ACCOUNT_FILE = 'account.json'
SESSION_NAME = 'session'
BATCH = 100
MAX_SONGS = 20000
# Telegram is where people keep lossless albums, so the cap is far above what a
# streamed song needs. Past it the song is refused rather than half downloaded.
MAX_FETCH = 200 * 1024 * 1024
# The picture a Telegram song or chat shows. The app's image loader refuses a
# file over 2 MB, so a bigger cover is not worth keeping.
MAX_ART = 1800 * 1024
# How much of the front of a file is read to find a cover inside it, and the
# size of each request for it (Telegram wants multiples of 4 KB).
MAX_HEADER = 512 * 1024
HEADER_CHUNK = 128 * 1024
# A song comes down in blocks of this size: the most one request can ask for,
# and a divisor of 1 MiB, which Telegram requires of where a request starts.
BLOCK = 512 * 1024
# Blocks kept in flight beside whatever the player is waiting for.
FILL_AHEAD = 2
MIME_EXTENSIONS = {'audio/mpeg': '.mp3', 'audio/mp3': '.mp3', 'audio/flac': '.flac', 'audio/x-flac': '.flac',
                   'audio/mp4': '.m4a', 'audio/x-m4a': '.m4a', 'audio/aac': '.aac', 'audio/ogg': '.ogg',
                   'application/ogg': '.ogg', 'audio/opus': '.opus', 'audio/wav': '.wav', 'audio/x-wav': '.wav',
                   'audio/vnd.wave': '.wav', 'audio/x-ms-wma': '.wma', 'audio/aiff': '.aiff', 'audio/x-aiff': '.aiff'}
# A pause Telegram asks for (FLOOD_WAIT) is waited out when it is short, with
# the person told; a longer one ends the request and the person is told when to
# come back. Telethon would sleep silently for anything under a minute.
FLOOD_LIMIT = 180
AUDIO_EXTENSIONS = ('.mp3', '.flac', '.m4a', '.ogg', '.opus', '.wav', '.aac', '.wma', '.alac', '.aiff', '.ape')
DATA_COMMANDS = ('dialogs', 'topics', 'resolve', 'sync', 'fetch', 'stream', 'art')


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
        'thumb': bool(getattr(document, 'thumbs', None)),
    }


def audio_extension(file_name, mime):
    """The extension a downloaded song is kept under: the file's own, else the type's."""
    suffix = Path(file_name or '').suffix.lower()
    if suffix in AUDIO_EXTENSIONS:
        return suffix
    return MIME_EXTENSIONS.get((mime or '').lower(), '.audio')


def read_tags(path):
    """What the file itself says about the song, or {} when mutagen can't read it.

    The sync only knows what the message's audio attribute carried, which for a
    plain document is nothing: no duration, no artist, the file name for a title.
    """
    try:
        import mutagen
        found = mutagen.File(str(path), easy=True)
    except Exception:
        return {}
    if found is None:
        return {}
    tags = {}
    length = getattr(getattr(found, 'info', None), 'length', 0) or 0
    if length > 0:
        tags['seconds'] = int(round(length))

    def first(key, frame):
        # Easy tags where mutagen has them, ID3 frames for formats it has no
        # easy form of (WAV, AIFF).
        try:
            value = found.tags.get(key) or found.tags.get(frame) if found.tags is not None else None
        except Exception:
            return ''
        if value is None:
            return ''
        value = getattr(value, 'text', value)
        return str(value[0] if isinstance(value, (list, tuple)) and value else value if not isinstance(value, (list, tuple)) else '').strip()
    for key, frame in (('title', 'TIT2'), ('artist', 'TPE1'), ('album', 'TALB')):
        if first(key, frame):
            tags[key] = first(key, frame)
    return tags


def image_type(data):
    """'.jpg', '.png' or '.webp' for bytes that really are one of those, else ''."""
    if data[:3] == b'\xff\xd8\xff':
        return '.jpg'
    if data[:8] == b'\x89PNG\r\n\x1a\n':
        return '.png'
    if data[:4] == b'RIFF' and data[8:12] == b'WEBP':
        return '.webp'
    return ''


def embedded_cover(path):
    """The picture inside an audio file as (bytes, extension), or None."""
    try:
        import base64
        import mutagen
        try:
            found = mutagen.File(str(path))
        except Exception:
            found = None
        candidates = []
        if found is None:
            # The front of an MP3 has its tag but no audio frames to recognise.
            from mutagen.id3 import ID3
            found = ID3(str(path))
            candidates += [(frame.type, frame.data) for frame in found.getall('APIC')]
            found = None
        if getattr(found, 'pictures', None):                      # FLAC
            candidates += [(p.type, p.data) for p in found.pictures]
        tags = getattr(found, 'tags', None)
        if tags is not None:
            if hasattr(tags, 'getall'):                           # ID3: MP3, WAV, AIFF
                candidates += [(frame.type, frame.data) for frame in tags.getall('APIC')]
            try:
                candidates += [(3, bytes(cover)) for cover in tags.get('covr', [])]   # MP4
            except Exception:
                pass
            try:
                from mutagen.flac import Picture
                for block in tags.get('metadata_block_picture', []) or []:  # Ogg, Opus
                    picture = Picture(base64.b64decode(block))
                    candidates.append((picture.type, picture.data))
            except Exception:
                pass
        # The front cover, else whatever comes first.
        candidates.sort(key=lambda c: 0 if c[0] == 3 else 1)
        for _, data in candidates:
            if 0 < len(data) <= MAX_ART and image_type(data):
                return data, image_type(data)
    except Exception:
        return None
    return None


def header_needed(head):
    """How many bytes from the start of a file hold all of its tags, as far as `head` shows.

    A lower bound that the caller raises as it reads on: an ID3 tag says its
    size at once, FLAC's blocks are walked one by one, and an MP4 has its tags
    in `moov`, which may come after the audio, in which case nothing in the
    front is worth reading (0). None means the format puts them in the first
    request, or is not one this knows how to measure.
    """
    if head[:3] == b'ID3' and len(head) >= 10:
        size = ((head[6] & 0x7f) << 21) | ((head[7] & 0x7f) << 14) | ((head[8] & 0x7f) << 7) | (head[9] & 0x7f)
        return 10 + size + (10 if head[5] & 0x10 else 0)
    if head[:4] == b'fLaC':
        position = 4
        while position + 4 <= len(head):
            last, length = head[position] & 0x80, int.from_bytes(head[position + 1:position + 4], 'big')
            position += 4 + length
            if last:
                return position
        return position + 4
    if head[4:8] == b'ftyp':
        position = 0
        while position + 8 <= len(head):
            size, kind = int.from_bytes(head[position:position + 4], 'big'), head[position + 4:position + 8]
            if size < 8:
                return None
            if kind == b'moov':
                return position + size
            if kind == b'mdat':
                return 0
            position += size
        return position + 8
    return None


def png_bytes(width, height, pixel):
    """A PNG written by hand, so the helper needs no imaging library."""
    import struct
    import zlib
    rows = b''.join(b'\x00' + b''.join(bytes(pixel(x, y)) for x in range(width)) for y in range(height))

    def chunk(kind, body):
        block = kind + body
        return struct.pack('>I', len(body)) + block + struct.pack('>I', zlib.crc32(block) & 0xffffffff)
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(rows, 9)) + chunk(b'IEND', b''))


def saved_messages_cover():
    """Saved Messages has no photo; a bookmark on a calm blue stands for it."""
    size, ground, ink = 256, (58, 79, 128), (238, 241, 250)

    def inside(x, y):   # the bookmark, in a 256 square: a bar whose foot is notched
        if not (84 <= x <= 172 and 52 <= y <= 204):
            return False
        return y <= 204 - 40 + abs(x - 128) * 40 / 44 if y > 164 else True

    def pixel(x, y):
        hits = sum(inside(x + dx / 2, y + dy / 2) for dx in (0, 1) for dy in (0, 1))
        return tuple((ground[i] * (4 - hits) + ink[i] * hits) // 4 for i in range(3))
    return png_bytes(size, size, pixel)


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


class Transfer:
    """One song coming down in blocks, readable while it does.

    Blocks are fetched wherever the player reads (a seek lands anywhere) and,
    beside that, from the last place it read to the end and then round to the
    start, so the whole file is on disk soon after playback began. It is written
    to a `.part` file that becomes the song only when every block is there.
    """
    def __init__(self, account, job, name, document, size, mime, part, target):
        self.account, self.job, self.name, self.document = account, job, name, document
        self.size, self.mime, self.part, self.target = size, mime, part, target
        self.count = -(-size // BLOCK)
        self.have = bytearray(self.count)
        self.waiting = {}
        self.cursor = 0
        self.received = 0
        self.closed = False
        self.complete = False
        self.fd = os.open(part, os.O_RDWR | os.O_CREAT | os.O_TRUNC, 0o600)
        os.ftruncate(self.fd, size)

    def next_missing(self):
        for step in range(self.count):
            index = (self.cursor + step) % self.count
            if not self.have[index] and index not in self.waiting:
                return index
        return None

    async def block(self, index):
        if self.have[index]:
            return
        if self.closed:
            raise asyncio.CancelledError()
        task = self.waiting.get(index)
        if task is None:
            task = self.waiting[index] = asyncio.ensure_future(self.download(index))
            task.add_done_callback(lambda finished: finished.cancelled() or finished.exception())   # seen, even if nobody is waiting any more
        # A reader that leaves must not cancel a block somebody else will want.
        await asyncio.shield(task)

    async def download(self, index):
        start = index * BLOCK
        wanted = min(BLOCK, self.size - start)

        async def read():
            data = b''
            async for chunk in self.account.client.iter_download(self.document, offset=start, request_size=BLOCK, limit=1, file_size=self.size):
                data += bytes(chunk)
            return data
        try:
            data = await self.account.guarded(read, self.job)
            if len(data) < wanted:
                raise Rejected('Telegram sent an incomplete file. Try again.')
            if not self.closed:
                os.pwrite(self.fd, data[:wanted], start)
                self.have[index] = 1
                self.received += wanted
                self.account.emit({'event': 'progress', 'job': self.job, 'received': self.received, 'total': self.size})
        finally:
            self.waiting.pop(index, None)

    async def read(self, offset, length):
        """Up to `length` bytes at `offset`, within one block, waiting for it to arrive."""
        index = offset // BLOCK
        self.cursor = index
        await self.block(index)
        end = min(self.size, offset + length, (index + 1) * BLOCK)
        return os.pread(self.fd, end - offset, offset)

    async def run(self):
        """Bring the whole file down; returns when it is the song."""
        running = set()
        try:
            while True:
                while len(running) < FILL_AHEAD:
                    index = self.next_missing()
                    if index is None:
                        break
                    running.add(asyncio.ensure_future(self.block(index)))
                if not running:
                    if self.waiting:                  # blocks only a reader is waiting for
                        await asyncio.wait(list(self.waiting.values()))
                        continue
                    break
                done, running = await asyncio.wait(running, return_when=asyncio.FIRST_COMPLETED)
                failure = None
                for finished in done:
                    try:
                        finished.result()
                    except Exception as error:
                        failure = failure or error
                if failure is not None:
                    raise failure
            if sum(self.have) != self.count:
                raise Rejected('Telegram sent an incomplete file. Try again.')
            os.replace(self.part, self.target)
            self.complete = True
        finally:
            for task in running:
                task.cancel()
            if not self.complete:
                self.close()
            if running:
                await asyncio.gather(*running, return_exceptions=True)

    def close(self):
        """Stop: whatever is in flight is dropped, and a half-song is not kept."""
        self.closed = True
        for task in list(self.waiting.values()):
            task.cancel()
        if self.fd >= 0:
            os.close(self.fd)
            self.fd = -1
        if not self.complete:
            try:
                self.part.unlink()
            except FileNotFoundError:
                pass


def parse_range(header, size):
    """(first, last) of the one range asked for, None for the whole file; ValueError if it can't be had."""
    if not header:
        return None
    found = re.fullmatch(r'\s*bytes\s*=\s*(\d*)\s*-\s*(\d*)\s*', header)
    if not found:
        return None                      # several ranges, or something else: the whole file is a valid answer
    first, last = found.groups()
    if not first and not last:
        return None
    if not first:
        length = int(last)
        if length == 0:
            raise ValueError('empty')
        return max(0, size - length), size - 1
    start = int(first)
    end = min(int(last), size - 1) if last else size - 1
    if start >= size or end < start:
        raise ValueError('outside')
    return start, end


class StreamServer:
    """The songs being fetched, over HTTP on this machine, for the player to open.

    It listens on 127.0.0.1 only, on a port the system picks, and answers only
    to a path that carries a random token made when it started; another program
    on the Mac that does not know the token gets a 404 like any other path.
    """
    KEPT = 6

    def __init__(self):
        self.token = secrets.token_urlsafe(24)
        self.server = None
        self.port = 0
        self.transfers = {}

    async def url(self, transfer):
        if self.server is None:
            self.server = await asyncio.start_server(self.connection, '127.0.0.1', 0, backlog=16)
            self.port = self.server.sockets[0].getsockname()[1]
        key = transfer.name + (transfer.target.suffix or '.audio')
        self.transfers.pop(key, None)
        self.transfers[key] = transfer
        for old in list(self.transfers)[:-self.KEPT]:
            self.transfers.pop(old).close()
        return 'http://127.0.0.1:%d/%s/%s' % (self.port, self.token, key)

    def forget(self, transfer):
        for key, held in list(self.transfers.items()):
            if held is transfer:
                del self.transfers[key]

    async def stop(self):
        if self.server is not None:
            self.server.close()
        for transfer in list(self.transfers.values()):
            transfer.close()
        self.transfers.clear()

    async def connection(self, reader, writer):
        try:
            while await self.request(reader, writer):
                pass
        except (ConnectionError, asyncio.IncompleteReadError, asyncio.TimeoutError, OSError):
            pass
        except asyncio.CancelledError:
            pass
        finally:
            try:
                writer.close()
            except Exception:
                pass

    @staticmethod
    def reply(writer, status, headers=(), body=b''):
        names = {200: 'OK', 206: 'Partial Content', 400: 'Bad Request', 404: 'Not Found', 405: 'Method Not Allowed',
                 416: 'Range Not Satisfiable', 500: 'Internal Server Error'}
        lines = ['HTTP/1.1 %d %s' % (status, names.get(status, 'Error')), 'Cache-Control: no-store', 'Accept-Ranges: bytes']
        lines += ['%s: %s' % pair for pair in headers]
        writer.write(('\r\n'.join(lines) + '\r\n\r\n').encode('latin-1') + body)

    async def request(self, reader, writer):
        """Answer one request; True if the connection may carry another."""
        line = await asyncio.wait_for(reader.readline(), 120)
        if not line:
            return False
        headers = {}
        for _ in range(64):
            header = await asyncio.wait_for(reader.readline(), 30)
            if header in (b'\r\n', b'\n', b''):
                break
            name, _, value = header.decode('latin-1').partition(':')
            headers[name.strip().lower()] = value.strip()
        parts = line.decode('latin-1').split()
        if len(parts) != 3:
            self.reply(writer, 400, [('Content-Length', '0'), ('Connection', 'close')])
            return False
        method, target, _version = parts
        pieces = target.split('?')[0].split('/')
        transfer = None
        if len(pieces) == 3 and hmac.compare_digest(pieces[1].encode(), self.token.encode()):
            transfer = self.transfers.get(pieces[2])
        if transfer is None or transfer.closed:
            self.reply(writer, 404, [('Content-Length', '0')])
            return True
        if method not in ('GET', 'HEAD'):
            self.reply(writer, 405, [('Allow', 'GET, HEAD'), ('Content-Length', '0')])
            return True
        kind = transfer.mime if '/' in (transfer.mime or '') else (mimetypes.guess_type(pieces[2])[0] or 'application/octet-stream')
        try:
            asked = parse_range(headers.get('range'), transfer.size)
        except ValueError:
            self.reply(writer, 416, [('Content-Range', 'bytes */%d' % transfer.size), ('Content-Length', '0')])
            return True
        first, last = asked if asked else (0, transfer.size - 1)
        head = [('Content-Type', kind), ('Content-Length', str(last - first + 1))]
        if asked:
            head.append(('Content-Range', 'bytes %d-%d/%d' % (first, last, transfer.size)))
        self.reply(writer, 206 if asked else 200, head)
        if method == 'GET':
            position = first
            try:
                while position <= last:
                    data = await transfer.read(position, last - position + 1)
                    writer.write(data)
                    await writer.drain()
                    position += len(data)
            except asyncio.CancelledError:
                if transfer.closed:
                    return False
                raise
            except Rejected:
                return False                     # the headers promised more than can be sent: end the connection
        else:
            await writer.drain()
        return headers.get('connection', '').lower() != 'close'


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
        self.transfers = {}
        self.transfer_futures = {}
        self.streams = StreamServer()

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

    async def message_for(self, chat, message_id):
        """The message a song lives in, found again from the ids the library kept."""
        try:
            peer = await self.client.get_input_entity(int(chat))
        except ValueError:
            # The session has not seen this chat since it started; reading the
            # chat list teaches it the access hash.
            async for _ in self.client.iter_dialogs(limit=400):
                pass
            peer = await self.client.get_input_entity(int(chat))
        message = await self.guarded(lambda: self.client.get_messages(peer, ids=int(message_id)))
        if isinstance(message, list):
            message = message[0] if message else None
        return message

    async def do_fetch(self, request):
        """Download one song whole into `directory`, under the name `name`.

        The file is written beside its final name and moved into place only when
        complete, so a cancelled or failed download never leaves something the
        player could mistake for a song. With "stream" the song comes down in
        blocks that `stream` can serve while they arrive.
        """
        job = request.get('job')
        name, directory = str(request.get('name') or ''), Path(str(request.get('directory') or ''))
        if not re.fullmatch(r'[A-Za-z0-9_]{1,64}', name) or not directory.is_dir():
            raise Rejected('Nowhere to keep the song.')
        future = self.transfer_futures.setdefault(job, asyncio.get_running_loop().create_future())
        try:
            message = await self.message_for(request['chatId'], request['messageId'])
            song = song_from(message) if message is not None else None
            if song is None:
                raise Rejected('This song is no longer in the chat.')
            limit = int(request.get('limit') or MAX_FETCH)
            if song['size'] > limit:
                raise Rejected('This file is %d MB; Musix plays Telegram songs up to %d MB.' % (song['size'] // 1048576, limit // 1048576))
            target = directory / (name + audio_extension(song['fileName'], song['mime']))
            part = directory / (name + '.part')
            document = message.media.document

            def progress(received, total):
                self.emit({'event': 'progress', 'job': job, 'received': int(received), 'total': int(total or song['size'])})

            if request.get('stream') and song['size'] > 0:
                transfer = Transfer(self, job, name, document, song['size'], song['mime'], part, target)
                self.transfers[job] = transfer
                if not future.done():
                    future.set_result(transfer)
                try:
                    await transfer.run()
                finally:
                    self.transfers.pop(job, None)
                    if not transfer.complete:
                        self.streams.forget(transfer)
            else:
                if not future.done():
                    future.set_exception(Rejected('This song can’t be streamed.'))
                    future.exception()

                async def download():
                    with open(part, 'wb') as out:
                        await self.client.download_file(document, out, file_size=song['size'] or None, progress_callback=progress)
                try:
                    await self.guarded(download, job)
                    if part.stat().st_size == 0 or (song['size'] and part.stat().st_size != song['size']):
                        raise Rejected('Telegram sent an incomplete file. Try again.')
                    os.replace(part, target)
                finally:
                    part.unlink(missing_ok=True)
        except BaseException as error:
            if not future.done():
                future.set_exception(error if isinstance(error, Exception) else Rejected('Cancelled.'))
                future.exception()                # nobody may be waiting for it; that is not an error
            raise
        finally:
            self.transfer_futures.pop(job, None)
        result = {'file': str(target), 'size': target.stat().st_size, 'tags': read_tags(target)}
        art = str(request.get('artDirectory') or '')
        if art and re.fullmatch(r'[A-Za-z0-9_]{1,64}', str(request.get('artName') or '')) and Path(art).is_dir():
            cover = embedded_cover(target)
            if cover:
                found = Path(art) / (request['artName'] + cover[1])
                self.write_file(found, cover[0])
                result['art'] = str(found)
        return result

    async def do_stream(self, request):
        """The URL the player can open for a fetch that is running with "stream"."""
        job = request.get('fetchJob')
        future = self.transfer_futures.get(job)
        if future is None:
            raise Rejected('That song is not being downloaded.')
        try:
            transfer = await asyncio.wait_for(asyncio.shield(future), 60)
        except asyncio.TimeoutError:
            raise Rejected('Telegram took too long to find the song.')
        return {'url': await self.streams.url(transfer), 'size': transfer.size}

    @staticmethod
    def write_file(target, data):
        part = target.with_name(target.name + '.part')
        try:
            part.write_bytes(data)
            os.replace(part, target)
        finally:
            part.unlink(missing_ok=True)

    async def header_cover(self, document, size, directory, name, job):
        """The picture in the front of a file, found without downloading the song.

        Reads as much of the start as its tags take (at most MAX_HEADER), writes
        it beside the pictures under a throwaway name and lets mutagen look.
        """
        head = b''
        stream = self.client.iter_download(document, offset=0, request_size=HEADER_CHUNK, limit=MAX_HEADER // HEADER_CHUNK, file_size=size or None)

        async def read():
            nonlocal head
            try:
                async for chunk in stream:
                    head += bytes(chunk)
                    needed = header_needed(head)
                    if needed is None or needed <= len(head) or needed > MAX_HEADER or len(head) >= MAX_HEADER:
                        break
            finally:
                close = getattr(stream, 'aclose', None)
                if close is not None:
                    await close()
        await self.guarded(read, job)
        needed = header_needed(head)
        # A tag longer than what is read would be cut off mid-picture.
        if not head or needed == 0 or (needed is not None and needed > len(head)):
            return None
        scratch = directory / (name + '.head')
        try:
            scratch.write_bytes(head)
            found = embedded_cover(scratch)
        finally:
            scratch.unlink(missing_ok=True)
        return found[0] if found else None

    async def do_art(self, request):
        """One picture: a song's thumbnail, or a chat's photo (Saved Messages gets a drawn one).

        `file` in the answer is empty when Telegram has none; that is not an error.
        """
        directory, name = Path(str(request.get('directory') or '')), str(request.get('name') or '')
        if not re.fullmatch(r'[A-Za-z0-9_]{1,64}', name) or not directory.is_dir():
            raise Rejected('Nowhere to keep the picture.')
        kind = request.get('kind')
        if kind == 'thumb':
            message = await self.message_for(request['chatId'], request['messageId'])
            data = None
            song = song_from(message) if message is not None else None
            if song is not None:
                if song['thumb']:
                    data = await self.guarded(lambda: self.client.download_media(message, file=bytes, thumb=-1), request.get('job'))
                # Most audio files carry no thumbnail, only a cover in their tags.
                if not (isinstance(data, (bytes, bytearray)) and image_type(data)):
                    data = await self.header_cover(message.media.document, song['size'], directory, name, request.get('job'))
        elif kind == 'photo':
            me = await self.guarded(self.client.get_me)
            if int(request['chatId']) == me.id:
                data = saved_messages_cover()
            else:
                try:
                    entity = await self.client.get_input_entity(int(request['chatId']))
                except ValueError:
                    async for _ in self.client.iter_dialogs(limit=400):
                        pass
                    entity = await self.client.get_input_entity(int(request['chatId']))
                data = await self.guarded(lambda: self.client.download_profile_photo(entity, file=bytes, download_big=False), request.get('job'))
        else:
            raise Rejected('Unknown picture.')
        extension = image_type(data) if isinstance(data, (bytes, bytearray)) else ''
        if not extension or len(data) > MAX_ART:
            return {'file': ''}
        target = directory / (name + extension)
        for stale in directory.glob(name + '.*'):
            if stale != target and stale.suffix != '.part':
                stale.unlink(missing_ok=True)
        self.write_file(target, bytes(data))
        return {'file': str(target)}

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
        await self.streams.stop()
        self.streams = StreamServer()
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
        await self.streams.stop()
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
    # Only the work itself is registered under its job number: the request
    # that cancels it names the same number and must not take its place.
    job = request.get('job') if request.get('cmd') in DATA_COMMANDS else None
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
