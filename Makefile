#
# elisso (C) 2016--2017 Baubadil GmbH.
#
# elisso is free software; you can redistribute it and/or modify it under the terms of the GNU
# General Public License as published by the Free Software Foundation, in version 2 as it comes
# in the "LICENSE" file of the elisso main distribution. This program is distributed in the hope
# that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the LICENSE file for more details.
#
# This makefile is for "kmk", the make utility of kBuild (http://trac.netlabs.org/kbuild). GNU make
# probably won't like it.
#
# See README.md for instructions.

SUB_DEPTH = .
include $(KBUILD_PATH)/subheader.kmk

#
# Target lists.
#
include $(PATH_CURRENT)/src/xwp/Makefile.kmk

PROGRAMS += elisso
elisso_TEMPLATE = EXE
elisso_SOURCES =
elisso_LIBS = $(PATH_STAGE_LIB)/xwp.a $(GTKMM_LIBS) libpcre

include $(PATH_CURRENT)/src/elisso/Makefile.kmk

include $(FILE_KBUILD_SUB_FOOTER)
