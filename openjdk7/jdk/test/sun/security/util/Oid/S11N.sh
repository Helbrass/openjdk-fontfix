#
# Copyright (c) 2004, 2009, Oracle and/or its affiliates. All rights reserved.
# DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
#
# This code is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License version 2 only, as
# published by the Free Software Foundation.
#
# This code is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
# version 2 for more details (a copy is included in the LICENSE file that
# accompanied this code).
#
# You should have received a copy of the GNU General Public License version
# 2 along with this work; if not, write to the Free Software Foundation,
# Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
#
# Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
# or visit www.oracle.com if you need additional information or have any
# questions.
#
# @test
# @bug 4811968 6908628
# @summary Serialization compatibility with old versions (and fix)
# @author Weijun Wang
#
# set a few environment variables so that the shell-script can run stand-alone
# in the source directory

if [ "${TESTSRC}" = "" ] ; then
  TESTSRC="."
fi
if [ "${TESTCLASSES}" = "" ] ; then
  TESTCLASSES="."
fi
if [ "${TESTJAVA}" = "" ] ; then
  echo "TESTJAVA not set.  Test cannot execute."
  echo "FAILED!!!"
  exit 1
fi

# set platform-dependent variables
PF=""

OS=`uname -s`
case "$OS" in
  SunOS )
    FS="/"
    ARCH=`isainfo`
    case "$ARCH" in
      sparc* )
        PF="solaris-sparc"
        ;;
      i[3-6]86 )
        PF="solaris-i586"
        ;;
      amd64* )
        PF="solaris-amd64"
        ;;
      * )
        echo "Unsupported System: Solaris ${ARCH}"
        exit 0;
        ;;
    esac
    ;;
  Linux )
    ARCH=`uname -m`
    FS="/"
    case "$ARCH" in
      i[3-6]86 )
        PF="linux-i586"
        ;;
      amd64* )
        PF="linux-amd64"
        ;;
      * )
        echo "Unsupported System: Linux ${ARCH}"
        exit 0;
        ;;
    esac
    ;;
  Windows* )
    FS="\\"
    PF="windows-i586"

    # 'uname -m' does not give us enough information -
    #  should rely on $PROCESSOR_IDENTIFIER (as is done in Defs-windows.gmk),
    #  but JTREG does not pass this env variable when executing a shell script.
    #
    #  execute test program - rely on it to exit if platform unsupported

    ;;
  * )
    echo "Unsupported System: ${OS}"
    exit 0;
    ;;
esac

# the test code

${TESTJAVA}${FS}bin${FS}javac -target 1.4 -source 1.4 \
        -d . ${TESTSRC}${FS}SerialTest.java || exit 10

OLDJAVA="
    /java/re/j2se/1.6.0/latest/binaries/${PF}
    /java/re/j2se/1.5.0/latest/binaries/${PF}
    /java/re/j2se/1.4.2/latest/binaries/${PF}
"

SMALL="
    0.0
    1.1
    2.2
    1.2.3456
    1.2.2147483647.4
    1.2.268435456.4
"

HUGE="
    2.16.764.1.3101555394.1.0.100.2.1
    1.2.2147483648.4
    2.3.4444444444444444444444
    1.2.888888888888888888.111111111111111.2222222222222.33333333333333333.44444444444444
"

for oid in ${SMALL}; do
    echo ${oid}
    # new ->
    ${TESTJAVA}${FS}bin${FS}java SerialTest out ${oid} > tmp.oid.serial || exit 1
    # -> new
    ${TESTJAVA}${FS}bin${FS}java SerialTest in ${oid} < tmp.oid.serial || exit 2
    for oldj in ${OLDJAVA}; do
        if [ -d ${oldj} ]; then
            echo ${oldj}
            # -> old
            ${oldj}${FS}bin${FS}java SerialTest in ${oid} < tmp.oid.serial || exit 3
            # old ->
            ${oldj}${FS}bin${FS}java SerialTest out ${oid} > tmp.oid.serial.old || exit 4
            # -> new
            ${TESTJAVA}${FS}bin${FS}java SerialTest in ${oid} < tmp.oid.serial.old || exit 5
        fi
    done
done

for oid in ${HUGE}; do
    echo ${oid}
    # new ->
    ${TESTJAVA}${FS}bin${FS}java SerialTest out ${oid} > tmp.oid.serial || exit 1
    # -> new
    ${TESTJAVA}${FS}bin${FS}java SerialTest in ${oid} < tmp.oid.serial || exit 2
    for oldj in ${OLDJAVA}; do
        if [ -d ${oldj} ]; then
            echo ${oldj}
            # -> old
            ${oldj}${FS}bin${FS}java SerialTest badin < tmp.oid.serial || exit 3
        fi
    done
done

rm -f tmp.oid.serial
rm -f tmp.oid.serial.old
rm -f SerialTest.class

for oldj in ${OLDJAVA}; do
    if [ ! -d ${oldj} ]; then
        echo WARNING: ${oldj} is missing. Test incomplete! > /dev/stderr
    fi
done

exit 0
