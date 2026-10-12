#!/usr/bin/env bash
# Host tests of the RFP extras (no hardware, no ChibiOS): bash rfp/test/run_tests.sh
set -e
cd "$(dirname "$0")/../.."
OUT=${TMPDIR:-/tmp}/rfp_test
gcc -std=gnu11 -Wall -Wextra -Irfp/test/stubs -Irfp -Iutil -I. \
    rfp/test/test_rfp_ext.c rfp/rfp_ext.c rfp/rfp_conf_gen.c util/buffer.c -o "$OUT" -lm
"$OUT"
