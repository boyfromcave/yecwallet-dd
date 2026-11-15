#!/usr/bin/env python3
# Copyright (c) 2026 The Ycash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://www.opensource.org/licenses/mit-license.php .
"""Dismiss the code-scanning alerts listed in code-scanning-dismissals.json and export the rest.

Runs in .github/workflows/code-scanning.yml with a GITHUB_TOKEN that has security-events: write.
An entry matches an open alert when the tool name and rule id are equal and the alert's path
matches the entry's path glob (fnmatch; "**" is not special, so a directory needs "dir/*" and a
deeper tree one entry per depth or a "*" prefix). Every matching open alert is dismissed with the
entry's reason ("false positive", "won't fix" or "used in tests") and its comment, truncated to
the 280 characters the API accepts.

An entry may be narrowed with "step": a workflow step name, or a list of them. It then matches only
an alert whose first line (the alert's start_line, read at the alert's commit when that commit is
here, else from the working tree) is that step's `- name:` line, so a new step with the same rule
in the same file stays open. A step name is stable across edits elsewhere in the file, unlike a line
number. A narrowed entry is checked before anything is dismissed: a step name that the file no
longer has (renamed, removed) or a "step" on a path glob is an error, so a stale entry fails loudly
instead of matching nothing in silence. Entries without "step" behave as before. Alerts no entry matches are left open and listed in the step
summary, one line per alert, so the log doubles as the export when artifacts are out of reach.
"""
import argparse
import fnmatch
import json
import os
import re
import subprocess
import sys

REASONS = {"false positive", "won't fix", "used in tests"}


def gh(*args, method="GET", body=None):
    cmd = ["gh", "api", "-X", method] + (["--paginate"] if method == "GET" else []) + list(args)
    if body is not None:
        cmd += ["--input", "-"]
    out = subprocess.run(cmd, input=None if body is None else json.dumps(body), capture_output=True, text=True)
    if out.returncode:
        sys.exit("gh api %s failed: %s" % (" ".join(args), out.stderr.strip()))
    return out.stdout


def pages(raw):
    """gh --paginate concatenates one JSON document per page; join them."""
    dec, i, items = json.JSONDecoder(), 0, []
    while i < len(raw):
        while i < len(raw) and raw[i].isspace():
            i += 1
        if i >= len(raw):
            break
        obj, i = dec.raw_decode(raw, i)
        items += obj
    return items


def entry_steps(e):
    """The step names an entry is narrowed to, as a list (empty: the entry is not narrowed)."""
    steps = e.get("step")
    if steps is None:
        return []
    if isinstance(steps, str):
        steps = [steps]
    if not isinstance(steps, list) or not steps or not all(isinstance(x, str) and x.strip() for x in steps):
        raise ValueError("\"step\" must be a step name or a non-empty list of them: %s" % e)
    return steps


def step_name(line):
    """The step name a workflow line declares (`- name: X`, optionally quoted), else None."""
    m = re.match(r"""^\s*-\s+name:\s*(?:"(.*)"|'(.*)'|(.*?))\s*$""", line or "")
    if not m:
        return None
    return next(g for g in m.groups() if g is not None)


def check_steps(entries, read):
    """Errors for narrowed entries whose steps the file does not declare (read(path) -> text or None)."""
    errors = []
    for e in entries:
        steps = entry_steps(e)
        if not steps:
            continue
        if any(c in e["path"] for c in "*?["):
            errors.append("dismissal entry %s %s: \"step\" needs one exact path, not the glob %s" % (e["tool"], e["rule"], e["path"]))
            continue
        text = read(e["path"])
        if text is None:
            errors.append("dismissal entry %s %s: %s does not exist" % (e["tool"], e["rule"], e["path"]))
            continue
        declared = {step_name(l) for l in text.splitlines()}
        for st in steps:
            if st not in declared:
                errors.append("dismissal entry %s %s: %s has no step named %r (renamed or removed? update the entry)" % (e["tool"], e["rule"], e["path"], st))
    return errors


def matches(e, tool, rule, path, first_line):
    """Does entry e cover an alert (tool, rule, path) whose first line is first_line (text or None)?"""
    if e["tool"] != tool or e["rule"] != rule or not fnmatch.fnmatchcase(path, e["path"]):
        return False
    steps = entry_steps(e)
    return not steps or step_name(first_line) in steps


def read_worktree(path):
    try:
        with open(path, encoding="utf-8") as f:
            return f.read()
    except OSError:
        return None


