#!/usr/bin/env python3
# Copyright (c) 2026 The Ycash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://www.opensource.org/licenses/mit-license.php .
"""Unit tests of code-scanning-triage.py's matcher: python3 contrib/secops/test_code_scanning_triage.py"""
import importlib.util
import json
import os
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
_spec = importlib.util.spec_from_file_location("triage", os.path.join(HERE, "code-scanning-triage.py"))
triage = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(triage)

WF = ".github/workflows/release.yml"
TEXT = """jobs:
  build:
    steps:
      - name: Check out the node
        uses: actions/checkout@x
      - name: "Build it"
        run: make
      - name: Another checkout
        uses: actions/checkout@x
"""


def entry(**kw):
    e = {"tool": "CodeQL", "rule": "actions/untrusted-checkout/medium", "path": WF,
         "reason": "false positive", "comment": "c"}
    e.update(kw)
    return e


class Matcher(unittest.TestCase):
    def test_unnarrowed_entry_matches_the_whole_file_as_before(self):
        e = entry()
        self.assertTrue(triage.matches(e, "CodeQL", e["rule"], WF, "      - name: Another checkout"))
        self.assertTrue(triage.matches(e, "CodeQL", e["rule"], WF, None))
        self.assertFalse(triage.matches(e, "CodeQL", "other/rule", WF, None))
        self.assertFalse(triage.matches(e, "Semgrep", e["rule"], WF, None))
        self.assertFalse(triage.matches(e, "CodeQL", e["rule"], "other.yml", None))

    def test_glob_path_still_works(self):
        e = entry(path=".github/workflows/*")
        self.assertTrue(triage.matches(e, "CodeQL", e["rule"], WF, None))

    def test_step_narrows_to_that_step(self):
        e = entry(step="Check out the node")
        self.assertTrue(triage.matches(e, "CodeQL", e["rule"], WF, "      - name: Check out the node"))
        self.assertFalse(triage.matches(e, "CodeQL", e["rule"], WF, "      - name: Another checkout"))
        # An alert whose line cannot be read is never covered by a narrowed entry.
        self.assertFalse(triage.matches(e, "CodeQL", e["rule"], WF, None))
        self.assertFalse(triage.matches(e, "CodeQL", e["rule"], WF, "        uses: actions/checkout@x"))

    def test_step_list_and_quoted_names(self):
        e = entry(step=["Build it", "Check out the node"])
        self.assertTrue(triage.matches(e, "CodeQL", e["rule"], WF, '      - name: "Build it"'))
        self.assertTrue(triage.matches(e, "CodeQL", e["rule"], WF, "      - name: 'Build it'  "))
        self.assertTrue(triage.matches(e, "CodeQL", e["rule"], WF, "      - name: Check out the node"))
        self.assertFalse(triage.matches(e, "CodeQL", e["rule"], WF, "      - name: Build it again"))

    def test_bad_step_values_are_refused(self):
        for bad in ([], "", [""], 3, [1]):
            with self.assertRaises(ValueError):
                triage.entry_steps(entry(step=bad))


class StaleEntries(unittest.TestCase):
    def read(self, path):
        return TEXT if path == WF else None

    def test_known_steps_pass(self):
        self.assertEqual(triage.check_steps([entry(step=["Check out the node", "Build it"]), entry()], self.read), [])

    def test_renamed_step_fails_loudly(self):
        errs = triage.check_steps([entry(step="Check out the nodes")], self.read)
        self.assertEqual(len(errs), 1)
        self.assertIn("no step named", errs[0])

    def test_missing_file_and_glob_fail(self):
        self.assertIn("does not exist", triage.check_steps([entry(path="gone.yml", step="x")], self.read)[0])
        self.assertIn("glob", triage.check_steps([entry(path=".github/*", step="Build it")], self.read)[0])

    def test_this_repository_entries_are_current(self):
        root = os.path.dirname(os.path.dirname(HERE))
        with open(os.path.join(HERE, "code-scanning-dismissals.json"), encoding="utf-8") as f:
            entries = json.load(f)["dismissals"]

        def read(path):
            try:
                with open(os.path.join(root, path), encoding="utf-8") as f:
                    return f.read()
            except OSError:
                return None
        self.assertEqual(triage.check_steps(entries, read), [])


if __name__ == "__main__":
    unittest.main()
