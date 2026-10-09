#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
./build/digivice-core --replay 12345 <<'EVENTS'
feed 0
play 0
walk 100
card 1
attack 0
attack 0
attack 0
capture 0
EVENTS
