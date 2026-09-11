#!/usr/bin/env python3
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


def main():
    findings = []
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
        if re.search(r"//\s*value\b", rest):
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
