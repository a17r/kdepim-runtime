add_definitions(-DTRANSLATION_DOMAIN=\"akonadi_imap_resource\")

if (Sasl2_FOUND AND KPimGAPI_FOUND)
    set(WITH_GMAIL_XOAUTH2 TRUE)
    add_subdirectory(saslplugin)
endif()
configure_file(config.h.cmake ${CMAKE_CURRENT_BINARY_DIR}/config.h)

########### next target ###############

set( imapresource_LIB_SRCS
  addcollectiontask.cpp
  additemtask.cpp
  batchfetcher.cpp
  changecollectiontask.cpp
  changeitemsflagstask.cpp
  changeitemtask.cpp
  expungecollectiontask.cpp
  highestmodseqattribute.cpp
  imapaccount.cpp
  imapflags.cpp
  imapresourcebase.cpp
  messagehelper.cpp
  movecollectiontask.cpp
  moveitemstask.cpp
  noselectattribute.cpp
  noinferiorsattribute.cpp
  passwordrequesterinterface.cpp
  removecollectionrecursivetask.cpp
  resourcestateinterface.cpp
  resourcetask.cpp
  retrievecollectionmetadatatask.cpp
  retrievecollectionstask.cpp
  retrieveitemtask.cpp
  retrieveitemstask.cpp
  searchtask.cpp
  sessionpool.cpp
  uidvalidityattribute.cpp
  uidnextattribute.cpp
  settings.cpp
  subscriptiondialog.cpp
  imapidlemanager.cpp
  resourcestate.cpp
  collectionmetadatahelper.cpp
  replacemessagejob.cpp
  tracer.cpp
  utils.cpp
  ${AKONADI_COLLECTIONATTRIBUTES_SHARED_SOURCES}
  ${AKONADI_IMAPATTRIBUTES_SHARED_SOURCES}
)


ecm_qt_declare_logging_category(imapresource_LIB_SRCS HEADER imapresource_debug.h IDENTIFIER IMAPRESOURCE_LOG CATEGORY_NAME org.kde.pim.imapresource)
kcfg_generate_dbus_interface( ${CMAKE_CURRENT_SOURCE_DIR}/imapresource.kcfg org.kde.Akonadi.Imap.Settings )
kconfig_add_kcfg_files(imapresource_LIB_SRCS settingsbase.kcfgc)

qt5_add_dbus_adaptor( imapresource_LIB_SRCS
     ${CMAKE_CURRENT_BINARY_DIR}/org.kde.Akonadi.Imap.Settings.xml settings.h Settings
     )

qt5_generate_dbus_interface( ${CMAKE_CURRENT_SOURCE_DIR}/imapresourcebase.h org.kde.Akonadi.Imap.Resource.xml OPTIONS -a )
qt5_add_dbus_adaptor( imapresource_LIB_SRCS
     ${CMAKE_CURRENT_BINARY_DIR}/org.kde.Akonadi.Imap.Resource.xml
        imapresourcebase.h ImapResourceBase
     )

add_library(imapresource STATIC ${imapresource_LIB_SRCS})

target_link_libraries(imapresource
    Qt5::DBus
    Qt5::Network
    KF5::AkonadiCore
    KF5::IMAP
    KF5::MailTransport
    KF5::KIOWidgets
    KF5::Mime
    KF5::AkonadiMime
    KF5::IdentityManagement
    KF5::AkonadiAgentBase
    KF5::I18n
    KF5::WindowSystem
    akonadi-singlefileresource
)

########### next target ###############

set( akonadi_imap_resource_SRCS
  main.cpp
  imapresource.cpp
  resourcestate.cpp
  settingspasswordrequester.cpp
  setupserver.cpp
  serverinfodialog.cpp
)

if (WITH_GMAIL_XOAUTH2)
  set( akonadi_imap_resource_SRCS
    ${akonadi_imap_resource_SRCS}
    passwordrequester.cpp
    gmailpasswordrequester.cpp
  )
endif ()

install( FILES imapresource.desktop DESTINATION "${KDE_INSTALL_DATAROOTDIR}/akonadi/agents" )

ki18n_wrap_ui(akonadi_imap_resource_SRCS setupserverview_desktop.ui)


add_executable(akonadi_imap_resource ${akonadi_imap_resource_SRCS})

if( APPLE )
  set_target_properties(akonadi_imap_resource PROPERTIES MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/../Info.plist.template)
  set_target_properties(akonadi_imap_resource PROPERTIES MACOSX_BUNDLE_GUI_IDENTIFIER "org.kde.Akonadi.Imap")
  set_target_properties(akonadi_imap_resource PROPERTIES MACOSX_BUNDLE_BUNDLE_NAME "KDE Akonadi IMAP Resource")
endif ()

target_link_libraries(akonadi_imap_resource
    Qt5::DBus
    KF5::AkonadiCore
    KF5::IMAP
    akonadi-singlefileresource
    KF5::AkonadiWidgets
    KF5::MailTransport
    KF5::Mime
    KF5::AkonadiMime
    KF5::IdentityManagement
    KF5::I18n
    imapresource
    KF5::TextWidgets
    folderarchivesettings)

if (WITH_GMAIL_XOAUTH2)
    target_link_libraries(akonadi_imap_resource KPim::GAPICore)
endif ()

install(TARGETS akonadi_imap_resource ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})

add_subdirectory( wizard )

if(BUILD_TESTING)
    add_subdirectory( autotests )
    add_subdirectory( tests )
endif()
