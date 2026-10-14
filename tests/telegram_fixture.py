#!/usr/bin/env python3
"""Stands in for helper/telegram.py in the C++ tests: the same lines on stdin and
stdout, a scripted account behind them, and no Telegram. FIXTURE_MODE picks a
misbehaviour: flood (a sync ends with a long FLOOD_WAIT after its first page), slow (a sync takes
its time, so it can be cancelled), crash (dies once after the first state, then behaves), silent
(never says anything), exit2 (says why and leaves, asking for no restart)."""
import json, os, sys, threading, time
from pathlib import Path

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

def sync(request, replies):
    job, chat, topic = request['job'], int(request['chatId']), int(request.get('topicId') or 0)
    total = COUNTS.get((chat, topic), 0)
    sent = 0
    while sent < total:
        n = min(100, total - sent)
        emit(event='songs', job=job, songs=[dict(messageId=1000 + sent + i, title='Song %d' % (sent + i), artist='Artist',
                                                  seconds=180, mime='audio/mpeg', size=4000000, date=1700000000 + sent + i,
                                                  fileName='s.mp3') for i in range(n)])
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
    elif cmd == 'sync':
        worker = threading.Thread(target=sync, args=(request, replies), daemon=True)
        workers.append(worker); worker.start()
        continue
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
