# Copies files into the iPadOS app bundle (run as a POST_BUILD step).
#
#   cmake -DBUNDLE_DIR=<app bundle> -DMANIFEST=<file> -P CopyIntoIosBundle.cmake
#
# MANIFEST holds one "<source>\t<path inside the bundle>" entry per line.
# Xcode exports TARGET_BUILD_DIR/WRAPPER_NAME to build phases. During Product >
# Archive they point at the installation directory instead of the CMake build
# directory that $<TARGET_BUNDLE_DIR> names, so they win when present.

if(DEFINED ENV{TARGET_BUILD_DIR} AND NOT "$ENV{WRAPPER_NAME}" STREQUAL "")
    set(BUNDLE_DIR "$ENV{TARGET_BUILD_DIR}/$ENV{WRAPPER_NAME}")
endif()

if("${BUNDLE_DIR}" STREQUAL "" OR NOT EXISTS "${MANIFEST}")
    message(FATAL_ERROR "CopyIntoIosBundle: BUNDLE_DIR and MANIFEST are required")
endif()

file(STRINGS "${MANIFEST}" entries ENCODING UTF-8)
foreach(entry IN LISTS entries)
    string(FIND "${entry}" "\t" tab)
    if(tab LESS 0)
        continue()
    endif()
    string(SUBSTRING "${entry}" 0 ${tab} source)
    math(EXPR start "${tab} + 1")
    string(SUBSTRING "${entry}" ${start} -1 destination)

    if(NOT EXISTS "${source}")
        message(FATAL_ERROR "CopyIntoIosBundle: missing ${source}")
    endif()
    set(target "${BUNDLE_DIR}/${destination}")
    get_filename_component(target_dir "${target}" DIRECTORY)
    file(MAKE_DIRECTORY "${target_dir}")
    file(COPY_FILE "${source}" "${target}" ONLY_IF_DIFFERENT)
endforeach()

message(STATUS "Bundle resources copied into ${BUNDLE_DIR}")
