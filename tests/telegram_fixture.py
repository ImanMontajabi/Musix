#!/usr/bin/env python3
"""Stands in for helper/telegram.py in the C++ tests: the same lines on stdin and
stdout, a scripted account behind them, and no Telegram. FIXTURE_MODE picks a
misbehaviour: fetchslow (a download takes its time, so it can be cancelled), flood (a sync ends with a long FLOOD_WAIT after its first page), slow (a sync takes
its time, so it can be cancelled), crash (dies once after the first state, then behaves), silent
(never says anything), exit2 (says why and leaves, asking for no restart)."""
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
COUNTS = {(4242, 0): 3, (-1001, 0): 250, (-1002, 0): 4, (-1003, 0): 3, (-1003, 11): 2, (-1003, 12): 1, (-100777, 0): 5}

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

def song_row(chat, index):
    """Saved Messages' third song is a plain document: no duration, no artist, its file name for a title."""
    if chat == 4242 and index == 2:
        return dict(messageId=1002, title='Plain', artist='', seconds=0, mime='audio/x-wav', size=16044, date=1700000002, fileName='Plain.wav', thumb=True)
    return dict(messageId=1000 + index, title='Song %d' % index, artist='Artist', seconds=180, mime='audio/mpeg',
                size=4000000, date=1700000000 + index, fileName='s.mp3', thumb=chat in (4242, -100777))

def sync(request, replies):
    job, chat, topic = request['job'], int(request['chatId']), int(request.get('topicId') or 0)
    total = COUNTS.get((chat, topic), 0)
    sent = 0
    while sent < total:
        n = min(100, total - sent)
        emit(event='songs', job=job, songs=[song_row(chat, sent + i) for i in range(n)])
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
    replies.put({'id': request['id'], 'ok': True, 'result': dict(count=total, complete=True)})

# Long enough, when the UI suite asks, for a crossfade to fit in a song.
WAV_SECONDS = int(os.environ.get('FIXTURE_WAV_SECONDS') or 1)

def wav_bytes():
    import io, wave
    buffer = io.BytesIO()
    w = wave.open(buffer, 'wb'); w.setnchannels(1); w.setsampwidth(2); w.setframerate(8000); w.writeframes(b'\0\0' * 8000 * WAV_SECONDS); w.close()
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
    result = dict(file=str(target), size=len(data), tags=dict(seconds=WAV_SECONDS, title='Tagged %d' % message, artist='Tag Artist'))
    if request.get('artDirectory') and message == 1003:   # only message 1003 has a picture inside
        inside = Path(request['artDirectory']) / (request['artName'] + '.png')
        inside.write_bytes(png(40, 200, 40))
        result['art'] = str(inside)
    replies.put(dict(reply, ok=True, result=result))

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
    elif cmd in ('sync', 'fetch'):
        worker = threading.Thread(target={'sync': sync, 'fetch': fetch}[cmd], args=(request, replies), daemon=True)
        workers.append(worker); worker.start()
        continue
    elif cmd == 'art':
        reply['result'] = art(request)
    elif cmd == 'cancel':
        cancelled.add(request['job'])
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
