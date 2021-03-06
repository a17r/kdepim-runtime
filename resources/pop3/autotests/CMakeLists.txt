set( EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_BINARY_DIR} )

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Stolen from kdepimlibs/akonadi/tests
kde_enable_exceptions()

macro(add_akonadi_isolated_test _source)
  get_filename_component(_targetName ${_source} NAME_WE)
  set(_srcList ${_source} fakeserver/fakeserver.cpp)

  kcfg_generate_dbus_interface(${CMAKE_CURRENT_SOURCE_DIR}/../settings.kcfg org.kde.Akonadi.POP3.Settings)
  set(pop3settingsinterface_xml ${CMAKE_CURRENT_BINARY_DIR}/org.kde.Akonadi.POP3.Settings.xml)
  kcfg_generate_dbus_interface(${CMAKE_CURRENT_SOURCE_DIR}/../../maildir/maildirresource.kcfg org.kde.Akonadi.Maildir.Settings)
  set(maildirsettingsinterface_xml ${CMAKE_CURRENT_BINARY_DIR}/org.kde.Akonadi.Maildir.Settings.xml)
  set_source_files_properties(${pop3settingsinterface_xml} PROPERTIES INCLUDE "../metatype.h")
  qt5_add_dbus_interface(_srcList ${pop3settingsinterface_xml} pop3settings)
  qt5_add_dbus_interface(_srcList ${maildirsettingsinterface_xml} maildirsettings)
  # add the dbus interace to every test (easier than adding to particular tests only)
  #qt5_add_dbus_interface(_srcList ../org.kde.krss.xml krssinterface)

  add_executable(${_targetName} ${_srcList})
  target_link_libraries(${_targetName}
    Qt5::Test
    KF5::AkonadiCore
    KF5::Mime
    Qt5::Network
    Qt5::Widgets
    Qt5::DBus
  )

  # based on kde4_add_unit_test
  if (WIN32)
    get_target_property( _loc ${_targetName} LOCATION )
    set(_executable ${_loc}.bat)
  else ()
    set(_executable ${EXECUTABLE_OUTPUT_PATH}/${_targetName})
  endif ()
  if (UNIX)
    set(_executable ${_executable}.shell)
  endif ()

  find_program(_testrunner akonaditest)

  if (KDEPIM_RUN_ISOLATED_TESTS)
    add_test( ${_targetName} ${_testrunner} -c ${CMAKE_CURRENT_SOURCE_DIR}/unittestenv/config.xml ${_executable} )
  endif ()
endmacro(add_akonadi_isolated_test)

add_akonadi_isolated_test(pop3test.cpp)
