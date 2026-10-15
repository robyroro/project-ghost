# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

import unittest

import compat_corpus
from compat_corpus import CorpusError, Site, Step

GOOD = """
[[site]]
url = "https://www.wikipedia.org/"
category = "docs"
flow = [
  { action = "type", selector = "#searchInput", text = "Chromium" },
  { action = "press", key = "Enter" },
  { action = "expect_text", selector = "#firstHeading", text = "Chromium" },
]

[[site]]
url = "https://www.bbc.com/"
category = "perftest"
reason = "a blocked tracker's fallback throws; nothing visible is lost"
"""


class ParseTest(unittest.TestCase):
    def test_reads_sites_flows_and_reasons(self):
        sites = compat_corpus.parse(GOOD)
        self.assertEqual(sites, [
            Site("https://www.wikipedia.org/", "docs", (
                Step("type", selector="#searchInput", text="Chromium"),
                Step("press", key="Enter"),
                Step("expect_text", selector="#firstHeading", text="Chromium"))),
            Site("https://www.bbc.com/", "perftest",
                 reason="a blocked tracker's fallback throws; nothing visible is lost"),
        ])

    def assertRefused(self, text: str, words: str):
        with self.assertRaises(CorpusError) as caught:
            compat_corpus.parse(text)
        self.assertIn(words, str(caught.exception))

    def test_a_site_needs_an_http_url(self):
        self.assertRefused('[[site]]\nurl = "ftp://x.test/"\ncategory = "news"\n', "url")
        self.assertRefused('[[site]]\ncategory = "news"\n', "url")

    def test_an_unknown_category_is_refused(self):
        self.assertRefused('[[site]]\nurl = "https://x.test/"\ncategory = "blogs"\n',
                           "unknown category 'blogs'")

    def test_a_site_is_listed_once(self):
        one = '[[site]]\nurl = "https://x.test/"\ncategory = "news"\n'
        self.assertRefused(one + one, "listed twice")

    def test_an_unknown_field_is_refused(self):
        self.assertRefused('[[site]]\nurl = "https://x.test/"\ncategory = "news"\nlevel = 1\n',
                           "unknown field(s) level")

    def test_an_unknown_action_is_refused(self):
        self.assertRefused('[[site]]\nurl = "https://x.test/"\ncategory = "news"\n'
                           'flow = [{ action = "hover", selector = "a" }]\n',
                           "step 1: unknown action 'hover'")

    def test_an_action_needs_its_fields_and_no_others(self):
        self.assertRefused('[[site]]\nurl = "https://x.test/"\ncategory = "news"\n'
                           'flow = [{ action = "type", selector = "#q" }]\n', "type needs text")
        self.assertRefused('[[site]]\nurl = "https://x.test/"\ncategory = "news"\n'
                           'flow = [{ action = "click", selector = "a", text = "x" }]\n',
                           "click takes no text")

    def test_only_known_keys_are_pressed(self):
        self.assertRefused('[[site]]\nurl = "https://x.test/"\ncategory = "news"\n'
                           'flow = [{ action = "press", key = "F5" }]\n', "unknown key 'F5'")

    def test_a_corpus_has_a_site(self):
        self.assertRefused("", "no site")

    def test_not_toml_is_refused(self):
        self.assertRefused("[[site]\n", "not TOML")



class ShippedCorpusTest(unittest.TestCase):
    def test_the_corpus_loads_and_covers_every_category(self):
        sites = compat_corpus.load()
        self.assertGreaterEqual(len(sites), 140)
        self.assertGreaterEqual(sum(1 for s in sites if s.flow), 20)
        self.assertEqual({s.category for s in sites}, set(compat_corpus.CATEGORIES))


if __name__ == "__main__":
    unittest.main()
