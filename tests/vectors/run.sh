#!/bin/sh
# Build and run the victronble core host tests (plain gcc, no framework).
# Regenerate vectors first with: python3 gen_vectors.py
set -e
cd "$(dirname "$0")"
cc -std=c99 -Wall -Wextra -Werror -I../../include -I../../src \
    ../../src/victronble_core.c ../../src/victronble_aes_sw.c \
    ../../src/crypto/vble_aes.c test_main.c -lm -o victronble_test
./victronble_test
