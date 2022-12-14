cmake_minimum_required(VERSION 3.14)

set(MYNAME HighlightPickedActor)

project(${MYNAME} VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Quick)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Quick)

set(PROJECT_SOURCES
        main.cpp
        QQuickVtkItem.cpp
        MyVtkItem.cpp
        qml.qrc
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(${MYNAME}
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
    )
# Define target properties for Android with Qt 6 as:
#    set_property(TARGET ${MYNAME} APPEND PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR
#                 ${CMAKE_CURRENT_SOURCE_DIR}/android)
# For more information, see https://doc.qt.io/qt-6/qt-add-executable.html#target-creation
else()
    if(ANDROID)
        add_library(${MYNAME} SHARED
            ${PROJECT_SOURCES}
        )
# Define properties for Android with Qt 5 after find_package() calls as:
#    set(ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android")
    else()
        add_executable(${MYNAME}
          ${PROJECT_SOURCES}
        )
    endif()
endif()

target_link_libraries(${MYNAME}
  PRIVATE Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Quick)

set_target_properties(${MYNAME} PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER my.example.com
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

install(TARGETS ${MYNAME}
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})

if(QT_VERSION_MAJOR EQUAL 6)
    qt_import_qml_plugins(${MYNAME})
    qt_finalize_executable(${MYNAME})
endif()

set(QT_DIR ${Qt${QT_VERSION_MAJOR}_DIR})
get_filename_component(QT_TOP "../../.."
  REALPATH BASE_DIR "${QT_DIR}" CACHE
)

find_package(VTK)

if (NOT VTK_FOUND OR VTK_VERSION VERSION_LESS "9.0.0")
  message("Skipping ${MYNAME}: ${VTK_NOT_FOUND_MESSAGE}")
  return ()
endif()

target_link_libraries(${MYNAME} PRIVATE ${VTK_LIBRARIES})
vtk_module_autoinit( TARGETS ${MYNAME} MODULES ${VTK_LIBRARIES} )

if(WIN32)
    set_target_properties(${MYNAME} PROPERTIES
      VS_DEBUGGER_ENVIRONMENT "PATH=%PATH%;${VTK_DIR}/Bin/Debug;${QT_TOP}/bin;${QT_TOP}/plugins/platforms"
    )
    set_target_properties(${MYNAME} PROPERTIES
      LINK_FLAGS_DEBUG "/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup"
      LINK_FLAGS_RELEASE "/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup"
    )
    set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY
      VS_STARTUP_PROJECT ${MYNAME}
    )
endif()

message (STATUS "VTK_VERSION: ${VTK_VERSION}")
message (STATUS "VTK_DIR: ${VTK_DIR}")
message (STATUS "QT_VERSION: Qt${QT_VERSION_MAJOR}")
message (STATUS "QT_DIR: ${QT_DIR}")
message (STATUS "QT_TOP: ${QT_TOP}")
