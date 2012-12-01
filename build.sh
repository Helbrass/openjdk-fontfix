#!/bin/sh

## system-dependent local folders
## path to installed Sun JDK 6:
ALT_BOOTDIR=/opt/jdk1.6.0_26

#########################################################################################
#      DO NOT EDIT
#########################################################################################

#set -x

## exporting local env vars:
export ALT_BOOTDIR
## resolving canonical path of current dir + '/drops' suffix:
export ALT_DROPS_DIR="$( readlink -f "$( dirname "$0" )" )/openjdk7/drops"

## unchangable exports:
export LANG=C
export JAVA_HOME=""
export LD_LIBRARY_PATH=""
export BUILD_NUMBER=b00
export MILESTONE=fontfix
export ALLOW_DOWNLOADS=false

## starting real build process in subshell:
(
  cd $(dirname $0)/openjdk7 && make sanity && make
)
