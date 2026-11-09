if(UNIX)
  SET(BINPATH "bin")
elseif(WIN32)
  SET(BINPATH ".")
endif()

add_executable(relive WIN32
    Exe.cpp
    resource.rc
    resource.h
    app.ico)
target_include_directories(relive PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>
    PRIVATE ${CMAKE_BINARY_DIR})
target_compile_features(relive
    PRIVATE cxx_auto_type
    PRIVATE cxx_variadic_templates)
target_compile_definitions(relive PRIVATE "_CRT_SECURE_NO_WARNINGS")
if(PS2)
    target_link_libraries(relive AliveLibAE project_warnings)
else()
    target_link_libraries(relive AliveLibAE AliveLibAO project_warnings)
endif()
if(PS2)
    # The R5900 has no LL/SC, so std::atomic falls back to libatomic calls.
    target_link_libraries(relive atomic eedebug -ldebug)
    # ps2sdk gives the main thread 128 KB of stack; the engine was written for Windows' 1 MB and
    # overflowed it while loading a level, corrupting memory and freezing the EE.
    target_link_options(relive PRIVATE -Wl,--defsym,_stack_size=0x100000)
    if(RELIVE_PS2_HOSTFS)
        # Test builds report any use of semaphore 0 (the kernel topThread's), see Exe.cpp.
        target_link_options(relive PRIVATE -Wl,--wrap=DeleteSema -Wl,--wrap=SignalSema)
    endif()
endif()
export(TARGETS relive FILE relive.cmake)
install(TARGETS relive DESTINATION "${BINPATH}")

if (MSVC)
    target_compile_options(relive PRIVATE /W4 /wd4996 /WX /MP)
endif()
