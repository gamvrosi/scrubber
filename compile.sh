#!/bin/bash
# Prep the environment for future recompiles
export CLEAN_SOURCE=no
export CONCURRENCY_LEVEL=5
echo CLEAN_SOURCE=$CLEAN_SOURCE
echo CONCURRENCY_LEVEL=$CONCURRENCY_LEVEL

# Compile it!
cd linux-2.6.35
make-kpkg clean
time fakeroot make-kpkg --initrd --append-to-version=-scrub kernel_image kernel_headers

