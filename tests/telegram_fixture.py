#!/usr/bin/env python3
"""Stands in for helper/telegram.py in the C++ tests: the same lines on stdin and
stdout, a scripted account behind them, and no Telegram. FIXTURE_MODE picks a
misbehaviour: fetchslow (a download takes its time, so it can be cancelled), flood (a sync ends with a long FLOOD_WAIT after its first page), slow (a sync takes
its time, so it can be cancelled), crash (dies once after the first state, then behaves), silent
(never says anything), exit2 (says why and leaves, asking for no restart).

Message 1004 is streamed: with "stream": true it is brought down in three
blocks, 0.7 s apart, by the real helper's Transfer and served by its real
StreamServer, so the player has something to open before the download is over."""
import json, os, sys, threading, time
from pathlib import Path

os.umask(0o077)  # like the real helper: what it writes is the owner's alone

directory = Path(os.environ['MUSIX_TELEGRAM_DIR'])
mode = os.environ.get('FIXTURE_MODE', '')
(directory / 'seen-env.json').write_text(json.dumps({k: os.environ.get(k, '') for k in
    ('MUSIX_TELEGRAM_API_ID', 'MUSIX_TELEGRAM_API_HASH', 'MUSIX_VERSION')}))

lock = threading.Lock()

def emit(**event):
    with lock:
        sys.stdout.write(json.dumps(event) + '\n'); sys.stdout.flush()

def state(step, **extra): emit(event='state', state=step, **extra)

if mode == 'silent':
    sys.stdin.read(); sys.exit(0)
if mode == 'exit2':
    state('offline', error='This build has no Telegram API credentials.'); sys.exit(2)
if mode == 'crash':
    flag = directory / 'crashed'
    if not flag.exists():
        flag.write_text('1'); state('wait-phone'); sys.exit(1)

DIALOGS = [dict(chatId=4242, title='Saved Messages', kind='saved', username=''),
           dict(chatId=-1001, title='News', kind='channel', username='news'),
           dict(chatId=-1002, title='Friends', kind='group', username=''),
           dict(chatId=-1003, title='Forum', kind='forum', username='')]
TOPICS = {-1003: [dict(topicId=11, title='Jazz'), dict(topicId=12, title='Rock')]}
COUNTS = {(4242, 0): 3, (-1001, 0): 250, (-1002, 0): 4, (-1003, 0): 3, (-1003, 11): 2, (-1003, 12): 1, (-100777, 0): 5, (-1004, 0): 5}

def png(r, g, b):
    import struct, zlib
    def chunk(kind, body):
        return struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body) & 0xffffffff)
    row = b'\x00' + bytes((r, g, b)) * 8
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 8, 8, 8, 2, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(row * 8)) + chunk(b'IEND', b''))

def art(request):
    """A thumbnail for message 1000 and 1002 of any chat (1001 has none); a photo for every chat but -1002."""
    out, name = Path(request['directory']), request['name']
    with open(directory / 'art.log', 'a') as log:
        log.write('%s %s %s\n' % (request['kind'], request['chatId'], request.get('messageId', '')))
    if request['kind'] == 'thumb' and int(request['messageId']) not in (1000, 1002):
        return dict(file='')
    if request['kind'] == 'photo' and int(request['chatId']) == -1002:
        return dict(file='')
    target = out / (name + '.png')
    target.write_bytes(png(200, 40, 40) if request['kind'] == 'photo' else png(40, 40, 200))
    return dict(file=str(target))

# A channel that signs its songs: handles in titles and performers, links, a file name for a title.
PROMO = [('The Weeknd - Blinding Lights @MusicHub', 'Telegram Audio', 'a.mp3'),
         ('Song | @MusicHub', '@MusicHub', 'b.mp3'),
         ('Plain_File @MusicHub', '', 'Plain_File @MusicHub.mp3'),
         ('Calvin Harris - Feel So Close (TMC & BLH Remix) [t.me/music_hub]', '', 'c.mp3'),
         ('Untouched (Live)', 'Real Artist', 'd.mp3')]

def song_row(chat, index):
    if chat == -1004:
        title, artist, name = PROMO[index]
        return dict(messageId=1000 + index, title=title, artist=artist, seconds=200, mime='audio/mpeg', size=4000000, date=1700000000 + index, fileName=name, thumb=False)
    """Saved Messages' third song is a plain document: no duration, no artist, its file name for a title."""
    if chat == 4242 and index == 2:
        return dict(messageId=1002, title='Plain', artist='', seconds=0, mime='audio/x-wav', size=16044, date=1700000002, fileName='Plain.wav', thumb=True)
    return dict(messageId=1000 + index, title='Song %d' % index, artist='Artist', seconds=180, mime='audio/mpeg',
                size=4000000, date=1700000000 + index, fileName='s.mp3', thumb=chat in (4242, -100777))