def alert_line(path, line, commit):
    """Line `line` (1-based) of `path` at `commit` when that commit is here, else in the working tree."""
    text = None
    if commit:
        out = subprocess.run(["git", "show", "%s:%s" % (commit, path)], capture_output=True, text=True)
        if out.returncode == 0:
            text = out.stdout
    if text is None:
        text = read_worktree(path)
    lines = (text or "").splitlines()
    return lines[line - 1] if line and 0 < line <= len(lines) else None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--repo", required=True)
    ap.add_argument("--dismissals", required=True)
    ap.add_argument("--export", help="write the alerts left open here, as JSON")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    with open(args.dismissals, encoding="utf-8") as f:
        entries = json.load(f)["dismissals"]
    for e in entries:
        missing = {"tool", "rule", "path", "reason", "comment"} - set(e)
        if missing or e["reason"] not in REASONS or not e["comment"].strip():
            sys.exit("bad dismissal entry (missing %s, or reason not in %s, or empty comment): %s" % (sorted(missing), sorted(REASONS), e))
    try:
        errors = check_steps(entries, read_worktree)
    except ValueError as err:
        sys.exit("bad dismissal entry: %s" % err)
    if errors:
        for msg in errors:
            print("::error::%s" % msg)
        sys.exit("%d stale or malformed narrowed dismissal entries; nothing dismissed" % len(errors))

    alerts = pages(gh("repos/%s/code-scanning/alerts" % args.repo, "-f", "state=open", "-f", "per_page=100"))
    print("%d open alerts" % len(alerts))
    left, dismissed, used = [], 0, set()
    for a in alerts:
        tool, rule = a["tool"]["name"], a["rule"]["id"]
        inst = a["most_recent_instance"]
        path = inst["location"].get("path", "")
        first = None
        if any(entry_steps(e) for e in entries):
            first = alert_line(path, inst["location"].get("start_line"), inst.get("commit_sha"))
        hit = next((i for i, e in enumerate(entries) if matches(e, tool, rule, path, first)), None)
        if hit is None:
            left.append(a)
            continue
        used.add(hit)
        e = entries[hit]
        print("%s #%d %s %s: %s (%s)" % ("would dismiss" if args.dry_run else "dismiss", a["number"], rule, path, e["reason"], e["comment"][:80]))
        if not args.dry_run:
            gh("repos/%s/code-scanning/alerts/%d" % (args.repo, a["number"]), method="PATCH",
               body={"state": "dismissed", "dismissed_reason": e["reason"], "dismissed_comment": e["comment"][:280]})
        dismissed += 1
    for i, e in enumerate(entries):
        if i not in used:
            print("note: no open alert matches dismissal entry %s %s %s" % (e["tool"], e["rule"], e["path"]))

    summary = ["## Code scanning: %d dismissed, %d left open" % (dismissed, len(left)), ""]
    summary += ["| # | Tool | Severity | Rule | Location |", "|---|---|---|---|---|"]
    for a in sorted(left, key=lambda a: (a["tool"]["name"], a["rule"]["id"], a["most_recent_instance"]["location"].get("path", ""))):
        loc = a["most_recent_instance"]["location"]
        summary.append("| [%d](%s) | %s | %s | `%s` | `%s:%s` |" % (
            a["number"], a["html_url"], a["tool"]["name"], a["rule"].get("security_severity_level") or a["rule"].get("severity"),
            a["rule"]["id"], loc.get("path", ""), loc.get("start_line", "")))
    text = "\n".join(summary) + "\n"
    print(text)
    print("ALERTS-BEGIN")
    for a in left:
        loc = a["most_recent_instance"]["location"]
        print("ALERT", json.dumps({"number": a["number"], "tool": a["tool"]["name"], "rule": a["rule"]["id"],
                                   "severity": a["rule"].get("security_severity_level") or a["rule"].get("severity"),
                                   "path": loc.get("path"), "line": loc.get("start_line"),
                                   "message": a["most_recent_instance"].get("message", {}).get("text")}, separators=(",", ":")))
    print("ALERTS-END")
    if os.environ.get("GITHUB_STEP_SUMMARY"):
        with open(os.environ["GITHUB_STEP_SUMMARY"], "a", encoding="utf-8") as f:
            f.write(text)
    if args.export:
        with open(args.export, "w", encoding="utf-8") as f:
            json.dump(left, f, indent=1)
    return 0


if __name__ == "__main__":
    sys.exit(main())
