# 卖出策略程序编译配置
cmake_minimum_required(VERSION 3.10)
project(SellStrategy CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Windows MSVC UTF-8 support
if(MSVC)
    add_compile_options(/utf-8)
endif()

# ==================== 路径配置 ====================
if(WIN32)
    set(SDK_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/include_external/windows")
    set(SDK_LIB_DIR "${CMAKE_SOURCE_DIR}/lib/windows")
else()
    set(SDK_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/include_external/linux")
    set(SDK_LIB_DIR "${CMAKE_SOURCE_DIR}/lib/linux")
    include_directories("${SDK_INCLUDE_DIR}/secitpdk")
endif()

# ==================== 头文件路径 ====================
include_directories(
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/adapters
    ${CMAKE_SOURCE_DIR}/src/strategies
    ${SDK_INCLUDE_DIR}
)

# ==================== 库文件路径 ====================
link_directories("${SDK_LIB_DIR}")

# ==================== 源文件 ====================
set(USAGE_EXAMPLE_SOURCES
    examples/usage_example.cpp
    src/adapters/TdfMarketDataApi.cpp
    src/adapters/SecTradingApi.cpp
    src/strategies/IntradaySellStrategy.cpp
    src/core/CsvConfig.cpp
    src/core/SellStrategy.cpp
    src/core/util.cpp
)

# ==================== 编译可执行文件 ====================
add_executable(usage_example ${USAGE_EXAMPLE_SOURCES})

# ==================== 拷贝配置文件到构建目录 ====================
# 拷贝CSV和config.json到构建输出目录，确保程序能找到配置文件
# 使用 GLOB 自动查找所有 CSV 文件
file(GLOB CSV_FILES "${CMAKE_SOURCE_DIR}/*.csv")
foreach(CSV_FILE ${CSV_FILES})
    get_filename_component(CSV_NAME ${CSV_FILE} NAME)
    configure_file(${CSV_FILE} ${CMAKE_BINARY_DIR}/${CSV_NAME} COPYONLY)
    message(STATUS "复制 CSV: ${CSV_NAME} -> build/")
endforeach()
configure_file(${CMAKE_SOURCE_DIR}/config.json ${CMAKE_BINARY_DIR}/config.json COPYONLY)

# Windows平台还需要拷贝到Release/Debug子目录
if(WIN32)
    foreach(CSV_FILE ${CSV_FILES})
        get_filename_component(CSV_NAME ${CSV_FILE} NAME)
        configure_file(${CSV_FILE} ${CMAKE_BINARY_DIR}/Release/${CSV_NAME} COPYONLY)
        configure_file(${CSV_FILE} ${CMAKE_BINARY_DIR}/Debug/${CSV_NAME} COPYONLY)
    endforeach()
    configure_file(${CMAKE_SOURCE_DIR}/config.json ${CMAKE_BINARY_DIR}/Release/config.json COPYONLY)
    configure_file(${CMAKE_SOURCE_DIR}/config.json ${CMAKE_BINARY_DIR}/Debug/config.json COPYONLY)
    
    # ==================== 自动复制 DLL 文件 ====================
    # 获取所有 DLL 文件
    file(GLOB SDK_DLLS "${SDK_LIB_DIR}/*.dll")
    
    # 复制到 Release 和 Debug 目录
    foreach(DLL_FILE ${SDK_DLLS})
        get_filename_component(DLL_NAME ${DLL_FILE} NAME)
        configure_file(${DLL_FILE} ${CMAKE_BINARY_DIR}/Release/${DLL_NAME} COPYONLY)
        configure_file(${DLL_FILE} ${CMAKE_BINARY_DIR}/Debug/${DLL_NAME} COPYONLY)
        message(STATUS "将复制 DLL: ${DLL_NAME} -> Release/Debug")
    endforeach()
    
    # ==================== 复制 itpdk.ini 配置文件 ====================
    # itpdk.ini 包含交易 API 的登录信息，需要和可执行文件在同一目录
    if(EXISTS "${CMAKE_SOURCE_DIR}/itpdk.ini")
        configure_file(${CMAKE_SOURCE_DIR}/itpdk.ini ${CMAKE_BINARY_DIR}/Release/itpdk.ini COPYONLY)
        configure_file(${CMAKE_SOURCE_DIR}/itpdk.ini ${CMAKE_BINARY_DIR}/Debug/itpdk.ini COPYONLY)
        message(STATUS "已复制 itpdk.ini -> Release/Debug")
    else()
        message(WARNING "未找到 itpdk.ini 文件，交易 API 可能无法正常登录")
    endif()
else()
    # Linux 平台：itpdk.ini 复制到 build 根目录
    if(EXISTS "${CMAKE_SOURCE_DIR}/itpdk.ini")
        configure_file(${CMAKE_SOURCE_DIR}/itpdk.ini ${CMAKE_BINARY_DIR}/itpdk.ini COPYONLY)
        message(STATUS "已复制 itpdk.ini -> build/")
    else()
        message(WARNING "未找到 itpdk.ini 文件，交易 API 可能无法正常登录")
    endif()
endif()

# ==================== 链接库 ====================
if(WIN32)
    target_link_libraries(usage_example
        TDFAPI30
        secitpdk
        ws2_32
    )
    
    set_target_properties(usage_example PROPERTIES
        VS_DEBUGGER_ENVIRONMENT "PATH=${SDK_LIB_DIR};$ENV{PATH}"
    )
else()
    # Linux 平台
    target_link_libraries(usage_example
        TDFAPI30
        secitpdk
        pthread
    )
    
    # RPATH 配置（自动查找 .so 文件，无需复制）
    set_target_properties(usage_example PROPERTIES
        BUILD_RPATH "${SDK_LIB_DIR}"
        INSTALL_RPATH "$ORIGIN:$ORIGIN/../lib/linux:${SDK_LIB_DIR}"
        BUILD_RPATH_USE_ORIGIN ON
    )
    
    message(STATUS "Linux RPATH: ${SDK_LIB_DIR}")
endif()

# ==================== 输出信息 ====================
message(STATUS "========================================")
message(STATUS "Project: ${PROJECT_NAME}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "SDK Include: ${SDK_INCLUDE_DIR}")
message(STATUS "SDK Lib: ${SDK_LIB_DIR}")
message(STATUS "========================================")
