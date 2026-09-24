cmake_minimum_required(VERSION 3.16)

project(plasmaglow)

# Enable automoc for Qt MOC processing
set(CMAKE_AUTOMOC ON)

# Find Extra CMake Modules (ECM)
find_package(ECM 6.0.0 REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

# Include standard KDE and CMake settings
include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(FeatureSummary)

# Copy metadata.json to the build directory so that MOC can find it next to the generated plugin code
configure_file(package/metadata.json metadata.json COPYONLY)

# Find Qt6 and KF6 libraries
find_package(Qt6 REQUIRED COMPONENTS Core DBus Qml Quick)
find_package(KF6 REQUIRED COMPONENTS CoreAddons Config)
find_package(Plasma REQUIRED)

option(PLASMAGLOW_BUILD_KWIN_EFFECT "Build the PlasmaGlow KWin effect" OFF)

if(PLASMAGLOW_BUILD_KWIN_EFFECT)
    find_package(Qt6 REQUIRED COMPONENTS Widgets)
    find_package(KWin REQUIRED)

    kcoreaddons_add_plugin(plasmaglow
        SOURCES
            src/kwineffect/plasmagloweffect.h
            src/kwineffect/plasmagloweffect.cpp
            src/kwineffect/plasmaglow.qrc
        INSTALL_NAMESPACE "kwin/effects/plugins"
    )

    target_compile_features(plasmaglow PRIVATE cxx_std_20)
    target_link_libraries(plasmaglow PRIVATE KWin::kwin)
endif()

# Define the Plasma applet target
plasma_add_applet(org.kde.plasmaglow
    QML_SOURCES
        package/contents/ui/main.qml
        package/contents/ui/CompactRepresentation.qml
        package/contents/ui/FullRepresentation.qml
    RESOURCES
        package/contents/images/icon.svg
    CPP_SOURCES
        src/glowcontroller.h
        src/glowcontroller.cpp
        src/x11backend.h
        src/x11backend.cpp
        src/kwinbackend.h
        src/kwinbackend.cpp
    GENERATE_APPLET_CLASS
)

# Link private libraries for QML registration and Core functions
target_link_libraries(org.kde.plasmaglow
    PRIVATE
        Qt6::Core
        Qt6::DBus
        Qt6::Qml
        Qt6::Quick
        KF6::CoreAddons
        KF6::ConfigCore
        Plasma::Plasma
)

# Add src folder to include directories so headers can be found by generated QML code
target_include_directories(org.kde.plasmaglow PRIVATE src)

# Install the applet package files (metadata, icons, QML)
plasma_install_package(package org.kde.plasmaglow)

# Install icon to system icon theme so Widget Explorer can load it
install(FILES package/contents/images/icon.svg DESTINATION ${KDE_INSTALL_ICONDIR}/hicolor/scalable/apps/ RENAME org.kde.plasmaglow.svg)

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)
