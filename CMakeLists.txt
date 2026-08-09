cmake_minimum_required(VERSION 3.28)

project(modern-ime VERSION 0.1.2 LANGUAGES CXX)

include(GNUInstallDirs)
include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(MODERN_IME_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(MODERN_IME_INSTALL_TEST_TOOLS "Install ASR smoke test and latency benchmark programs" ON)
option(MODERN_IME_ENABLE_DEB_PACKAGING "Generate CPack configuration for the Ubuntu Debian package" OFF)

# User units and D-Bus activation files contain the final executable path.
# Configuring them, rather than shipping a path tied to one installation
# method, keeps both ~/.local source installs and /usr Debian packages valid.
set(MODERN_IME_SYSTEMD_USER_UNIT_DIR "lib/systemd/user" CACHE PATH "systemd user unit installation directory")
set(MODERN_IME_GENERATED_DATA_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated-data")
file(MAKE_DIRECTORY "${MODERN_IME_GENERATED_DATA_DIR}/systemd" "${MODERN_IME_GENERATED_DATA_DIR}/dbus")
foreach(unit modern-ime-service modern-ime-ui modern-ime-voice-worker)
    configure_file("data/systemd/${unit}.service.in"
        "${MODERN_IME_GENERATED_DATA_DIR}/systemd/${unit}.service" @ONLY)
endforeach()
foreach(service org.modernime.Service1 org.modernime.UI1)
    configure_file("data/dbus/${service}.service.in"
        "${MODERN_IME_GENERATED_DATA_DIR}/dbus/${service}.service" @ONLY)
endforeach()

find_package(Qt6 6.4 REQUIRED COMPONENTS Core DBus Gui Qml Quick QuickControls2)
find_package(SQLite3 REQUIRED)
find_package(Fcitx5Utils CONFIG REQUIRED)
find_package(Fcitx5Config CONFIG REQUIRED)
find_package(Fcitx5Core CONFIG REQUIRED)
find_package(LibIMECore CONFIG REQUIRED)
find_package(LibIMEPinyin CONFIG REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(PIPEWIRE REQUIRED IMPORTED_TARGET libpipewire-0.3)

function(modern_ime_target_defaults target)
    target_compile_features(${target} PRIVATE cxx_std_20)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    if(MODERN_IME_WARNINGS_AS_ERRORS)
        target_compile_options(${target} PRIVATE -Werror)
    endif()
endfunction()

add_library(modern-ime-contracts
    src/contracts/audio_health.cpp
    src/contracts/contracts.cpp
    src/contracts/xdg.cpp
    src/contracts/logging.cpp
    src/contracts/voice_model.cpp
)
target_include_directories(modern-ime-contracts PUBLIC src)
target_link_libraries(modern-ime-contracts PUBLIC Qt6::Core)
modern_ime_target_defaults(modern-ime-contracts)

add_library(modern-ime-language-core
    src/language-core/language_core.cpp
    src/language-core/lexicon_store.cpp
)
target_include_directories(modern-ime-language-core PUBLIC src)
target_link_libraries(modern-ime-language-core PUBLIC modern-ime-contracts SQLite::SQLite3 LibIME::Pinyin)
modern_ime_target_defaults(modern-ime-language-core)

add_executable(modern-ime-service src/service/main.cpp src/service/service.cpp)
target_link_libraries(modern-ime-service PRIVATE modern-ime-language-core Qt6::Core Qt6::DBus Fcitx5::Utils)
modern_ime_target_defaults(modern-ime-service)

add_executable(modern-ime-voice-worker src/voice-worker/main.cpp src/voice-worker/voice_worker.cpp)
target_sources(modern-ime-voice-worker PRIVATE src/voice-worker/sherpa_api.cpp)
target_link_libraries(modern-ime-voice-worker PRIVATE modern-ime-contracts Qt6::Core Qt6::DBus PkgConfig::PIPEWIRE ${CMAKE_DL_LIBS})
modern_ime_target_defaults(modern-ime-voice-worker)

add_executable(modern-ime-asr-smoke tests/integration/asr_model_smoke.cpp src/voice-worker/sherpa_api.cpp)
target_link_libraries(modern-ime-asr-smoke PRIVATE modern-ime-contracts Qt6::Core ${CMAKE_DL_LIBS})
modern_ime_target_defaults(modern-ime-asr-smoke)

add_executable(modern-ime-latency-benchmark tests/integration/latency_benchmark.cpp)
target_link_libraries(modern-ime-latency-benchmark PRIVATE modern-ime-language-core)
modern_ime_target_defaults(modern-ime-latency-benchmark)

add_executable(modern-ime-candidate-ui
    src/candidate-ui/main.cpp
    src/candidate-ui/candidate_controller.cpp
    src/candidate-ui/candidate_ui.qrc
)
target_link_libraries(modern-ime-candidate-ui PRIVATE modern-ime-contracts Qt6::Core Qt6::DBus Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2)
modern_ime_target_defaults(modern-ime-candidate-ui)

add_executable(modern-ime-settings
    src/settings/main.cpp
    src/settings/settings_controller.cpp
    src/settings/settings.qrc
)
target_link_libraries(modern-ime-settings PRIVATE modern-ime-contracts Qt6::Core Qt6::DBus Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2)
modern_ime_target_defaults(modern-ime-settings)

add_library(modern-ime-engine-addon MODULE
    src/fcitx-engine/engine.cpp
    src/fcitx-engine/engine_factory.cpp
)
set_target_properties(modern-ime-engine-addon PROPERTIES PREFIX "lib" OUTPUT_NAME "modernime")
target_link_libraries(modern-ime-engine-addon PRIVATE modern-ime-language-core Qt6::Core Qt6::DBus Fcitx5::Core Fcitx5::Utils)
modern_ime_target_defaults(modern-ime-engine-addon)

add_library(modern-ime-ui-addon MODULE src/fcitx-ui-bridge/ui_bridge.cpp)
set_target_properties(modern-ime-ui-addon PROPERTIES PREFIX "lib" OUTPUT_NAME "modernimeui")
target_link_libraries(modern-ime-ui-addon PRIVATE modern-ime-contracts Fcitx5::Core)
modern_ime_target_defaults(modern-ime-ui-addon)

install(TARGETS modern-ime-service modern-ime-voice-worker modern-ime-candidate-ui modern-ime-settings
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
if(MODERN_IME_INSTALL_TEST_TOOLS)
    install(TARGETS modern-ime-asr-smoke modern-ime-latency-benchmark RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
install(TARGETS modern-ime-engine-addon modern-ime-ui-addon
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/fcitx5)
install(FILES data/fcitx5/modernime.conf DESTINATION ${CMAKE_INSTALL_DATADIR}/fcitx5/addon)
install(FILES data/fcitx5/modernimeui.conf DESTINATION ${CMAKE_INSTALL_DATADIR}/fcitx5/addon)
install(FILES data/fcitx5/modernime-inputmethod.conf DESTINATION ${CMAKE_INSTALL_DATADIR}/fcitx5/inputmethod RENAME modernime.conf)
install(FILES data/dbus/org.modernime.Service1.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/modern-ime/dbus)
install(FILES
    "${MODERN_IME_GENERATED_DATA_DIR}/dbus/org.modernime.Service1.service"
    "${MODERN_IME_GENERATED_DATA_DIR}/dbus/org.modernime.UI1.service"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/dbus-1/services)
install(FILES data/modern-ime-settings.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(PROGRAMS tools/install-voice-model.sh DESTINATION ${CMAKE_INSTALL_DATADIR}/modern-ime)
install(FILES README.md DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/modern-ime)
install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/modern-ime RENAME copyright)
install(FILES
    "${MODERN_IME_GENERATED_DATA_DIR}/systemd/modern-ime-service.service"
    "${MODERN_IME_GENERATED_DATA_DIR}/systemd/modern-ime-ui.service"
    "${MODERN_IME_GENERATED_DATA_DIR}/systemd/modern-ime-voice-worker.service"
    DESTINATION ${MODERN_IME_SYSTEMD_USER_UNIT_DIR})

if(BUILD_TESTING)
    find_package(Catch2 3 REQUIRED)
    add_executable(modern-ime-tests
        tests/unit/contracts_test.cpp
        tests/unit/candidate_list_test.cpp
        tests/unit/language_core_test.cpp
        tests/unit/lexicon_store_test.cpp
    )
    target_link_libraries(modern-ime-tests PRIVATE modern-ime-language-core Fcitx5::Core Catch2::Catch2WithMain)
    modern_ime_target_defaults(modern-ime-tests)
    add_test(NAME modern-ime-unit COMMAND modern-ime-tests)

    add_executable(modern-ime-fcitx-client-smoke tests/integration/fcitx_client_smoke.cpp)
    target_link_libraries(modern-ime-fcitx-client-smoke PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick)
    modern_ime_target_defaults(modern-ime-fcitx-client-smoke)

    find_program(DBUS_RUN_SESSION_EXECUTABLE dbus-run-session REQUIRED)
    set(MODERN_IME_ISOLATED_DBUS_ARGS --config-file=${CMAKE_SOURCE_DIR}/tests/fixtures/dbus-session-isolated.conf --)
    add_test(NAME modern-ime-candidate-ui-smoke
        COMMAND ${DBUS_RUN_SESSION_EXECUTABLE} ${MODERN_IME_ISOLATED_DBUS_ARGS} $<TARGET_FILE:modern-ime-candidate-ui> --smoke-test)
    set_tests_properties(modern-ime-candidate-ui-smoke PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_SCALE_FACTOR=1.5"
        TIMEOUT 10)
    add_test(NAME modern-ime-candidate-ui-voice-error-smoke
        COMMAND ${DBUS_RUN_SESSION_EXECUTABLE} ${MODERN_IME_ISOLATED_DBUS_ARGS} $<TARGET_FILE:modern-ime-candidate-ui> --smoke-test-voice-error)
    set_tests_properties(modern-ime-candidate-ui-voice-error-smoke PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        TIMEOUT 10)
    add_test(NAME modern-ime-settings-smoke
        COMMAND ${DBUS_RUN_SESSION_EXECUTABLE} ${MODERN_IME_ISOLATED_DBUS_ARGS} $<TARGET_FILE:modern-ime-settings> --smoke-test)
    set_tests_properties(modern-ime-settings-smoke PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        TIMEOUT 10)
    add_test(NAME modern-ime-model-installer-smoke
        COMMAND ${DBUS_RUN_SESSION_EXECUTABLE} ${MODERN_IME_ISOLATED_DBUS_ARGS} $<TARGET_FILE:modern-ime-settings> --smoke-test-model-installer)
    set_tests_properties(modern-ime-model-installer-smoke PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_DATA_DIRS=${CMAKE_SOURCE_DIR}/tests/fixtures/model-installer:/usr/local/share:/usr/share"
        TIMEOUT 10)
    find_program(XVFB_RUN_EXECUTABLE xvfb-run)
    if(XVFB_RUN_EXECUTABLE)
        add_test(NAME modern-ime-candidate-ui-x11-smoke
            COMMAND ${XVFB_RUN_EXECUTABLE} -a ${DBUS_RUN_SESSION_EXECUTABLE} ${MODERN_IME_ISOLATED_DBUS_ARGS}
                    $<TARGET_FILE:modern-ime-candidate-ui> --smoke-test)
        set_tests_properties(modern-ime-candidate-ui-x11-smoke PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=xcb"
            TIMEOUT 10)
        find_program(XDOTOOL_EXECUTABLE xdotool)
        if(XDOTOOL_EXECUTABLE)
            add_test(NAME modern-ime-fcitx-composition-smoke
                COMMAND ${XVFB_RUN_EXECUTABLE} -a ${DBUS_RUN_SESSION_EXECUTABLE} --
                        bash ${CMAKE_SOURCE_DIR}/tests/integration/run_fcitx_composition_smoke.sh
                        ${CMAKE_SOURCE_DIR}
                        $<TARGET_FILE:modern-ime-engine-addon>
                        $<TARGET_FILE:modern-ime-fcitx-client-smoke>
                        $<TARGET_FILE:modern-ime-candidate-ui>
                        $<TARGET_FILE:modern-ime-service>)
            set_tests_properties(modern-ime-fcitx-composition-smoke PROPERTIES TIMEOUT 20)
        endif()
    endif()
    add_test(NAME modern-ime-keyboard-latency COMMAND modern-ime-latency-benchmark)
endif()

add_custom_target(format
    COMMAND clang-format -i $<TARGET_PROPERTY:modern-ime-contracts,SOURCES> $<TARGET_PROPERTY:modern-ime-language-core,SOURCES>
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Formatting core C++ sources")

if(MODERN_IME_ENABLE_DEB_PACKAGING)
    if(NOT CMAKE_INSTALL_PREFIX STREQUAL "/usr")
        message(FATAL_ERROR "Debian packages must be configured with -DCMAKE_INSTALL_PREFIX=/usr")
    endif()
    set(CPACK_GENERATOR "DEB")
    set(CPACK_PACKAGE_NAME "modern-ime")
    set(CPACK_PACKAGE_VENDOR "Modern IME contributors")
    set(CPACK_PACKAGE_CONTACT "Modern IME contributors")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Local Fcitx5 Chinese, English, and offline voice input method")
    set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
    set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
    set(CPACK_INSTALL_DEFAULT_DIRECTORY_PERMISSIONS
        OWNER_READ OWNER_WRITE OWNER_EXECUTE
        GROUP_READ GROUP_EXECUTE
        WORLD_READ WORLD_EXECUTE)
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Modern IME contributors")
    set(CPACK_DEBIAN_PACKAGE_SECTION "x11")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    set(CPACK_DEBIAN_PACKAGE_DEPENDS
        "fcitx5 (>= 5.1.7), qml6-module-qtquick-controls, qml6-module-qtquick-layouts, qml6-module-qtquick-templates, qml6-module-qtquick-window, qml6-module-qtqml-workerscript, curl, bzip2, ca-certificates")
    set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "fcitx5-config-qt")
    set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
        "${CMAKE_CURRENT_SOURCE_DIR}/packaging/debian/postinst"
        "${CMAKE_CURRENT_SOURCE_DIR}/packaging/debian/prerm")
    include(CPack)
endif()
