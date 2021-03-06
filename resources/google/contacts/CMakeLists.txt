
add_definitions(-DTRANSLATION_DOMAIN=\"akonadi_googlecontacts_resource\")

set(contactsresource_SRCS
  contactsresource.cpp
  settings.cpp
  settingsdialog.cpp
  ../common/googlesettings.cpp
  ../common/googleresource.cpp
  ../common/googleaccountmanager.cpp
  ../common/googlesettingsdialog.cpp
  ${accounts_SRCS}
)

kconfig_add_kcfg_files(contactsresource_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/settingsbase.kcfgc)

kcfg_generate_dbus_interface(
  ${CMAKE_CURRENT_SOURCE_DIR}/settingsbase.kcfg
  org.kde.Akonadi.GoogleContacts.Settings
)

qt5_add_dbus_adaptor(contactsresource_SRCS
  ${CMAKE_CURRENT_BINARY_DIR}/org.kde.Akonadi.GoogleContacts.Settings.xml
  ${CMAKE_CURRENT_SOURCE_DIR}/settings.h Settings
)

add_executable(akonadi_googlecontacts_resource ${contactsresource_SRCS})

if( APPLE )
  set_target_properties(akonadi_googlecontacts_resource PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/../../Info.plist.template
  )
  set_target_properties(akonadi_googlecontacts_resource PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.kde.Akonadi.googlecontacts"
  )
  set_target_properties(akonadi_googlecontacts_resource PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME "KDE Akonadi Google Contacts Resource"
  )
endif()


target_link_libraries(akonadi_googlecontacts_resource
  KF5::AkonadiCore
  KF5::Contacts
  KPim::GAPICore
  KPim::GAPIContacts
  KF5::AkonadiAgentBase
  KF5::Wallet
  KF5::I18n
  KF5::WindowSystem
  KF5::Completion
  KF5::WidgetsAddons
  KF5::TextWidgets
  Qt5::DBus
)

install(TARGETS akonadi_googlecontacts_resource ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})

install(
  FILES googlecontactsresource.desktop
  DESTINATION "${KDE_INSTALL_DATAROOTDIR}/akonadi/agents"
)
