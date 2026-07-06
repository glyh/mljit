# cmake/Sanitizers.cmake — reusable per-target sanitizer configuration

function(enable_sanitizers target)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set(SANITIZER_FLAGS "")

    if(ENABLE_ASAN)
      list(APPEND SANITIZER_FLAGS -fsanitize=address -fno-omit-frame-pointer)
    endif()

    if(ENABLE_UBSAN)
      list(APPEND SANITIZER_FLAGS -fsanitize=undefined)
    endif()

    if(ENABLE_TSAN)
      list(APPEND SANITIZER_FLAGS -fsanitize=thread)
    endif()

    if(SANITIZER_FLAGS)
      target_compile_options(${target} PRIVATE ${SANITIZER_FLAGS})
      target_link_options(${target} PRIVATE ${SANITIZER_FLAGS})
    endif()
  endif()
endfunction()
