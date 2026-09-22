#!/usr/bin/env python3
"""Write the license notices for the third-party code compiled into Qt.

Qt records every piece of third-party code it carries in a qt_attribution.json
beside it, with the license and the file holding its text. This reads those
straight out of the pinned module source tarballs and writes one document with
a notice per component and the full text of every license they use.

What is left out is decided below, a line per rule with the reason, and only
for code that cannot be in what ships: other platforms, build tooling, tests,
examples, and parts of modules the bundle does not carry. The build checks each
of those premises against the finished bundle, so an exclusion stops being
valid the moment the thing it excludes appears. Anything that cannot be proven
absent stays in: an extra notice is harmless, a missing one is not.

Usage: qt-licenses.py <dir with the *-everywhere-src-*.tar.xz> <output file>
"""
import json
import re
import sys
import tarfile
from pathlib import Path

MODULES = ('qtbase', 'qtdeclarative', 'qtmultimedia', 'qtsvg', 'qtimageformats')

# (module, path prefix within the module) -> why it cannot be in the bundle.
EXCLUDE = {
    ('qtbase', 'cmake/'): 'build tooling',
    ('qtbase', 'src/3rdparty/D3D12MemoryAllocator'): 'Windows only',
    ('qtbase', 'src/gui/rhi'): 'the D3D12 mipmap generator, Windows only',
    ('qtbase', 'src/3rdparty/wintab'): 'Windows only',
    ('qtbase', 'src/3rdparty/android'): 'Android only',
    ('qtbase', 'src/3rdparty/gradle'): 'Android build tooling',
    ('qtbase', 'src/3rdparty/wasm'): 'WebAssembly only',
    ('qtbase', 'src/3rdparty/wayland'): 'Linux only',
    ('qtbase', 'src/3rdparty/xcb'): 'Linux only',
    ('qtbase', 'src/3rdparty/sqlite'): 'QtSql, which the bundle does not carry',
    ('qtbase', 'src/3rdparty/zlib'): 'the macOS build uses the system zlib (system-zlib)',
    ('qtbase', 'src/testlib'): 'QtTest, not shipped',
    ('qtbase', 'src/testinternal'): 'test tooling',
    ('qtdeclarative', 'examples/'): 'examples',
    ('qtdeclarative', 'src/quickcontrols/material'): 'the Material style, pruned from the bundle',
    ('qtimageformats', 'src/3rdparty/libtiff'): 'the TIFF plugin, pruned from the bundle',
    ('qtmultimedia', 'src/3rdparty/ffmpeg'): "Qt's FFmpeg media backend, pruned with its libraries",
    ('qtmultimedia', 'src/3rdparty/signalsmith-stretch'): "linked only by the FFmpeg media backend",
    ('qtmultimedia', 'src/3rdparty/eigen'): 'linked only by QtSpatialAudio, not shipped',
    ('qtmultimedia', 'src/3rdparty/pffft'): 'linked only by QtSpatialAudio, not shipped',
    ('qtmultimedia', 'src/3rdparty/resonance-audio'): 'linked only by QtSpatialAudio, not shipped',
}

OPERATORS = {'AND', 'OR', 'WITH'}


def excluded(module, directory):
    for (m, prefix), reason in EXCLUDE.items():
        if m == module and (directory + '/').startswith(prefix.rstrip('/') + '/'):
            return reason
    return None


def text_of(value):
    if isinstance(value, list):
        return '\n'.join(str(v) for v in value)
    return str(value or '').strip()


def main():
    source, output = Path(sys.argv[1]), Path(sys.argv[2])
    notices, licenses, skipped = [], {}, []
    for module in MODULES:
        tarball = next(source.glob(module + '-everywhere-src-*.tar.xz'))
        with tarfile.open(tarball) as tar:
            members = {m.name: m for m in tar.getmembers() if m.isfile()}
            root = next(iter(members)).split('/', 1)[0]

            def read(path):
                member = members.get(root + '/' + path)
                return tar.extractfile(member).read().decode('utf-8', 'replace') if member else None

            for name in sorted(n for n in members if n.endswith('/qt_attribution.json')):
                directory = name[len(root) + 1:-len('/qt_attribution.json')]
                entries = json.loads(read(directory + '/qt_attribution.json'), strict=False)
                for entry in entries if isinstance(entries, list) else [entries]:
                    why = excluded(module, directory)
                    if why:
                        skipped.append('%s: %s (%s)' % (module, entry.get('Name', directory), why))
                        continue
                    ident = entry.get('LicenseId', '')
                    files = entry.get('LicenseFiles') or ([entry['LicenseFile']] if entry.get('LicenseFile') else [])
                    body = []
                    for f in files:
                        text = read(directory + '/' + f)
                        if text:
                            body.append(text.strip())
                    for spdx in re.findall(r'[A-Za-z0-9.+-]+', ident):
                        if spdx not in OPERATORS and spdx not in licenses:
                            text = read('LICENSES/' + spdx + '.txt')
                            if text:
                                licenses[spdx] = text.strip()
                    notices.append((entry.get('Name', directory), entry.get('Version', ''), ident,
                                    text_of(entry.get('Copyright')), module, directory, body))

    out = ['Third-party code compiled into Qt %s' % '6.11.2',
           '=' * 40, '',
           'The Qt frameworks and plugins in this bundle contain the components below.',
           'Each notice is followed by the text shipped with that component; the full',
           'text of every license named is at the end.', '']
    for name, version, ident, copyright, module, directory, body in sorted(notices, key=lambda n: n[0].lower()):
        out += ['-' * 78, '%s%s' % (name, ' ' + version if version else ''),
                'License: %s' % ident, 'Source: %s/%s' % (module, directory)]
        if copyright:
            out += ['', copyright]
        for text in body:
            out += ['', text]
        out.append('')
    out += ['=' * 78, 'License texts', '=' * 78, '']
    for spdx in sorted(licenses):
        out += ['-' * 78, spdx, '-' * 78, '', licenses[spdx], '']
    output.write_text('\n'.join(out) + '\n')
    print('%d components, %d license texts, %d excluded' % (len(notices), len(licenses), len(skipped)))
    for s in skipped:
        print('  excluded ' + s, file=sys.stderr)


if __name__ == '__main__':
    main()
