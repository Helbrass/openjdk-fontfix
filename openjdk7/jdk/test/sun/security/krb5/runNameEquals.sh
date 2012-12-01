#
# Copyright (c) 2009, 2010, Oracle and/or its affiliates. All rights reserved.
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
# @bug 6317711 6944847
# @summary Ensure the GSSName has the correct impl which respects
# the contract for equals and hashCode across different configurations.

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

NATIVE=false

# set platform-dependent variables
OS=`uname -s`
case "$OS" in
  SunOS )
    PATHSEP=":"
    FILESEP="/"
    NATIVE=true
    ;;
  Linux )
    PATHSEP=":"
    FILESEP="/"
    NATIVE=true
    ;;
  CYGWIN* )
    PATHSEP=";"
    FILESEP="/"
    ;;
  Windows* )
    PATHSEP=";"
    FILESEP="\\"
    ;;
  * )
    echo "Unrecognized system!"
    exit 1;
    ;;
esac

TEST=Krb5NameEquals

${TESTJAVA}${FILESEP}bin${FILESEP}javac \
    -d ${TESTCLASSES}${FILESEP} \
    ${TESTSRC}${FILESEP}${TEST}.java

EXIT_STATUS=0

if [ "${NATIVE}" = "true" ] ; then
    echo "Testing native provider"
    ${TESTJAVA}${FILESEP}bin${FILESEP}java \
        -classpath ${TESTCLASSES} \
        -Dsun.security.jgss.native=true \
        ${TEST}
    if [ $? != 0 ] ; then
        echo "Native provider fails"
        EXIT_STATUS=1
    fi
fi

echo "Testing java provider"
${TESTJAVA}${FILESEP}bin${FILESEP}java \
        -classpath ${TESTCLASSES} \
        -Djava.security.krb5.realm=R \
        -Djava.security.krb5.kdc=127.0.0.1 \
        ${TEST}
if [ $? != 0 ] ; then
    echo "Java provider fails"
    EXIT_STATUS=1
fi

exit ${EXIT_STATUS}
