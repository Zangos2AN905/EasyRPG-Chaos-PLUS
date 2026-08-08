#.rst:
# FindOpus
# --------
#
# Find the Opus codec library
#
# Imported Targets
# ^^^^^^^^^^^^^^^^
#
# This module defines the following :prop_tgt:`IMPORTED` targets:
#
# ``Opus::opus``
#   The ``Opus`` library, if found.
#
# Result Variables
# ^^^^^^^^^^^^^^^^
#
# This module will set the following variables in your project:
#
# ``OPUS_INCLUDE_DIRS``
#   where to find opus headers.
# ``OPUS_LIBRARIES``
#   the libraries to link against to use Opus.
# ``Opus_FOUND``
#   true if the Opus headers and libraries were found.

find_package(PkgConfig QUIET)

pkg_check_modules(PC_OPUS QUIET opus)

# Look for the header file.
find_path(OPUS_INCLUDE_DIR
	NAMES opus.h
	PATH_SUFFIXES opus
	HINTS ${PC_OPUS_INCLUDE_DIRS})

# Look for the library.
if(NOT OPUS_LIBRARY)
	find_library(OPUS_LIBRARY
		NAMES libopus opus
		HINTS ${PC_OPUS_LIBRARY_DIRS})
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Opus
	REQUIRED_VARS OPUS_LIBRARY OPUS_INCLUDE_DIR)

if(Opus_FOUND)
	set(OPUS_INCLUDE_DIRS ${OPUS_INCLUDE_DIR})

	if(NOT OPUS_LIBRARIES)
		set(OPUS_LIBRARIES ${OPUS_LIBRARY})
	endif()

	if(NOT TARGET Opus::opus)
		add_library(Opus::opus UNKNOWN IMPORTED)
		set_target_properties(Opus::opus PROPERTIES
			INTERFACE_INCLUDE_DIRECTORIES "${OPUS_INCLUDE_DIRS}"
			IMPORTED_LOCATION "${OPUS_LIBRARY}")
	endif()
endif()

mark_as_advanced(OPUS_INCLUDE_DIR OPUS_LIBRARY)