def sync(request, replies):
    """Every song of a chat, 100 to a page. `minId` keeps it to newer messages, as Telegram's search does; a file
    `more` in the session folder adds that many newer songs to every chat, to refresh against."""
    job, chat, topic = request['job'], int(request['chatId']), int(request.get('topicId') or 0)
    min_id = int(request.get('minId') or 0)
    with open(directory / 'syncs.log', 'a') as log:
        log.write('%d %d\n' % (chat, min_id))
    base = COUNTS.get((chat, topic), 0)
    extra = int((directory / 'more').read_text()) if base and (directory / 'more').exists() else 0
    everything = [song_row(chat, i) for i in range(base + extra)]
    newest = max([row['messageId'] for row in everything], default=0)
    rows = [row for row in everything if row['messageId'] > min_id]
    total = len(rows)
    sent = 0
    while sent < total:
        n = min(100, total - sent)
        emit(event='songs', job=job, songs=rows[sent:sent + n])
        sent += n
        emit(event='progress', job=job, scanned=sent)
        if mode == 'flood':
            replies.put({'id': request['id'], 'ok': False, 'error': 'Telegram asks to wait 2 hours before more requests. Try again then.'})
            return
        if mode == 'slow':
            for _ in range(200):
                if job in cancelled:
                    replies.put({'id': request['id'], 'ok': False, 'error': 'Cancelled.'}); return
                time.sleep(0.05)
        if sent == 100 and total > 200:
            emit(event='wait', job=job, seconds=3); time.sleep(0.2); emit(event='wait', job=job, seconds=0)
    replies.put({'id': request['id'], 'ok': True, 'result': dict(count=total, complete=True, maxId=newest if total else 0)})

# Long enough, when the UI suite asks, for a crossfade to fit in a song.
WAV_SECONDS = int(os.environ.get('FIXTURE_WAV_SECONDS') or 1)

def wav_bytes(seconds=WAV_SECONDS):
    import io, wave
    buffer = io.BytesIO()
    w = wave.open(buffer, 'wb'); w.setnchannels(1); w.setsampwidth(2); w.setframerate(8000); w.writeframes(b'\0\0' * 8000 * seconds); w.close()
    return buffer.getvalue()

