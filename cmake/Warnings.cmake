# cmake/Warnings.cmake — reusable per-target warning configuration

function(set_project_warnings target)
  set(MSVC_WARNINGS
    /W4 /permissive- /utf-8 /Zc:__cplusplus
  )

  set(GCC_CLANG_WARNINGS
    -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor
    -Wold-style-cast -Wcast-align -Woverloaded-virtual
    -Wconversion -Wsign-conversion -Wnull-dereference
    -Wdouble-promotion -Wformat=2
  )

  set(GCC_ONLY_WARNINGS
    -Wduplicated-cond -Wduplicated-branches -Wlogical-op
    -Wuseless-cast
  )

  if(WARNINGS_AS_ERRORS)
    list(APPEND MSVC_WARNINGS /WX)
    list(APPEND GCC_CLANG_WARNINGS -Werror)
  endif()

  if(MSVC)
    target_compile_options(${target} PRIVATE ${MSVC_WARNINGS})
  elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(${target} PRIVATE ${GCC_CLANG_WARNINGS} ${GCC_ONLY_WARNINGS})
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(${target} PRIVATE ${GCC_CLANG_WARNINGS})
  endif()
endfunction()
