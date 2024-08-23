set(SRC_LIST 
    ${PROJECT_SOURCE_DIR}/src/main.cpp
    ${PROJECT_SOURCE_DIR}/src/http_conn.cpp
    ${PROJECT_SOURCE_DIR}/src/memory_pool.cpp
    ${PROJECT_SOURCE_DIR}/src/LFUCache.cpp
    # ${PROJECT_SOURCE_DIR}/src/nonactive_conn.cpp
)

add_executable(mywebserver ${SRC_LIST})
target_link_libraries(mywebserver pthread)