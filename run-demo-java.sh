#!/bin/sh
#set -x

#OPT_FLAGS="-Dawt.useSystemAAFontSettings=lcd"
OPT_FLAGS=

thisdirname=$( readlink -f "$( dirname "$0" )" )
javai586="$thisdirname/openjdk7/build/linux-i586/j2re-image/bin/java"
javax86="$thisdirname/openjdk7/build/linux-amd64/j2re-image/bin/java"

if [ -x $javai586 ] ; then
  $javai586 $OPT_FLAGS -jar $thisdirname/demo/java/dist/javafontview.jar $*
elif [ -x $javax86 ] ; then
  $javax86 $OPT_FLAGS -jar $thisdirname/demo/java/dist/javafontview.jar $*
else
  echo 'No builded OpenJDK found. Build openjdk from this repository before launching test application.'
  exit 1;
fi
