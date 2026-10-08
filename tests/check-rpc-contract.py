#!/usr/bin/env python3
# Copyright (c) 2026 The Ycash developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or https://www.opensource.org/licenses/mit-license.php .

"""Check src/yellowbackrpc.h against docs/yellowback-rpc-contract.json (plan P7, §6.0 item 6).

The header is the single place the wallet names anything of the node's RPC surface; the JSON
is the generated copy of ycash-dd/doc/yellowback-rpc.md (the workspace's `make spec`). This
script asserts:

  1. `RPC_VERSION` in the header equals the JSON's `rpcversion`.
  2. Every `yed_*` method constant is a top-level command in the JSON.
  3. Every namespace carries a `// contract: <command>[.path...][[]]` marker, and every
     `constexpr const char*` constant in it that is not marked `// value` is a key of the JSON
     object the marker names (`[]` selects the example row of a list result). The marker
     `errors` checks the Errors namespace against the JSON's error identifiers.
  4. A constant marked `// optional` names a field the contract text marks **optional** and its
     example omits (rpcversion 5: `yed_getvault.intents` while CLAIMING); it is not looked up. A
     namespace marked `// contract: optional <command>.<path>` holds the keys of such an absent
     object; its constants are not looked up either, but the command must exist.

  5. With `--spec <yellowback-spec.md>` (the node's generated doc/yellowback-spec.md of the in-term
     line, `make spec-in-term`): the collateral bullet of its section 8.1 trust statement (the in-term
     plan's IT-8 promise, "- Your YEC is locked ...") equals, whitespace-normalised, the string
     YellowbackTab::inTermPromise() returns in src/yellowbacktab.cpp, so the wallet's disclosure is the
     promise verbatim.

Exit code 0 when everything matches; 1 with one line per finding otherwise. No dependencies
beyond the standard library, so the CI job runs it with the platform's python3 and a developer
with the workspace venv.
"""
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
HEADER = os.path.join(REPO, "src", "yellowbackrpc.h")
CONTRACT = os.path.join(REPO, "docs", "yellowback-rpc-contract.json")

RE_VERSION = re.compile(r"constexpr\s+int\s+RPC_VERSION\s*=\s*(\d+)\s*;")
RE_NAMESPACE = re.compile(r"^\s*namespace\s+(\w+)\s*\{\s*(?://\s*contract:\s*(\S+))?")
RE_CONST = re.compile(r'constexpr\s+const\s+char\*\s+(\w+)\s*=\s*"([^"]*)"\s*;(.*)$')
TAB = os.path.join(REPO, "src", "yellowbacktab.cpp")
PROMISE_OPENING = "Your YEC is locked"

RE_METHOD = re.compile(r'constexpr\s+const\s+char\*\s+\w+\s*=\s*"(yed_\w+)"\s*;')


def resolve(contract, marker):
    """Return the JSON object a marker names, or None with a reason."""
    if marker == "errors":
        return contract.get("errors"), None
    path = marker
    node = contract
    for step in path.split("."):
        want_row = step.endswith("[]")
        key = step[:-2] if want_row else step
        if node is None:
            return None, "path %s runs past a null" % marker
        if key not in node:
            return None, "no %r under %s" % (key, marker)
        node = node[key]
        if key.startswith("yed_"):
            node = node.get("returns")
        if want_row:
            if not isinstance(node, list) or not node:
                return None, "%s is not a non-empty list" % marker
            node = node[0]
    if not isinstance(node, dict):
        return None, "%s is not an object" % marker
    return node, None


def spec_promise(path):
    """The section 8.1 bullet that opens with PROMISE_OPENING, its continuation lines joined."""
    with open(path, encoding="utf-8") as f:
        lines = f.read().splitlines()
    in81, out = False, None
    for line in lines:
        if line.startswith("### "):
            in81 = line.startswith("### 8.1")
            continue
        if not in81:
            continue
        if out is None:
            if line.startswith("- " + PROMISE_OPENING):
                out = [line[2:]]
        elif line.startswith("  ") and line.strip():
            out.append(line.strip())
        else:
            break
    return " ".join(" ".join(out).split()) if out else None


