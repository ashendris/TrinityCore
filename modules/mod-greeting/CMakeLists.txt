trinity_add_module(NAME mod-greeting
    SOURCES src/mod_greeting.cpp
    MANIFEST ${CMAKE_CURRENT_SOURCE_DIR}/module.json
    CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/conf/mod-greeting.conf.dist
    SQL ${CMAKE_CURRENT_SOURCE_DIR}/sql/world/mod_greeting_001.sql
    INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/include
)
