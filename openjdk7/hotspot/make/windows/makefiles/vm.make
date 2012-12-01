#
# Copyright (c) 1997, 2011, Oracle and/or its affiliates. All rights reserved.
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
#

# Resource file containing VERSIONINFO
Res_Files=.\version.res

!include ..\generated\objfiles.make

COMMONSRC=$(WorkSpace)\src
ALTSRC=$(WorkSpace)\src\closed

!ifdef RELEASE 
!ifdef DEVELOP
CPP_FLAGS=$(CPP_FLAGS) /D "DEBUG"
!else
CPP_FLAGS=$(CPP_FLAGS) /D "PRODUCT"
!endif
!else
CPP_FLAGS=$(CPP_FLAGS) /D "ASSERT"
!endif

!if "$(Variant)" == "core"
# No need to define anything, CORE is defined as !COMPILER1 && !COMPILER2
!endif

!if "$(Variant)" == "kernel"
CPP_FLAGS=$(CPP_FLAGS) /D "KERNEL"
!endif

!if "$(Variant)" == "compiler1"
CPP_FLAGS=$(CPP_FLAGS) /D "COMPILER1"
!endif

!if "$(Variant)" == "compiler2"
CPP_FLAGS=$(CPP_FLAGS) /D "COMPILER2"
!endif

!if "$(Variant)" == "tiered"
CPP_FLAGS=$(CPP_FLAGS) /D "COMPILER1" /D "COMPILER2"
!endif

!if "$(BUILDARCH)" == "i486"
HOTSPOT_LIB_ARCH=i386
!else
HOTSPOT_LIB_ARCH=$(BUILDARCH)
!endif

# The following variables are defined in the generated local.make file.
CPP_FLAGS=$(CPP_FLAGS) /D "HOTSPOT_RELEASE_VERSION=\"$(HS_BUILD_VER)\""
CPP_FLAGS=$(CPP_FLAGS) /D "JRE_RELEASE_VERSION=\"$(JRE_RELEASE_VER)\""
CPP_FLAGS=$(CPP_FLAGS) /D "HOTSPOT_LIB_ARCH=\"$(HOTSPOT_LIB_ARCH)\""
CPP_FLAGS=$(CPP_FLAGS) /D "HOTSPOT_BUILD_TARGET=\"$(BUILD_FLAVOR)\""
CPP_FLAGS=$(CPP_FLAGS) /D "HOTSPOT_BUILD_USER=\"$(BuildUser)\""
CPP_FLAGS=$(CPP_FLAGS) /D "HOTSPOT_VM_DISTRO=\"$(HOTSPOT_VM_DISTRO)\""

CPP_FLAGS=$(CPP_FLAGS) $(CPP_INCLUDE_DIRS)

# Define that so jni.h is on correct side
CPP_FLAGS=$(CPP_FLAGS) /D "_JNI_IMPLEMENTATION_"

!if "$(BUILDARCH)" == "ia64"
STACK_SIZE="/STACK:1048576,262144"
!else
STACK_SIZE=
!endif

!if "$(BUILDARCH)" == "ia64"
# AsyncGetCallTrace is not supported on IA64 yet
AGCT_EXPORT=
!else
!if "$(Variant)" == "kernel"
AGCT_EXPORT=
!else
AGCT_EXPORT=/export:AsyncGetCallTrace
!endif
!endif

# If you modify exports below please do the corresponding changes in
# src/share/tools/ProjectCreator/WinGammaPlatformVC7.java 
LINK_FLAGS=$(LINK_FLAGS) $(STACK_SIZE) /subsystem:windows /dll /base:0x8000000 \
  /export:JNI_GetDefaultJavaVMInitArgs       \
  /export:JNI_CreateJavaVM                   \
  /export:JVM_FindClassFromBootLoader        \
  /export:JNI_GetCreatedJavaVMs              \
  /export:jio_snprintf                       \
  /export:jio_printf                         \
  /export:jio_fprintf                        \
  /export:jio_vfprintf                       \
  /export:jio_vsnprintf                      \
  $(AGCT_EXPORT)                             \
  /export:JVM_GetVersionInfo                 \
  /export:JVM_GetThreadStateNames            \
  /export:JVM_GetThreadStateValues           \
  /export:JVM_InitAgentProperties

