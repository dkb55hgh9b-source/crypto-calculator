cmake_minimum_required(VERSION 3.21)
project(IOSCalculator LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Qt 6: один и тот же проект собирается для Windows и для Android
find_package(Qt6 REQUIRED COMPONENTS Widgets Network)
qt_standard_project_setup()

# qt_add_executable (а не add_executable) нужен для Android: он собирает APK.
# WIN32 и MACOSX_BUNDLE обязательно указывать здесь, а не через set_target_properties:
# иначе на Windows (MinGW) будет ошибка "undefined reference to qMain(int, char**)".
qt_add_executable(ios_calculator WIN32 MACOSX_BUNDLE
    main.cpp calculator_engine.h units.h expr_tracker.h)

target_link_libraries(ios_calculator PRIVATE Qt6::Widgets Qt6::Network)

set_target_properties(ios_calculator PROPERTIES
    # Android: идентификатор приложения — замените на свой (com.ваше_имя.calculator)
    QT_ANDROID_PACKAGE_NAME "com.example.ioscalculator"
    QT_ANDROID_VERSION_NAME "1.0"
    QT_ANDROID_VERSION_CODE 1)

if(MSVC)
    target_compile_options(ios_calculator PRIVATE /utf-8)
endif()

# Android: для HTTPS (курсы НБУ и CoinGecko) в приложение нужно добавить библиотеки OpenSSL.
# Скачиваются при конфигурации проекта (нужен интернет).
if(ANDROID)
    include(FetchContent)
    FetchContent_Declare(android_openssl
        DOWNLOAD_EXTRACT_TIMESTAMP true
        URL https://github.com/KDAB/android_openssl/archive/refs/heads/master.zip)
    FetchContent_MakeAvailable(android_openssl)
    include(${android_openssl_SOURCE_DIR}/android_openssl.cmake)
    add_android_openssl_libraries(ios_calculator)
endif()

# iPhone: идентификатор и название приложения (сборка — только на Mac / в GitHub Actions)
if(IOS)
    set_target_properties(ios_calculator PROPERTIES
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.example.ioscalculator"
        MACOSX_BUNDLE_BUNDLE_NAME "Calculator"
        MACOSX_BUNDLE_BUNDLE_VERSION "1"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0"
        XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2")
endif()
