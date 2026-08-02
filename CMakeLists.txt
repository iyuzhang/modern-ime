cmake_minimum_required(VERSION 3.28)

project(modern-ime VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(MODERN_IME_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

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
target_link_libraries(modern-ime-engine-addon PRIVATE modern-ime-language-core Qt6::Core Qt6::DBus Fcitx5::Core)
modern_ime_target_defaults(modern-ime-engine-addon)

add_library(modern-ime-ui-addon MODULE src/fcitx-ui-bridge/ui_bridge.cpp)
set_target_properties(modern-ime-ui-addon PROPERTIES PREFIX "lib" OUTPUT_NAME "modernimeui")
target_link_libraries(modern-ime-ui-addon PRIVATE modern-ime-contracts Fcitx5::Core)
modern_ime_target_defaults(modern-ime-ui-addon)

install(TARGETS modern-ime-service modern-ime-voice-worker modern-ime-candidate-ui modern-ime-settings modern-ime-asr-smoke modern-ime-latency-benchmark
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS modern-ime-engine-addon modern-ime-ui-addon
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/fcitx5)
install(FILES data/fcitx5/modernime.conf DESTINATION ${CMAKE_INSTALL_DATADIR}/fcitx5/addon)
install(FILES data/fcitx5/modernimeui.conf DESTINATION ${CMAKE_INSTALL_DATADIR}/fcitx5/addon)
install(FILES data/fcitx5/modernime-inputmethod.conf DESTINATION ${CMAKE_INSTALL_DATADIR}/fcitx5/inputmethod RENAME modernime.conf)
install(FILES data/dbus/org.modernime.Service1.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/modern-ime/dbus)
install(FILES data/modern-ime-settings.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(PROGRAMS tools/install-voice-model.sh DESTINATION ${CMAKE_INSTALL_DATADIR}/modern-ime)
install(FILES data/systemd/modern-ime-service.service data/systemd/modern-ime-ui.service data/systemd/modern-ime-voice-worker.service
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/systemd/user)

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
    add_test(NAME modern-ime-candidate-ui-smoke
        COMMAND ${DBUS_RUN_SESSION_EXECUTABLE} -- $<TARGET_FILE:modern-ime-candidate-ui> --smoke-test)
    set_tests_properties(modern-ime-candidate-ui-smoke PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_SCALE_FACTOR=1.5"
        TIMEOUT 10)
    find_program(XVFB_RUN_EXECUTABLE xvfb-run)
    if(XVFB_RUN_EXECUTABLE)
        add_test(NAME modern-ime-candidate-ui-x11-smoke
            COMMAND ${XVFB_RUN_EXECUTABLE} -a ${DBUS_RUN_SESSION_EXECUTABLE} --
                    $<TARGET_FILE:modern-ime-candidate-ui> --smoke-test)
        set_tests_properties(modern-ime-candidate-ui-x11-smoke PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=xcb"
            TIMEOUT 10)
    endif()
    add_test(NAME modern-ime-keyboard-latency COMMAND modern-ime-latency-benchmark)
endif()

add_custom_target(format
    COMMAND clang-format -i $<TARGET_PROPERTY:modern-ime-contracts,SOURCES> $<TARGET_PROPERTY:modern-ime-language-core,SOURCES>
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Formatting core C++ sources")
