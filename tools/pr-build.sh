#!/bin/sh

set -e

echo "Running PR build (all modules, SSL disabled)"
(
cd "$TRAVIS_BUILD_DIR"/app/include || exit
# uncomment disabled modules e.g. '//#define LUA_USE_MODULES_UCG' -> '#define LUA_USE_MODULES_UCG'
sed -E -i.bak 's@(//.*)(#define *LUA_USE_MODULES_.*)@\2@g' user_modules.h
cat user_modules.h

# disable SSL
sed -i.bak 's@#define CLIENT_SSL_ENABLE@//#define CLIENT_SSL_ENABLE@' user_config.h
cat user_config.h

# change to "root" directory no matter where the script was started from
cd "$TRAVIS_BUILD_DIR" || exit
make clean
make
)
