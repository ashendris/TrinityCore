
trinity_add_module(NAME mod-example
    SOURCES src/mod_example.cpp
    MANIFEST ${CMAKE_CURRENT_SOURCE_DIR}/module.json
    CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/conf/mod-example.conf.dist
    SQL ${CMAKE_CURRENT_SOURCE_DIR}/sql/world/mod_example_001.sql
    INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/../mod-greeting/include
)