CPP_INCLUDE_DIRS=/I "..\generated"

!if exists($(ALTSRC)\share\vm)
CPP_INCLUDE_DIRS=$(CPP_INCLUDE_DIRS) /I "$(ALTSRC)\share\vm"
!endif

!if exists($(ALTSRC)\os\windows\vm)
CPP_INCLUDE_DIRS=$(CPP_INCLUDE_DIRS) /I "$(ALTSRC)\os\windows\vm"
!endif

!if exists($(ALTSRC)\os_cpu\windows_$(Platform_arch)\vm)
CPP_INCLUDE_DIRS=$(CPP_INCLUDE_DIRS) /I "$(ALTSRC)\os_cpu\windows_$(Platform_arch)\vm"
!endif

!if exists($(ALTSRC)\cpu\$(Platform_arch)\vm)
CPP_INCLUDE_DIRS=$(CPP_INCLUDE_DIRS) /I "$(ALTSRC)\cpu\$(Platform_arch)\vm"
!endif

CPP_INCLUDE_DIRS=$(CPP_INCLUDE_DIRS) \
  /I "$(COMMONSRC)\share\vm" \
  /I "$(COMMONSRC)\share\vm\prims" \
  /I "$(COMMONSRC)\os\windows\vm" \
  /I "$(COMMONSRC)\os_cpu\windows_$(Platform_arch)\vm" \
  /I "$(COMMONSRC)\cpu\$(Platform_arch)\vm"

CPP_DONT_USE_PCH=/D DONT_USE_PRECOMPILED_HEADER

!if "$(USE_PRECOMPILED_HEADER)" != "0"
CPP_USE_PCH=/Fp"vm.pch" /Yu"precompiled.hpp"
!else
CPP_USE_PCH=$(CPP_DONT_USE_PCH)
!endif

# Where to find the source code for the virtual machine (is this used?)
VM_PATH=../generated
VM_PATH=$(VM_PATH);../generated/adfiles
VM_PATH=$(VM_PATH);../generated/jvmtifiles
VM_PATH=$(VM_PATH);$(WorkSpace)/src/share/vm/c1
VM_PATH=$(VM_PATH);$(WorkSpace)/src/share/vm/compiler
VM_PATH=$(VM_PATH);$(WorkSpace)/src/share/vm/code
VM_PATH=$(VM_PATH);$(WorkSpace)/src/share/vm/interpreter
VM_PATH=$(VM_PATH);$(WorkSpace)/src/share/vm/ci
VM_PATH=$(VM_PATH);$(WorkSpace)/src/share/vm/classfile
VM_PATH=$(VM_PATH);$(WorkSpace)/src/share/vm/gc_implementation/parallelScavenge
VM_PATH=$(VM_PATH);$(WorkSpace)/src/share/vm/gc_implementation/shared
VM_PATH=$(VM_PATH);$(WorkSpace)/src/share/vm/gc_implementation/parNew
VM_PATH=$(VM_PATH);$(WorkSpace)/src/share/vm/gc_implementation/concurrentMarkSweep
VM_PATH=$(VM_PATH);$(WorkSpace)/src/share/vm/gc_implementation/g1
VM_PATH=$(VM_PATH);$(WorkSpace)/src/share/vm/gc_interface
VM_PATH=$(VM_PATH);$(WorkSpace)/src/share/vm/asm
VM_PATH=$(VM_PATH);$(WorkSpace)/src/share/vm/memory
VM_PATH=$(VM_PATH);$(WorkSpace)/src/share/vm/oops
VM_PATH=$(VM_PATH);$(WorkSpace)/src/share/vm/prims
VM_PATH=$(VM_PATH);$(WorkSpace)/src/share/vm/runtime
VM_PATH=$(VM_PATH);$(WorkSpace)/src/share/vm/services
VM_PATH=$(VM_PATH);$(WorkSpace)/src/share/vm/utilities
VM_PATH=$(VM_PATH);$(WorkSpace)/src/share/vm/libadt
VM_PATH=$(VM_PATH);$(WorkSpace)/src/os/windows/vm
VM_PATH=$(VM_PATH);$(WorkSpace)/src/os_cpu/windows_$(Platform_arch)/vm
VM_PATH=$(VM_PATH);$(WorkSpace)/src/cpu/$(Platform_arch)/vm
VM_PATH=$(VM_PATH);$(WorkSpace)/src/share/vm/opto

