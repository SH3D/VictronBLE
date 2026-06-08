#!/usr/bin/env bash
#
# check_version.sh -- verify the library version is consistent and released.
#
# Read-only: confirms the version matches in both manifests and that a matching
# git tag (vX.Y.Z) exists. Run it before tagging a release. Non-zero exit if the
# versions disagree or the tag is missing.
#
# SPDX-License-Identifier: MIT

set -u
cd "$(dirname "$0")" || exit 2

json=$(sed -nE 's/.*"version"[[:space:]]*:[[:space:]]*"([^"]+)".*/\1/p' library.json | head -1)
prop=$(sed -nE 's/^version=([^[:space:]]+).*/\1/p' library.properties | tr -d '\r' | head -1)

echo "library.json       : ${json:-<none>}"
echo "library.properties : ${prop:-<none>}"

fail=0

if [ -n "$json" ] && [ "$json" = "$prop" ]; then
    echo "ok   versions match ($json)"
else
    echo "FAIL versions differ (json=$json properties=$prop)"; fail=1
fi

if git rev-parse -q --verify "refs/tags/v$json" >/dev/null 2>&1; then
    echo "ok   git tag v$json exists"
else
    echo "FAIL no git tag v$json -- create one to release: git tag v$json"; fail=1
fi

exit "$fail"
