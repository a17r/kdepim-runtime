add_definitions(-D_POSIX_SOURCE)

if (WIN32)
    add_definitions(-DWIN32)
elseif (APPLE)
    add_definitions(-Dmacintosh)
endif()

include_directories(${Sasl2_INCLUDE_DIRS}
                    ${Sasl2_INCLUDE_DIRS}/sasl)

set(kdexoauth2sasl_SRCS
    xoauth2plugin.c
    xoauth2plugin_init.c
    plugin_common.c
)

add_library(kdexoauth2 SHARED ${kdexoauth2sasl_SRCS})
set_target_properties(kdexoauth2 PROPERTIES
    C_VISIBILITY_PRESET default
    SOVERSION 3
    VERSION 3.0.0)

install(TARGETS kdexoauth2 DESTINATION ${KDE_INSTALL_LIBDIR}/sasl2)