def fetch(request, replies):
    """A download of one song: message 9999 is gone, 8888 is over the cap, 1001 takes three seconds, any other is a one-second WAV."""
    job, message = request['job'], int(request['messageId'])
    with open(directory / 'fetches.log', 'a') as log:
        log.write('%d\n' % message)
    reply = {'id': request['id']}
    if message == 9999:
        replies.put(dict(reply, ok=False, error='This song is no longer in the chat.')); return
    if message == 8888:
        replies.put(dict(reply, ok=False, error='This file is 300 MB; Musix plays Telegram songs up to 200 MB.')); return
    out = Path(request['directory'])
    part, target = out / (request['name'] + '.part'), out / (request['name'] + '.wav')
    data = wav_bytes()
    part.write_bytes(data[:len(data) // 2])
    emit(event='progress', job=job, received=len(data) // 2, total=len(data))
    for _ in range(60 if mode == 'fetchslow' or message == 1001 else 4):
        if job in cancelled:
            part.unlink(missing_ok=True)
            replies.put(dict(reply, ok=False, error='Cancelled.')); return
        time.sleep(0.05)
    part.write_bytes(data)
    emit(event='progress', job=job, received=len(data), total=len(data))
    part.replace(target)
    result = dict(file=str(target), size=len(data), tags=dict(seconds=WAV_SECONDS, title='Tagged %d | @MusicHub' % message, artist='Tag Artist (@MusicHub)'))
    if request.get('artDirectory') and message == 1003:   # only message 1003 has a picture inside
        inside = Path(request['artDirectory']) / (request['artName'] + '.png')
        inside.write_bytes(png(40, 200, 40))
        result['art'] = str(inside)
    replies.put(dict(reply, ok=True, result=result))

# --- Message 1004: the real helper's blocks and loopback server, a slow Telegram behind them ---
import asyncio, importlib.util
STREAMED = 1004
_streaming = {}

def _helper():
    if 'module' not in _streaming:
        source = Path(__file__).resolve().parents[1] / 'helper' / 'telegram.py'
        spec = importlib.util.spec_from_file_location('real_helper', source)
        module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
        loop = asyncio.new_event_loop()
        threading.Thread(target=loop.run_forever, daemon=True).start()

        class Document:
            def __init__(self, payload): self.payload, self.size = payload, len(payload)

        class Client:
            async def iter_download(self, document, offset=0, request_size=None, limit=None, file_size=None):
                await asyncio.sleep(float(os.environ.get('FIXTURE_BLOCK_DELAY', '0.7')))
                if (directory / 'die').exists():             # a test asks for the helper to be lost once, mid-stream
                    (directory / 'die').unlink()
                    os._exit(1)
                yield document.payload[offset:offset + request_size]

        class Account:
            client = Client()
            def emit(self, event): emit(**event)
            async def guarded(self, call, job=None): return await call()

        _streaming.update(module=module, loop=loop, document=Document, account=Account(), transfers={}, tasks={}, server=asyncio.run_coroutine_threadsafe(_make_server(module), loop).result(10))
    return _streaming

async def _make_server(module): return module.StreamServer()

def stream_fetch(request, replies):
    h = _helper()
    job, reply = request['job'], {'id': request['id']}
    out = Path(request['directory'])
    data = wav_bytes(96)                                    # three blocks of silence
    part, target = out / (request['name'] + '.part'), out / (request['name'] + '.wav')
    transfer = h['module'].Transfer(h['account'], job, request['name'], h['document'](data), len(data), 'audio/x-wav', part, target)
    h['transfers'][job] = transfer

    async def run():
        try:
            await transfer.run()
        except asyncio.CancelledError:
            replies.put(dict(reply, ok=False, error='Cancelled.')); return
        except Exception as error:
            replies.put(dict(reply, ok=False, error=str(error))); return
        finally:
            h['transfers'].pop(job, None)
        replies.put(dict(reply, ok=True, result=dict(file=str(target), size=len(data), tags=dict(seconds=96, title='Streamed', artist='Tag Artist'))))
    h['tasks'][job] = asyncio.run_coroutine_threadsafe(run(), h['loop'])

def stream_url(request, replies):
    reply, job = {'id': request['id']}, request['fetchJob']
    if 'module' not in _streaming:
        replies.put(dict(reply, ok=False, error='This song can’t be streamed.')); return
    h = _streaming
    for _ in range(300):
        if job in h['transfers']: break
        time.sleep(0.05)
    else:
        replies.put(dict(reply, ok=False, error='That song is not being downloaded.')); return
    transfer = h['transfers'][job]
    url = asyncio.run_coroutine_threadsafe(h['server'].url(transfer), h['loop']).result(10)
    replies.put(dict(reply, ok=True, result=dict(url=url, size=transfer.size)))

import queue
cancelled = set()
replies = queue.Queue()

def pump():
    while True:
        reply = replies.get()
        with lock:
            sys.stdout.write(json.dumps(reply) + '\n'); sys.stdout.flush()
threading.Thread(target=pump, daemon=True).start()

workers = []
phone = None
state('ready', name='Ada Lovelace', username='ada', userId=4242) if (directory / 'account.json').exists() else state('wait-phone')
for line in sys.stdin:
    request = json.loads(line)
    reply = {'id': request['id'], 'ok': True}
    cmd = request['cmd']
    if cmd == 'dialogs':
        reply['result'] = dict(dialogs=DIALOGS)
    elif cmd == 'topics':
        reply['result'] = dict(topics=TOPICS.get(int(request['chatId']), []))
    elif cmd == 'resolve':
        if request['query'].lstrip('@').endswith('public_music'):
            reply['result'] = dict(chat=dict(chatId=-100777, title='Public Music', kind='channel', username='public_music'))
        else:
            reply.update(ok=False, error='There is no public channel called %s.' % request['query'])
    elif cmd == 'fetch' and request.get('stream') and int(request['messageId']) == STREAMED:
        with open(directory / 'fetches.log', 'a') as log:
            log.write('%d\n' % STREAMED)
        stream_fetch(request, replies)
        continue
    elif cmd == 'stream':
        worker = threading.Thread(target=stream_url, args=(request, replies), daemon=True)
        workers.append(worker); worker.start()
        continue
    elif cmd in ('sync', 'fetch'):
        worker = threading.Thread(target={'sync': sync, 'fetch': fetch}[cmd], args=(request, replies), daemon=True)
        workers.append(worker); worker.start()
        continue
    elif cmd == 'art':
        reply['result'] = art(request)
    elif cmd == 'cancel':
        cancelled.add(request['job'])
        if request['job'] in _streaming.get('tasks', {}):
            _streaming['tasks'][request['job']].cancel()
    elif cmd == 'phone':
        if not request['phone'].startswith('+49'):
            reply.update(ok=False, error='That phone number isn’t valid.')
        else:
            phone = request['phone']; state('wait-code', codeKind='app')
    elif cmd == 'code':
        if request['code'] != '12345':
            reply.update(ok=False, error='That code isn’t right.')
        elif phone.endswith('99'):
            state('wait-password', passwordHint='first pet')
        else:
            (directory / 'account.json').write_text(json.dumps({'name': 'Ada Lovelace', 'username': 'ada'}))
            state('ready', name='Ada Lovelace', username='ada', userId=4242)
    elif cmd == 'password':
        if request['password'] != 'secret':
            reply.update(ok=False, error='That password isn’t right.')
        else:
            (directory / 'account.json').write_text(json.dumps({'name': 'Ada Lovelace', 'username': 'ada'}))
            state('ready', name='Ada Lovelace', username='ada', userId=4242)
    elif cmd == 'back':
        state('wait-phone')
    elif cmd == 'logout':
        (directory / 'account.json').unlink(missing_ok=True); state('wait-phone')
    sys.stdout.write(json.dumps(reply) + '\n'); sys.stdout.flush()
# Input ended: the app wants the helper gone. Let a running sync say what it has, then leave.
for worker in workers:
    worker.join(timeout=2)
time.sleep(0.1)
os._exit(0)
