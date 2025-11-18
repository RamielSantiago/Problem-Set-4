# Create the server executable
add_executable(server
    
    "server.cpp"
)

set(SERVER_PROTO_FILES ${CMAKE_SOURCE_DIR}/proto/ocr.proto)
set(SERVER_PROTO_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/proto)
set(SERVER_PROTO_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/proto_build")

PROTOBUF_GENERATE(
    LANGUAGE cpp
    TARGET server
    PROTOS ${SERVER_PROTO_FILES}
    IMPORT_DIRS ${SERVER_PROTO_INCLUDE_DIR}
    PROTOC_OUT_DIR "${SERVER_PROTO_BINARY_DIR}"
)

# Include directories
target_include_directories(server PRIVATE
    ${Tesseract_INCLUDE_DIRS}
    ${Leptonica_INCLUDE_DIRS}
    ${SERVER_PROTO_BINARY_DIR}
)

# Link libraries
target_link_libraries(server PRIVATE  
    gRPC::grpc
    gRPC::grpc++
    protobuf::libprotobuf
    ${Tesseract_LIBRARIES}   
    ${Leptonica_LIBRARIES}   
)