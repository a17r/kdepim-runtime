include(ECMAddTests)

find_package(Qt5Test CONFIG REQUIRED)

if(${EXECUTABLE_OUTPUT_PATH})
    set( PREVIOUS_EXEC_OUTPUT_PATH ${EXECUTABLE_OUTPUT_PATH} )
else()
    set( PREVIOUS_EXEC_OUTPUT_PATH . )
endif()
set( EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_BINARY_DIR} )



include_directories(
  ${CMAKE_CURRENT_SOURCE_DIR}/../
  ${CMAKE_CURRENT_BINARY_DIR}/../
  ${CMAKE_CURRENT_SOURCE_DIR}
)

add_executable(abstractlocalstoretest abstractlocalstoretest.cpp)
add_test(abstractlocalstoretest abstractlocalstoretest)
ecm_mark_as_test(abstractlocalstoretest)

target_link_libraries(
  abstractlocalstoretest
  akonadi-filestore
  KF5::AkonadiCore
  Qt5::Test
)
