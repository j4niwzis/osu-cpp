# Where each library's sources are in this Flatpak module, read from the list
# the manifest hands flatpak-builder: one declaration per library, made by
# the project.
#
# Not an overlay, which is how this used to be said. An overlay is a port
# file, and a port file is part of what the provider keeps a library under in
# its store -- so an overlay that names this module's directory names a
# different entry in every module. The libraries are built in the ports
# module and used from the client's, whose directories differ, and a
# declaration made by the project is not hashed: where a library's sources
# are is where they are, and what the library is still comes from the
# registry.
#
# skiff and skiff-widgets are left out: the client declares them itself.
file(READ "${CMAKE_CURRENT_LIST_DIR}/cme-sources.json" osu_flatpak_sources)
get_filename_component(osu_flatpak_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
string(JSON osu_flatpak_count LENGTH "${osu_flatpak_sources}")
math(EXPR osu_flatpak_last "${osu_flatpak_count} - 1")
foreach(osu_flatpak_index RANGE ${osu_flatpak_last})
  string(JSON osu_flatpak_dest GET "${osu_flatpak_sources}"
         ${osu_flatpak_index} dest)
  if(osu_flatpak_dest MATCHES "^\\.flatpak-sources/ports/([^/]+)$")
    set(osu_flatpak_port "${CMAKE_MATCH_1}")
    if(NOT osu_flatpak_port STREQUAL "skiff" AND
       NOT osu_flatpak_port STREQUAL "skiff-widgets")
      cme_declare_port(NAME "${osu_flatpak_port}"
                       SOURCE_DIR "${osu_flatpak_root}/${osu_flatpak_dest}")
    endif()
  endif()
endforeach()
