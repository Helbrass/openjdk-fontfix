#
# Copyright (c) 2005, Oracle and/or its affiliates. All rights reserved.
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
#
# @summary The Serialization benchmark test.  This script is only
#          used to run the test under JTREG.
#
# @build bench.BenchInfo bench.HtmlReporter bench.Util bench.Benchmark 
# @build bench.Reporter bench.XmlReporter bench.ConfigFormatException 
# @build bench.Harness bench.TextReporter
# @build bench.serial.BooleanArrays bench.serial.Booleans
# @build bench.serial.ByteArrays bench.serial.Bytes bench.serial.CharArrays
# @build bench.serial.Chars bench.serial.ClassDesc bench.serial.Cons
# @build bench.serial.CustomDefaultObjTrees bench.serial.CustomObjTrees
# @build bench.serial.DoubleArrays bench.serial.Doubles
# @build bench.serial.ExternObjTrees bench.serial.FloatArrays
# @build bench.serial.Floats bench.serial.GetPutFieldTrees
# @build bench.serial.IntArrays bench.serial.Ints bench.serial.LongArrays
# @build bench.serial.Longs bench.serial.Main bench.serial.ObjArrays
# @build bench.serial.ObjTrees bench.serial.ProxyArrays
# @build bench.serial.ProxyClassDesc bench.serial.RepeatObjs
# @build bench.serial.ReplaceTrees bench.serial.ShortArrays
# @build bench.serial.Shorts bench.serial.SmallObjTrees
# @build bench.serial.StreamBuffer bench.serial.Strings
#
# @run shell/timeout=1800 runSerialBench.sh
#
# @author Mike Warres, Nigel Daley

echo "Starting serialization benchmark "

$TESTJAVA/bin/java \
    -server \
    -cp $TESTCLASSES \
    bench.serial.Main \
    -c $TESTSRC/bench/serial/jtreg-config &

