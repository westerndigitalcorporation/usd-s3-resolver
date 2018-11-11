set(PLUGIN_NAME S3Resolver)

find_package(Boost REQUIRED)
find_package(PythonLibs REQUIRED)
find_package(OpenEXR REQUIRED)
find_package(TBB REQUIRED)

find_package(AWSSDK REQUIRED COMPONENTS s3) # s3-encryption)

link_directories(${USD_LIBRARY_DIR})

file(GLOB SRC *.cpp)

add_library(${PLUGIN_NAME} SHARED ${SRC})
set_target_properties(${PLUGIN_NAME} PROPERTIES PREFIX "")
set_target_properties(${PLUGIN_NAME} PROPERTIES INSTALL_RPATH_USE_LINK_PATH ON)
target_link_libraries(${PLUGIN_NAME} arch tf plug vt ar sdf usd)
target_link_libraries(${PLUGIN_NAME} ${AWSSDK_LINK_LIBRARIES})
target_include_directories(${PLUGIN_NAME} SYSTEM PRIVATE "${USD_INCLUDE_DIR}")
target_include_directories(${PLUGIN_NAME} SYSTEM PRIVATE "${Boost_INCLUDE_DIRS}")
target_include_directories(${PLUGIN_NAME} SYSTEM PRIVATE "${PYTHON_INCLUDE_DIRS}")
target_include_directories(${PLUGIN_NAME} SYSTEM PRIVATE "${OPENEXR_INCLUDE_DIRS}")
target_include_directories(${PLUGIN_NAME} SYSTEM PRIVATE "${TBB_INCLUDE_DIRS}")

install(TARGETS ${PLUGIN_NAME}
        DESTINATION .)

install(FILES plugInfo.json
        DESTINATION ${PLUGIN_NAME}/resources/)

install(FILES README.md
        DESTINATION docs)
