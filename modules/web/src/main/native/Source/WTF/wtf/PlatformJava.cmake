set(WTF_LIBRARY_TYPE STATIC)

list(APPEND WTF_INCLUDE_DIRECTORIES
    "${WTF_DIR}/wtf/java"
    "${CMAKE_SOURCE_DIR}/Source"
    "${JAVA_INCLUDE_PATH}"
    "${JAVA_INCLUDE_PATH2}"
)

list(APPEND WTF_HEADERS
    java/JavaEnv.h
    java/JavaRef.h
    java/DbgUtils.h
    unicode/java/UnicodeJava.h
)

list(APPEND WTF_SOURCES
    java/StringJava.cpp
    java/MainThreadJava.cpp
    java/JavaEnv.cpp
    java/TextBreakIteratorInternalICUJava.cpp
)

list(APPEND WTF_LIBRARIES
    "${JAVA_JVM_LIBRARY}"
)

list(APPEND WTF_SYSTEM_INCLUDE_DIRECTORIES
	  "${JDK_INCLUDE_DIRS}"
)

if (APPLE)
    file(COPY mac/MachExceptions.defs DESTINATION ${DERIVED_SOURCES_WTF_DIR})

    add_custom_command(
        OUTPUT
            ${DERIVED_SOURCES_WTF_DIR}/MachExceptionsServer.h
            ${DERIVED_SOURCES_WTF_DIR}/mach_exc.h
            ${DERIVED_SOURCES_WTF_DIR}/mach_excServer.c
            ${DERIVED_SOURCES_WTF_DIR}/mach_excUser.c
        MAIN_DEPENDENCY mac/MachExceptions.defs
        WORKING_DIRECTORY ${DERIVED_SOURCES_WTF_DIR}
        COMMAND mig -sheader MachExceptionsServer.h MachExceptions.defs
        VERBATIM)

    list(APPEND WTF_SOURCES
        ${DERIVED_SOURCES_WTF_DIR}/mach_excServer.c
        ${DERIVED_SOURCES_WTF_DIR}/mach_excUser.c
    )

    list(APPEND WTF_HEADERS
        cf/TypeCastsCF.h
    )

    list(APPEND WTF_PRIVATE_INCLUDE_DIRECTORIES
        # Check whether we can use WTF/icu
        # "${WTF_DIR}/icu"
        ${DERIVED_SOURCES_WTF_DIR}
    )

    list(APPEND WTF_SOURCES
        cf/RunLoopCF.cpp
        cf/LanguageCF.cpp
        cocoa/CPUTimeCocoa.mm
        cocoa/MemoryFootprintCocoa.cpp
        cocoa/MemoryPressureHandlerCocoa.mm
        cocoa/WorkQueueCocoa.cpp
        text/cf/StringImplCF.cpp
        text/cf/StringCF.cpp
        text/mac/StringMac.mm
        text/mac/StringImplMac.mm
        BlockObjCExceptions.mm
    )

    find_library(COCOA_LIBRARY Cocoa)
    find_library(COREFOUNDATION_LIBRARY CoreFoundation)
    list(APPEND WTF_LIBRARIES
        ${COREFOUNDATION_LIBRARY}
        ${COCOA_LIBRARY}
    )
elseif (UNIX)
    list(APPEND WTF_SOURCES
        generic/RunLoopGeneric.cpp
        generic/WorkQueueGeneric.cpp
        linux/CurrentProcessMemoryStatus.cpp
        linux/MemoryFootprintLinux.cpp
        linux/MemoryPressureHandlerLinux.cpp
        unix/CPUTimeUnix.cpp
        unix/LanguageUnix.cpp
    )
    list(APPEND WTF_LIBRARIES rt)
elseif (WIN32)
    list(APPEND WTF_SOURCES
        win/CPUTimeWin.cpp
        win/LanguageWin.cpp
        win/MemoryFootprintWin.cpp
        win/MemoryPressureHandlerWin.cpp
        win/RunLoopWin.cpp
        win/WorkQueueWin.cpp
        win/WorkItemContext.cpp
    )

    list(APPEND WTF_HEADERS
        text/win/WCharStringExtras.h
        win/Win32Handle.h
    )

    list(APPEND WTF_LIBRARIES
        winmm
    )
endif ()

if (DEFINED CMAKE_USE_PTHREADS_INIT)
    list(APPEND WTF_LIBRARIES pthread)
endif()

add_dependencies(WTF icudatagen)
