find_package(Sasl2)
set_package_properties(Sasl2 PROPERTIES DESCRIPTION "cyrus-sasl" URL "http://asg.web.cmu.edu/sasl/sasl-library.html" TYPE REQUIRED PURPOSE "Login authentication for Gmail resource")

add_definitions(-D_POSIX_SOURCE)

include_directories(${Sasl2_INCLUDE_DIRS}
                    ${Sasl2_INCLUDE_DIR}/sasl)

set(kdexoauth2sasl_SRCS
    plugin_common.c
    xoauth2plugin.c
    xoauth2plugin_init.c
)

if(Sasl2_FOUND)
  set(kdexoauth2sasl_EXTRA_LIBS ${Sasl2_LIBRARIES})
endif()


add_library(kdexoauth2 SHARED ${kdexoauth2sasl_SRCS} ${kdexoauth2sasl_EXTRA_LIBS})
set_target_properties(kdexoauth2 PROPERTIES SOVERSION 4 VERSION 4.0.0)

install(TARGETS kdexoauth2 DESTINATION ${KDE_INSTALL_LIBDIR}/sasl2)
