#!/usr/bin/env python3
# Copyright Max Golovanov.
# SPDX-License-Identifier: Apache-2.0
"""Record the staged tree of ports/socketshpp in versions/ (the git registry database).

Equivalent to `vcpkg x-add-version socketshpp --x-builtin-ports-root=ports
--x-builtin-registry-versions-dir=versions` for this single port, without needing
vcpkg. Stage the port first (git add ports/socketshpp); the version and port-version
come from ports/socketshpp/vcpkg.json. Replaces the entry for the same version, or
adds a new one at the top.
"""
import json, pathlib, subprocess

root = pathlib.Path(__file__).resolve().parent.parent
tree = subprocess.check_output(
    ["git", "write-tree", "--prefix=ports/socketshpp/"], cwd=root, text=True).strip()
manifest = json.loads((root / "ports/socketshpp/vcpkg.json").read_text())
version = manifest.get("version") or manifest["version-string"]
port_version = manifest.get("port-version", 0)

db_path = root / "versions/s-/socketshpp.json"
db = json.loads(db_path.read_text()) if db_path.exists() else {"versions": []}
entries = [e for e in db["versions"]
           if not (e.get("version") == version and e.get("port-version", 0) == port_version)]
entries.insert(0, {"git-tree": tree, "version": version, "port-version": port_version})
db_path.parent.mkdir(parents=True, exist_ok=True)
db_path.write_text(json.dumps({"versions": entries}, indent=2) + "\n")

baseline = {"default": {"socketshpp": {"baseline": version, "port-version": port_version}}}
(root / "versions/baseline.json").write_text(json.dumps(baseline, indent=2) + "\n")
print(f"socketshpp {version}#{port_version} -> git-tree {tree}")
