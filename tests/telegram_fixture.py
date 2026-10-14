#!/usr/bin/env python3
"""Stands in for helper/telegram.py in the C++ tests: the same lines on stdin and
stdout, a scripted account behind them, and no Telegram. FIXTURE_MODE picks a
misbehaviour: crash (dies once after the first state, then behaves), silent
(never says anything), exit2 (says why and leaves, asking for no restart)."""
import json, os, sys
from pathlib import Path

directory = Path(os.environ['MUSIX_TELEGRAM_DIR'])
mode = os.environ.get('FIXTURE_MODE', '')
(directory / 'seen-env.json').write_text(json.dumps({k: os.environ.get(k, '') for k in
    ('MUSIX_TELEGRAM_API_ID', 'MUSIX_TELEGRAM_API_HASH', 'MUSIX_VERSION')}))

def emit(**event):
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

phone = None
state('ready', name='Ada Lovelace', username='ada') if (directory / 'account.json').exists() else state('wait-phone')
for line in sys.stdin:
    request = json.loads(line)
    reply = {'id': request['id'], 'ok': True}
    cmd = request['cmd']
    if cmd == 'phone':
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
            state('ready', name='Ada Lovelace', username='ada')
    elif cmd == 'password':
        if request['password'] != 'secret':
            reply.update(ok=False, error='That password isn’t right.')
        else:
            (directory / 'account.json').write_text(json.dumps({'name': 'Ada Lovelace', 'username': 'ada'}))
            state('ready', name='Ada Lovelace', username='ada')
    elif cmd == 'back':
        state('wait-phone')
    elif cmd == 'logout':
        (directory / 'account.json').unlink(missing_ok=True); state('wait-phone')
    sys.stdout.write(json.dumps(reply) + '\n'); sys.stdout.flush()
