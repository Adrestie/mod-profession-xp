#
# This file is part of mod-profession-xp, GPL-2.0-or-later.
#
# Included by modules/CMakeLists.txt, inline, once the `modules` target exists
# (the core includes modules/<name>/<name>.cmake when it is there).
#
# OPTIONAL DEPENDENCY ON mod-spheregrid: at the maximum level the module awards
# Spherite through SphereGridPlayerMgr::Earn. That call only compiles and links
# when mod-spheregrid sits in the same sources and both modules are built into
# the same static `modules` library. Otherwise nothing is defined, and the
# module builds and runs on its own, with no reward at the maximum level.
#

ModuleNameToVariable("mod-profession-xp" PROFESSION_XP_LINKAGE_VAR)
ModuleNameToVariable("mod-spheregrid" PROFESSION_XP_SPHEREGRID_LINKAGE_VAR)

if(EXISTS "${CMAKE_SOURCE_DIR}/modules/mod-spheregrid/src/core/SphereGridPlayerMgr.h"
   AND "${${PROFESSION_XP_LINKAGE_VAR}}" STREQUAL "static"
   AND "${${PROFESSION_XP_SPHEREGRID_LINKAGE_VAR}}" STREQUAL "static")
  target_compile_definitions(modules PRIVATE MOD_PROFESSION_XP_SPHEREGRID)
  message(STATUS "mod-profession-xp: mod-spheregrid found, Spherite at the maximum level")
else()
  message(STATUS "mod-profession-xp: built without mod-spheregrid")
endif()