def wallet_promise():
    """The C++ string literals of YellowbackTab::inTermPromise(), concatenated."""
    with open(TAB, encoding="utf-8") as f:
        text = f.read()
    m = re.search(r"QString\s+YellowbackTab::inTermPromise\(\)\s*\{(.*?)\n\}", text, re.S)
    if not m:
        return None
    lits = re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(1))
    return " ".join("".join(l.replace('\\"', '"') for l in lits).split())


def check_spec(path):
    want, have = spec_promise(path), wallet_promise()
    if want is None:
        return ["%s: section 8.1 has no bullet opening %r" % (path, PROMISE_OPENING)]
    if have is None:
        return ["%s: no YellowbackTab::inTermPromise()" % TAB]
    if want != have:
        return ["the wallet's inTermPromise() differs from %s section 8.1:\n  spec:   %s\n  wallet: %s" % (path, want, have)]
    return []


def main():
    findings = []
    if len(sys.argv) == 3 and sys.argv[1] == "--spec":
        findings += check_spec(sys.argv[2])
        if not findings:
            print("YellowbackTab::inTermPromise() equals the trust statement of %s (section 8.1)" % sys.argv[2])
    elif len(sys.argv) != 1:
        print("usage: check-rpc-contract.py [--spec <yellowback-spec.md>]")
        return 2
    with open(CONTRACT, encoding="utf-8") as f:
        contract = json.load(f)
    with open(HEADER, encoding="utf-8") as f:
        lines = f.read().splitlines()

    header_text = "\n".join(lines)
    m = RE_VERSION.search(header_text)
    if not m:
        findings.append("RPC_VERSION not found in %s" % HEADER)
    elif int(m.group(1)) != contract.get("rpcversion"):
        findings.append("RPC_VERSION %s != contract rpcversion %s" % (m.group(1), contract.get("rpcversion")))

    for method in RE_METHOD.findall(header_text):
        if method not in contract:
            findings.append("method %s is not a command in the contract" % method)

    namespace = None      # (name, marker, json object or None)
    for lineno, line in enumerate(lines, 1):
        nm = RE_NAMESPACE.match(line)
        if nm and nm.group(1) != "YellowbackRpc":
            name, marker = nm.group(1), nm.group(2)
            if marker == "optional":
                # `// contract: optional yed_x.path`: an object the example omits; the command must exist
                rest = line.split("optional", 1)[1].strip().split()
                command = rest[0].split(".")[0] if rest else ""
                if command not in contract:
                    findings.append("%s:%d namespace %s: optional object of unknown command %r" % (HEADER, lineno, name, command))
                namespace = (name, marker, None)
                continue
            if marker is None:
                namespace = (name, None, None)
            else:
                obj, why = resolve(contract, marker)
                if obj is None:
                    findings.append("%s:%d namespace %s: %s" % (HEADER, lineno, name, why))
                namespace = (name, marker, obj)
            continue
        if namespace and line.strip().startswith("}"):
            namespace = None
            continue
        cm = RE_CONST.search(line)
        if not cm or namespace is None:
            continue
        cname, value, rest = cm.group(1), cm.group(2), cm.group(3)
        name, marker, obj = namespace
        if marker is None:
            if name != "RpcErrors":
                findings.append("%s:%d namespace %s has no `// contract:` marker" % (HEADER, lineno, name))
            continue
        if re.search(r"//\s*(value|optional)\b", rest):
            continue
        if obj is None:
            continue
        if value not in obj:
            findings.append("%s:%d %s::%s = %r is not a key under %s" % (HEADER, lineno, name, cname, value, marker))

    for line in findings:
        print(line)
    if findings:
        print("%d finding(s)" % len(findings))
        return 1
    print("yellowbackrpc.h matches docs/yellowback-rpc-contract.json (rpcversion %s)" % contract.get("rpcversion"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