VM_PATH={$(VM_PATH)}

# Special case files not using precompiled header files.

c1_RInfo_$(Platform_arch).obj: $(WorkSpace)\src\cpu\$(Platform_arch)\vm\c1_RInfo_$(Platform_arch).cpp 
	 $(CPP) $(CPP_FLAGS) $(CPP_DONT_USE_PCH) /c $(WorkSpace)\src\cpu\$(Platform_arch)\vm\c1_RInfo_$(Platform_arch).cpp

os_windows.obj: $(WorkSpace)\src\os\windows\vm\os_windows.cpp
        $(CPP) $(CPP_FLAGS) $(CPP_DONT_USE_PCH) /c $(WorkSpace)\src\os\windows\vm\os_windows.cpp

os_windows_$(Platform_arch).obj: $(WorkSpace)\src\os_cpu\windows_$(Platform_arch)\vm\os_windows_$(Platform_arch).cpp
        $(CPP) $(CPP_FLAGS) $(CPP_DONT_USE_PCH) /c $(WorkSpace)\src\os_cpu\windows_$(Platform_arch)\vm\os_windows_$(Platform_arch).cpp

osThread_windows.obj: $(WorkSpace)\src\os\windows\vm\osThread_windows.cpp
        $(CPP) $(CPP_FLAGS) $(CPP_DONT_USE_PCH) /c $(WorkSpace)\src\os\windows\vm\osThread_windows.cpp

conditionVar_windows.obj: $(WorkSpace)\src\os\windows\vm\conditionVar_windows.cpp
        $(CPP) $(CPP_FLAGS) $(CPP_DONT_USE_PCH) /c $(WorkSpace)\src\os\windows\vm\conditionVar_windows.cpp

getThread_windows_$(Platform_arch).obj: $(WorkSpace)\src\os_cpu\windows_$(Platform_arch)\vm\getThread_windows_$(Platform_arch).cpp
        $(CPP) $(CPP_FLAGS) $(CPP_DONT_USE_PCH) /c $(WorkSpace)\src\os_cpu\windows_$(Platform_arch)\vm\getThread_windows_$(Platform_arch).cpp

opcodes.obj: $(WorkSpace)\src\share\vm\opto\opcodes.cpp
        $(CPP) $(CPP_FLAGS) $(CPP_DONT_USE_PCH) /c $(WorkSpace)\src\share\vm\opto\opcodes.cpp

bytecodeInterpreter.obj: $(WorkSpace)\src\share\vm\interpreter\bytecodeInterpreter.cpp
        $(CPP) $(CPP_FLAGS) $(CPP_DONT_USE_PCH) /c $(WorkSpace)\src\share\vm\interpreter\bytecodeInterpreter.cpp

bytecodeInterpreterWithChecks.obj: ..\generated\jvmtifiles\bytecodeInterpreterWithChecks.cpp
        $(CPP) $(CPP_FLAGS) $(CPP_DONT_USE_PCH) /c ..\generated\jvmtifiles\bytecodeInterpreterWithChecks.cpp

# Default rules for the Virtual Machine
{$(COMMONSRC)\share\vm\c1}.cpp.obj::
        $(CPP) $(CPP_FLAGS) $(CPP_USE_PCH) /c $<

{$(COMMONSRC)\share\vm\compiler}.cpp.obj::
        $(CPP) $(CPP_FLAGS) $(CPP_USE_PCH) /c $<

{$(COMMONSRC)\share\vm\code}.cpp.obj::
        $(CPP) $(CPP_FLAGS) $(CPP_USE_PCH) /c $<

{$(COMMONSRC)\share\vm\interpreter}.cpp.obj::
        $(CPP) $(CPP_FLAGS) $(CPP_USE_PCH) /c $<

{$(COMMONSRC)\share\vm\ci}.cpp.obj::
        $(CPP) $(CPP_FLAGS) $(CPP_USE_PCH) /c $<

{$(COMMONSRC)\share\vm\classfile}.cpp.obj::
        $(CPP) $(CPP_FLAGS) $(CPP_USE_PCH) /c $<

