include(ECMAddTests)

find_package(Qt5Test CONFIG REQUIRED)

set( EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_BINARY_DIR} )

include_directories( ${CMAKE_CURRENT_SOURCE_DIR}/..   )

########### next target ###############

set(testmaildir_SRCS testmaildir.cpp )


add_executable( testmaildir ${testmaildir_SRCS} )
add_test( testmaildir testmaildir )
ecm_mark_as_test(maildir-testmaildir)

target_link_libraries(testmaildir  Qt5::Test  KF5::AkonadiMime maildir)

