"""New releases from followed artists and channels: what counts as new, in what order, and what never reaches the network."""
import importlib.util
import pathlib
import unittest

spec = importlib.util.spec_from_file_location('catalog', pathlib.Path(__file__).parents[1]/'helper/catalog.py')
catalog = importlib.util.module_from_spec(spec)
spec.loader.exec_module(catalog)


def item(rid, year=''):
    return {'id': rid, 'title': rid, 'year': year}


class DetectReleases(unittest.TestCase):
    def test_following_records_the_catalogue_without_reporting_it(self):
        catalogue = [item('a', '2026'), item('b', '2025'), item('c', '2019')]
        fresh, seen = catalog.detect_releases(catalogue, [], True, 2026)
        self.assertEqual(fresh, [])
        self.assertEqual(seen, ['a', 'b', 'c'])

    def test_only_what_was_not_seen_is_new_newest_first(self):
        _, seen = catalog.detect_releases([item('a', '2025'), item('b', '2024')], [], True, 2026)
        now = [item('n2', '2026'), item('n1', '2026'), item('a', '2025'), item('b', '2024')]
        fresh, seen = catalog.detect_releases(now, seen, False, 2026)
        self.assertEqual([r['id'] for r in fresh], ['n2', 'n1'])
        # Seen once, never again.
        fresh, _ = catalog.detect_releases(now, seen, False, 2026)
        self.assertEqual(fresh, [])

    def test_an_old_release_turning_up_is_not_news(self):
        _, seen = catalog.detect_releases([item('a', '2026')], [], True, 2026)
        fresh, seen = catalog.detect_releases([item('a', '2026'), item('reissue', '2011'), item('last-year', '2025')], seen, False, 2026)
        self.assertEqual([r['id'] for r in fresh], ['last-year'])
        self.assertIn('reissue', seen)

    def test_undated_uploads_count_when_unseen(self):
        _, seen = catalog.detect_releases([item('v1'), item('v2')], [], True, 2026)
        fresh, _ = catalog.detect_releases([item('v3'), item('v1'), item('v2')], seen, False, 2026)
        self.assertEqual([r['id'] for r in fresh], ['v3'])

    def test_known_is_bounded_and_tolerates_junk(self):
        many = [item('x%d' % i) for i in range(700)]
        fresh, seen = catalog.detect_releases(many, ['', None, 'x1', 'x1'], True, 2026)
        self.assertEqual(fresh, [])
        self.assertEqual(len(seen), 600)
        self.assertEqual(seen[-1], 'x699')


class FakeApi:
    def __init__(self, artist=None, fail=False):
        self.artist, self.fail = artist, fail

    def get_artist(self, browse):
        if self.fail:
            raise KeyError('musicImmersiveHeaderRenderer')
        return self.artist


class Candidates(unittest.TestCase):
    def test_artist_releases_are_singles_and_albums_by_year(self):
        api = FakeApi({'name': 'Someone', 'thumbnails': [],
                       'albums': {'results': [{'title': 'Old', 'browseId': 'MPREb_old', 'year': '2015'},
                                              {'title': 'Newer', 'browseId': 'MPREb_new', 'year': '2025'}]},
                       'singles': {'results': [{'title': 'Newest', 'browseId': 'MPREb_single', 'year': '2026', 'type': 'Single'}]},
                       'songs': {'results': [{'title': 'Popular', 'videoId': 'aaaaaaaaaaa'}]}})
        kind, name, _, found = catalog.release_candidates(api, 'UC' + 'a' * 22, 'artist')
        self.assertEqual((kind, name), ('artist', 'Someone'))
        self.assertEqual([r['id'] for r in found], ['MPREb_single', 'MPREb_new', 'MPREb_old'])
        self.assertEqual(found[0]['releaseType'], 'Single')
        self.assertEqual(found[1]['releaseType'], 'Album')
        self.assertTrue(all(r['artistId'] == 'UC' + 'a' * 22 for r in found))

    def test_a_non_artist_id_that_is_not_a_channel_is_an_error(self):
        with self.assertRaises(KeyError):
            catalog.release_candidates(FakeApi(fail=True), 'not-a-channel', 'artist')

    def test_channel_ids_are_checked_before_anything_is_fetched(self):
        for bad in ('', 'UC', 'UC' + 'a' * 21, 'https://evil.example/', '../../x', 'UC' + 'a' * 22 + '/x'):
            with self.assertRaises(ValueError, msg=bad):
                catalog.channel_uploads(bad)


if __name__ == '__main__':
    unittest.main()
