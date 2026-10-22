#!/bin/bash
# Deterministic parser regressions; all injection is confined to this sourced-helper harness.
set -eu
source "${1:-$(dirname "$0")/../bin/gryphfield-profile}"
PREFIX='/tmp/profile prefix'
comm='/wine/bin/wine64-preloader'
args='wine64 C:\games\Endfield.exe'
from_engine=1   # the candidate's executable is (1) or is not (0) this engine's Wine loader
candidates='101'
profile_candidates() { printf '%s\n' "$candidates"; }
profile_comm() { printf '%s\n' "$comm"; }
profile_arguments() { printf '%s\n' "$args"; }
profile_from_engine() { [ "$from_engine" = 1 ]; }
expect() {
    local actual rc=0
    actual="$(profile_target)" || rc=$?
    [ "$rc" = "$1" ] && [ "$actual" = "${2:-}" ] || {
        echo "unexpected target: rc=$rc pid=$actual comm=$comm args=$args from_engine=$from_engine" >&2
        exit 1
    }
}
expect 0 101
args='wine64 "C:\Program Files\Endfield.exe"'
expect 0 101
args='C:\Program Files\Endfield.exe'
expect 0 101
args='wine64 C:\Program Files\Endfield.exe'
expect 0 101
args='wine64 C:\cmd.exe /c C:\games\Endfield.exe'
expect 1
args='C:\games\Endfield.exe'
expect 0 101
args='wine64 C:\games\NotEndfield.exe'
expect 1
args='wine64 cmd.exe /c echo Endfield.exe'
expect 1
args='wine64 explorer.exe Endfield.exe'
expect 1
args='wine64 C:\games\Endfield.exe.bak'
expect 1
args='wine64 C:\games\Endfield.exe'
comm='/tmp/NotEndfield.exe'
expect 1
comm='C:\Program Files\Endfield.exe'
expect 0 101
comm='/tmp/Endfield.exe'
# an Endfield.exe run by another Wine (CrossOver, a different engine) is never selected
from_engine=0
expect 1
from_engine=1
expect 0 101
candidates='101 102'
expect 2
candidates=''
expect 1
echo 'PASS strict target parsing'
