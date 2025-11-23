set(CMAKE_AUTOMOC ON)      # automatically generate MOC files
set(CMAKE_AUTOUIC ON)      # automatically process .ui files
set(CMAKE_AUTORCC ON)      # if you have Qt resources (.qrc)

add_executable(client
    client.cpp
    clientwindow.cpp
    clientwindow.h
    clientwindow.ui 
)

set(CLIENT_PROTO_FILES ${CMAKE_SOURCE_DIR}/proto/ocr.proto)
set(CLIENT_PROTO_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/proto)
set(CLIENT_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/proto_build")

PROTOBUF_GENERATE(
    LANGUAGE cpp
    TARGET client
    PROTOS ${CLIENT_PROTO_FILES}
    IMPORT_DIRS ${CLIENT_PROTO_INCLUDE_DIR}
    PROTOC_OUT_DIR "${CLIENT_BINARY_DIR}"
)

add_custom_command(
    OUTPUT
        ${CLIENT_BINARY_DIR}/ocr.grpc.pb.cc
        ${CLIENT_BINARY_DIR}/ocr.grpc.pb.h
    COMMAND ${Protobuf_PROTOC_EXECUTABLE}
    ARGS
        --grpc_out=${CLIENT_BINARY_DIR}
        --plugin=protoc-gen-grpc="D:\\Documents\\vcpkg\\installed\\x64-windows\\tools\\grpc\\grpc_cpp_plugin.exe" #Requires full actual directory in this format
        -I ${CLIENT_PROTO_INCLUDE_DIR}
        ${CLIENT_PROTO_FILES}
    DEPENDS ${CLIENT_PROTO_FILES}
)

target_sources(client PRIVATE
    ${CLIENT_BINARY_DIR}/ocr.grpc.pb.cc
    ${CLIENT_BINARY_DIR}/ocr.grpc.pb.h
)


# Include directories
target_include_directories(client PRIVATE
    ${CLIENT_BINARY_DIR}
)

# Link libraries
target_link_libraries(client PRIVATE
    #proto_lib
    Qt6::Widgets
    gRPC::grpc++
    protobuf::libprotobuf
)