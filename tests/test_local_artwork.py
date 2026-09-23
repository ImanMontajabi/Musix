"""Real local sidecars, generated in temporary directories; no network or media assets."""
import importlib.util
import pathlib
import subprocess
import tempfile
import unittest
import wave
from urllib.parse import unquote, urlparse

spec = importlib.util.spec_from_file_location('catalog', pathlib.Path(__file__).parents[1]/'helper/catalog.py')
catalog = importlib.util.module_from_spec(spec)
spec.loader.exec_module(catalog)

# The app canonicalises every path before the helper sees it -- QFileInfo::canonicalFilePath
# on the C++ side, Path.resolve() in the helper -- so the tests hand it canonical paths too.
# On macOS the temp directory sits behind the /var -> /private/var symlink.
class LocalArtworkTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = pathlib.Path(self.temp.name).resolve()
        self.song = self.root/'Été #100%.wav'
        with wave.open(str(self.song), 'wb') as output:
            output.setparams((1, 2, 8000, 8000, 'NONE', 'not compressed'))
            output.writeframes(b'\0'*16000)
        self.request = dict(files=[str(self.song)], artDirectory=str(self.root/'cache'))

    def cover(self, name):
        path = self.root/name
        args = ['ffmpeg','-nostdin','-v','error','-f','lavfi','-i','testsrc2=size=128x128:rate=10:duration=0.5','-threads','1']
        if path.suffix.lower() in ('.jpg', '.png'): args += ['-frames:v','1']
        subprocess.run(args+['-y',str(path)],check=True,capture_output=True,timeout=10)
        return path

    def test_formats_posters_and_shared_cache(self):
        # The fixtures are made with whatever ffmpeg is installed, and not every
        # build can write every format -- Homebrew's has no WebP encoder. That
        # says nothing about Musix, so the case is skipped by name, not failed.
        encoders = subprocess.run(['ffmpeg','-hide_banner','-encoders'],capture_output=True,text=True).stdout
        for extension in ('gif','webp','mp4','webm','jpg','png'):
            with self.subTest(extension=extension):
                if extension == 'webp' and 'libwebp' not in encoders and ' webp ' not in encoders:
                    self.skipTest('this ffmpeg cannot write WebP, so the fixture cannot be made')
                cover = self.cover('CoVeR.'+extension)
                item = catalog.local_files(self.request)['items'][0]
                self.assertTrue(item['art'])
                poster = pathlib.Path(unquote(urlparse(item['art']).path))
                self.assertLessEqual(poster.stat().st_size,262144)
                self.assertEqual(bool(item['motionArt']),extension in ('gif','webp','mp4','webm'))
                if item['motionArt']: self.assertEqual(pathlib.Path(unquote(urlparse(item['motionArt']).path)),cover)
                modified = poster.stat().st_mtime_ns
                self.assertEqual(catalog.local_files(self.request)['items'][0],item)
                self.assertEqual(poster.stat().st_mtime_ns,modified)
                cover.unlink()

    def test_rescan_added_changed_removed_and_track_precedence(self):
        original = catalog.local_files(self.request)['items'][0]
        def scan(item): return catalog.scan_music_folders(dict(folders=[str(self.root)],known={str(self.song):item['localStamp']}))['files']
        self.assertEqual(scan(original),[])
        shared = self.cover('cover.gif')
        self.assertEqual(scan(original),[str(self.song)])
        first = catalog.local_files(self.request)['items'][0]
        self.assertEqual(scan(first),[])
        specific = self.cover(self.song.stem+'.mp4')
        self.assertEqual(scan(first),[str(self.song)])
        second = catalog.local_files(self.request)['items'][0]
        self.assertIn('.mp4',second['motionArt'])
        import os
        os.utime(specific, ns=(specific.stat().st_atime_ns,specific.stat().st_mtime_ns+1000000))
        self.assertEqual(scan(second),[str(self.song)])
        specific.unlink();shared.unlink()
        self.assertEqual(scan(second),[str(self.song)])
        last = catalog.local_files(self.request)['items'][0]
        self.assertFalse(last['motionArt']);self.assertFalse(last['art']);self.assertEqual(scan(last),[])

    def test_bad_or_oversized_cover_keeps_song_playable(self):
        cover = self.root/'cover.mp4';cover.write_bytes(b'not a video')
        result = catalog.local_files(self.request)
        self.assertFalse(result['failed']);self.assertEqual(len(result['items']),1)
        self.assertFalse(result['items'][0]['motionArt'])
        with cover.open('wb') as output: output.truncate(128*1024*1024+1)
        result = catalog.local_files(self.request)
        self.assertFalse(result['failed']);self.assertFalse(result['items'][0]['motionArt'])

if __name__ == '__main__': unittest.main()
