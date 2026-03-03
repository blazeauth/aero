include_guard(GLOBAL)

include(FetchContent)

function(_aero_try_enable_magic_enum dependent_target)
  if(NOT TARGET magic_enum::magic_enum)
    set(CMAKE_FIND_PACKAGE_PREFER_CONFIG TRUE)
    find_package(magic_enum CONFIG QUIET)
  endif()

  if(TARGET magic_enum::magic_enum)
    message(STATUS "[AERO] Using magic_enum (magic_enum::magic_enum)")
  endif()
endfunction()

function(_aero_enable_bundled_magic_enum dependent_target)
  message(STATUS "[AERO] Fetching magic_enum v0.9.7 from git...")

  FetchContent_Declare(magic_enum
    GIT_REPOSITORY  https://github.com/Neargye/magic_enum
    GIT_TAG         v0.9.7
    GIT_SHALLOW     ON
  )
  FetchContent_MakeAvailable(magic_enum)
endfunction()

function(aero_use_magic_enum dependent_target)
  if (NOT TARGET magic_enum::magic_enum)
    _aero_try_enable_magic_enum(${dependent_target})

    # Not found on system
    if (NOT TARGET magic_enum::magic_enum)
      _aero_enable_bundled_magic_enum(${dependent_target})
    endif()
  endif()

  target_link_libraries(${dependent_target} PRIVATE magic_enum::magic_enum)
endfunction()