{$(COMMONSRC)\share\vm\gc_implementation\parallelScavenge}.cpp.obj::
        $(CPP) $(CPP_FLAGS) $(CPP_USE_PCH) /c $<

{$(COMMONSRC)\share\vm\gc_implementation\shared}.cpp.obj::
        $(CPP) $(CPP_FLAGS) $(CPP_USE_PCH) /c $<

{$(COMMONSRC)\share\vm\gc_implementation\parNew}.cpp.obj::
        $(CPP) $(CPP_FLAGS) $(CPP_USE_PCH) /c $<

{$(COMMONSRC)\share\vm\gc_implementation\concurrentMarkSweep}.cpp.obj::
        $(CPP) $(CPP_FLAGS) $(CPP_USE_PCH) /c $<

{$(COMMONSRC)\share\vm\gc_implementation\g1}.cpp.obj::
        $(CPP) $(CPP_FLAGS) $(CPP_USE_PCH) /c $<

{$(COMMONSRC)\share\vm\gc_interface}.cpp.obj::
        $(CPP) $(CPP_FLAGS) $(CPP_USE_PCH) /c $<

{$(COMMONSRC)\share\vm\asm}.cpp.obj::
        $(CPP) $(CPP_FLAGS) $(CPP_USE_PCH) /c $<

{$(COMMONSRC)\share\vm\memory}.cpp.obj::
        $(CPP) $(CPP_FLAGS) $(CPP_USE_PCH) /c $<

{$(COMMONSRC)\share\vm\oops}.cpp.obj::
        $(CPP) $(CPP_FLAGS) $(CPP_USE_PCH) /c $<

{$(COMMONSRC)\share\vm\prims}.cpp.obj::
        $(CPP) $(CPP_FLAGS) $(CPP_USE_PCH) /c $<

{$(COMMONSRC)\share\vm\runtime}.cpp.obj::
        $(CPP) $(CPP_FLAGS) $(CPP_USE_PCH) /c $<

{$(COMMONSRC)\share\vm\services}.cpp.obj::
        $(CPP) $(CPP_FLAGS) $(CPP_USE_PCH) /c $<

{$(COMMONSRC)\share\vm\utilities}.cpp.obj::
        $(CPP) $(CPP_FLAGS) $(CPP_USE_PCH) /c $<

{$(COMMONSRC)\share\vm\libadt}.cpp.obj::
        $(CPP) $(CPP_FLAGS) $(CPP_USE_PCH) /c $<

{$(COMMONSRC)\share\vm\opto}.cpp.obj::
        $(CPP) $(CPP_FLAGS) $(CPP_USE_PCH) /c $<

{$(COMMONSRC)\os\windows\vm}.cpp.obj::
        $(CPP) $(CPP_FLAGS) $(CPP_USE_PCH) /c $<

# This guy should remain a single colon rule because
# otherwise we can't specify the output filename.
{$(COMMONSRC)\os\windows\vm}.rc.res:
        @$(RC) $(RC_FLAGS) /fo"$@" $<

{$(COMMONSRC)\cpu\$(Platform_arch)\vm}.cpp.obj::
        $(CPP) $(CPP_FLAGS) $(CPP_USE_PCH) /c $<

{$(COMMONSRC)\os_cpu\windows_$(Platform_arch)\vm}.cpp.obj::
        $(CPP) $(CPP_FLAGS) $(CPP_USE_PCH) /c $<

{$(ALTSRC)\share\vm\c1}.cpp.obj::
        $(CPP) $(CPP_FLAGS) $(CPP_USE_PCH) /c $<

{$(ALTSRC)\share\vm\compiler}.cpp.obj::
        $(CPP) $(CPP_FLAGS) $(CPP_USE_PCH) /c $<

{$(ALTSRC)\share\vm\code}.cpp.obj::
        $(CPP) $(CPP_FLAGS) $(CPP_USE_PCH) /c $<

{$(ALTSRC)\share\vm\interpreter}.cpp.obj::
        $(CPP) $(CPP_FLAGS) $(CPP_USE_PCH) /c $<

{$(ALTSRC)\share\vm\ci}.cpp.obj::
        $(CPP) $(CPP_FLAGS) $(CPP_USE_PCH) /c $<

