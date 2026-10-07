cmake_minimum_required(VERSION 3.19)

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
find_package(Qt6 REQUIRED COMPONENTS Core DBus Qml Quick Widgets)
find_package(KF6 REQUIRED COMPONENTS CoreAddons Config XmlGui)
find_package(Plasma REQUIRED)

add_executable(plasmaglow-login-snapshot src/loginsettingssnapshot.cpp)
target_link_libraries(plasmaglow-login-snapshot PRIVATE Qt6::Core KF6::ConfigCore)
install(TARGETS plasmaglow-login-snapshot DESTINATION ${KDE_INSTALL_LIBEXECDIR})

option(PLASMAGLOW_BUILD_KWIN_EFFECT "Build the PlasmaGlow KWin effect" OFF)

if(PLASMAGLOW_BUILD_KWIN_EFFECT)
    find_package(KWin REQUIRED)

    kcoreaddons_add_plugin(plasmaglow
        SOURCES
            src/kwineffect/plasmagloweffect.h
            src/kwineffect/plasmagloweffect.cpp
            src/kwineffect/computesharpening.h
            src/kwineffect/computesharpening.cpp
            src/kwineffect/screenpass.h
            src/kwineffect/screenpass.cpp
            src/kwineffect/plasmaglow.qrc
        INSTALL_NAMESPACE "kwin/effects/plugins"
    )

    target_compile_features(plasmaglow PRIVATE cxx_std_20)
    target_link_libraries(plasmaglow PRIVATE KWin::kwin KF6::ConfigCore)
endif()

# Define the Plasma applet target
qt_add_resources(about_resources src/about.qrc)

plasma_add_applet(org.kde.plasmaglow
    QML_SOURCES
        package/contents/ui/main.qml
        package/contents/ui/CompactRepresentation.qml
        package/contents/ui/FullRepresentation.qml
    RESOURCES
        package/contents/images/icon.svg
        package/contents/images/icon-light.svg
        package/contents/images/mode-off.svg
        package/contents/images/mode-cas.svg
        package/contents/images/mode-luma.svg
        package/contents/images/mode-rcas.svg
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
target_sources(org.kde.plasmaglow PRIVATE ${about_resources})

file(READ package/metadata.json applet_metadata)
string(JSON applet_version GET "${applet_metadata}" KPlugin Version)
target_compile_definitions(org.kde.plasmaglow PRIVATE PLASMAGLOW_VERSION="${applet_version}")
target_link_libraries(org.kde.plasmaglow PRIVATE Qt6::Widgets KF6::XmlGui)

# Install the applet package files (metadata, icons, QML)
plasma_install_package(package org.kde.plasmaglow)

# Install icon to system icon theme so Widget Explorer can load it
install(FILES package/contents/images/icon.svg DESTINATION ${KDE_INSTALL_ICONDIR}/hicolor/scalable/apps/ RENAME org.kde.plasmaglow.svg)

install(FILES THIRD_PARTY_NOTICES.md DESTINATION ${KDE_INSTALL_DATADIR}/plasmaglow)

option(PLASMAGLOW_BUILD_TESTS "Build isolated controller regression tests" OFF)
if(PLASMAGLOW_BUILD_TESTS)
    enable_testing()
    find_program(DBUS_RUN_SESSION dbus-run-session REQUIRED)
    add_executable(plasmaglow-adjustments-test
        tests/adjustments.cpp
        ${about_resources}
        src/glowcontroller.cpp src/glowcontroller.h
        src/kwinbackend.cpp src/kwinbackend.h
        src/x11backend.cpp src/x11backend.h)
    target_include_directories(plasmaglow-adjustments-test PRIVATE src)
    target_compile_definitions(plasmaglow-adjustments-test PRIVATE PLASMAGLOW_VERSION="${applet_version}")
    target_link_libraries(plasmaglow-adjustments-test PRIVATE
        Qt6::Widgets Qt6::DBus Qt6::Qml KF6::CoreAddons KF6::ConfigCore KF6::XmlGui)
    add_test(NAME adjustments COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:plasmaglow-adjustments-test>)
    set_tests_properties(adjustments PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)
    if(PLASMAGLOW_BUILD_KWIN_EFFECT)
        add_executable(plasmaglow-sharpening-gpu-test
            tests/sharpening-gpu.cpp
            src/kwineffect/computesharpening.cpp
            src/kwineffect/screenpass.cpp
            src/kwineffect/plasmaglow.qrc)
        target_include_directories(plasmaglow-sharpening-gpu-test PRIVATE src/kwineffect)
        target_compile_features(plasmaglow-sharpening-gpu-test PRIVATE cxx_std_20)
        target_link_libraries(plasmaglow-sharpening-gpu-test PRIVATE KWin::kwin)
        add_test(NAME sharpening-gpu COMMAND plasmaglow-sharpening-gpu-test)
        set_tests_properties(sharpening-gpu PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_FORCE_STDERR_LOGGING=1" SKIP_RETURN_CODE 77 TIMEOUT 60)
    endif()
endif()

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)
