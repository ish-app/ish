#!/bin/sh
set -eu

gcc -O2 -pthread fd_race.c -o fd_race
./fd_race