{$(ALTSRC)\share\vm\classfile}.cpp.obj::
        $(CPP) $(CPP_FLAGS) $(CPP_USE_PCH) /c $<

{$(ALTSRC)\share\vm\gc_implementation\parallelScavenge}.cpp.obj::
        $(CPP) $(CPP_FLAGS) $(CPP_USE_PCH) /c $<

{$(ALTSRC)\share\vm\gc_implementation\shared}.cpp.obj::
        $(CPP) $(CPP_FLAGS) $(CPP_USE_PCH) /c $<

{$(ALTSRC)\share\vm\gc_implementation\parNew}.cpp.obj::
        $(CPP) $(CPP_FLAGS) $(CPP_USE_PCH) /c $<

{$(ALTSRC)\share\vm\gc_implementation\concurrentMarkSweep}.cpp.obj::
        $(CPP) $(CPP_FLAGS) $(CPP_USE_PCH) /c $<

{$(ALTSRC)\share\vm\gc_implementation\g1}.cpp.obj::
        $(CPP) $(CPP_FLAGS) $(CPP_USE_PCH) /c $<

{$(ALTSRC)\share\vm\gc_interface}.cpp.obj::
        $(CPP) $(CPP_FLAGS) $(CPP_USE_PCH) /c $<

{$(ALTSRC)\share\vm\asm}.cpp.obj::
        $(CPP) $(CPP_FLAGS) $(CPP_USE_PCH) /c $<

{$(ALTSRC)\share\vm\memory}.cpp.obj::
        $(CPP) $(CPP_FLAGS) $(CPP_USE_PCH) /c $<

{$(ALTSRC)\share\vm\oops}.cpp.obj::
        $(CPP) $(CPP_FLAGS) $(CPP_USE_PCH) /c $<

{$(ALTSRC)\share\vm\prims}.cpp.obj::
        $(CPP) $(CPP_FLAGS) $(CPP_USE_PCH) /c $<

{$(ALTSRC)\share\vm\runtime}.cpp.obj::
        $(CPP) $(CPP_FLAGS) $(CPP_USE_PCH) /c $<

{$(ALTSRC)\share\vm\services}.cpp.obj::
        $(CPP) $(CPP_FLAGS) $(CPP_USE_PCH) /c $<

{$(ALTSRC)\share\vm\utilities}.cpp.obj::
        $(CPP) $(CPP_FLAGS) $(CPP_USE_PCH) /c $<

{$(ALTSRC)\share\vm\libadt}.cpp.obj::
        $(CPP) $(CPP_FLAGS) $(CPP_USE_PCH) /c $<

{$(ALTSRC)\share\vm\opto}.cpp.obj::
        $(CPP) $(CPP_FLAGS) $(CPP_USE_PCH) /c $<

{$(ALTSRC)\os\windows\vm}.cpp.obj::
        $(CPP) $(CPP_FLAGS) $(CPP_USE_PCH) /c $<

# otherwise we can't specify the output filename.
{$(ALTSRC)\os\windows\vm}.rc.res:
        @$(RC) $(RC_FLAGS) /fo"$@" $<

{$(ALTSRC)\cpu\$(Platform_arch)\vm}.cpp.obj::
        $(CPP) $(CPP_FLAGS) $(CPP_USE_PCH) /c $<

{$(ALTSRC)\os_cpu\windows_$(Platform_arch)\vm}.cpp.obj::
        $(CPP) $(CPP_FLAGS) $(CPP_USE_PCH) /c $<

{..\generated\incls}.cpp.obj::
        $(CPP) $(CPP_FLAGS) $(CPP_USE_PCH) /c $<

{..\generated\adfiles}.cpp.obj::
        $(CPP) $(CPP_FLAGS) $(CPP_USE_PCH) /c $<

{..\generated\jvmtifiles}.cpp.obj::
        $(CPP) $(CPP_FLAGS) $(CPP_USE_PCH) /c $<

default::

_build_pch_file.obj:
        @echo #include "precompiled.hpp" > ../generated/_build_pch_file.cpp
        $(CPP) $(CPP_FLAGS) /Fp"vm.pch" /Yc"precompiled.hpp" /c ../generated/_build_pch_file.cpp
