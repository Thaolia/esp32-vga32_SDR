cmake_minimum_required(VERSION 3.16)
# FabGL est consommé directement depuis le submodule (composant "FabGL")
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/third_party/FabGL")
set(ENV{IDF_COMPONENT_MANAGER} "0")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# Marge de heap laissée par FabGL après le framebuffer : 40000 par défaut, trop pour la RAM
# restante une fois la radio initialisée (cf. patch 0002 et docs/ARCHITECTURE.md)
idf_build_set_property(COMPILE_DEFINITIONS "FABGLIB_MINFREELARGESTBLOCK=8192" APPEND)
project(vga32_sdr)
