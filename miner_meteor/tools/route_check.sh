#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>
# Build the route tools with the port's game.cpp and levels.cpp for the host.
#   tools/route_check.sh              play tools/routes.json through all six caverns
#   tools/route_check.sh search N     search a route through cavern N (1-6)
set -e
cd "$(dirname "$0")/.."
mkdir -p tools/build
CXX=${CXX:-c++}
FLAGS="-O2 -std=c++11 -w -I. -I../st_port -I../st_port/compat"
if [ "$1" = search ]; then
    $CXX $FLAGS -o tools/build/route_search tools/route_search.cpp game.cpp levels.cpp
    exec tools/build/route_search "$2"
fi
$CXX $FLAGS -o tools/build/route_check tools/route_check.cpp game.cpp levels.cpp
exec tools/build/route_check tools/routes.json
