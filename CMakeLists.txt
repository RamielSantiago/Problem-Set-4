cmake_minimum_required (VERSION 3.10)

list(APPEND CMAKE_MODULE_PATH "$ENV{VCPKG_ROOT}/installed/x64-windows/share")
list(APPEND CMAKE_MODULE_PATH "$ENV{VCPKG_ROOT}/installed/x64-windows/lib")

project ("PS4" LANGUAGES CXX)

# ------------------------- Project Settings ----------------------------
set(CMAKE_CXX_STANDARD 20) 
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_DEBUG_POSTFIX "")

# ------------------------- Qt Package Setup ----------------------------
if(DEFINED ENV{QT6_PATH})
    list(PREPEND CMAKE_PREFIX_PATH $ENV{QT6_PATH})
endif()
find_package(Qt6 REQUIRED COMPONENTS Widgets)

#-------------------------- Tesseract OCR ------------------------------- 
find_package(Tesseract REQUIRED)
find_package(Leptonica REQUIRED)
set(Tesseract_LIBRARIES "$ENV{VCPKG_ROOT}/installed/x64-windows/lib/tesseract55.lib")
set(Leptonica_LIBRARIES "$ENV{VCPKG_ROOT}/installed/x64-windows/lib/leptonica-1.85.0.lib")
set(Tesseract_INCLUDE_DIRS "$ENV{VCPKG_ROOT}/installed/x64-windows/include/tesseract")
set(Leptonica_INCLUDE_DIRS "$ENV{VCPKG_ROOT}/installed/x64-windows/include/leptonica")

#-------------------------- gRPC/Protobuf ------------------------------- 
find_package(gRPC CONFIG REQUIRED) 
find_package(Protobuf CONFIG REQUIRED)

#----------------------- Add subdirectories -----------------------------
add_subdirectory(client)
add_subdirectory(server)