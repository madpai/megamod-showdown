#!/bin/sh
# Three separate headless processes, no Trial assets or renderer required.
set -eu
cd "$(dirname "$0")/.."
port=${1:-32270}
seconds=${2:-8}
cmake --build build-host --target htanet >/dev/null
tmp=$(mktemp -d)
cleanup() { kill "$server" "$a" "$b" 2>/dev/null || true; rm -rf "$tmp"; }
trap cleanup EXIT INT TERM
./build-host/htanet server "$port" "$seconds" >"$tmp/server" 2>&1 & server=$!
sleep 0.2
./build-host/htanet client 127.0.0.1 "$port" "$seconds" >"$tmp/a" 2>&1 & a=$!
./build-host/htanet client 127.0.0.1 "$port" "$seconds" >"$tmp/b" 2>&1 & b=$!
wait "$a"
wait "$b"
wait "$server"
cat "$tmp/server" "$tmp/a" "$tmp/b"
