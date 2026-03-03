include_guard(GLOBAL)

include(FetchContent)

set(AERO_ASIO_DEFINITIONS
  ASIO_STANDALONE
  ASIO_NO_DEPRECATED
  ASIO_NO_TS_EXECUTORS
  ASIO_DISABLE_BUFFER_DEBUGGING
  ASIO_NO_TYPEID
)

function(_aero_select_first_existing_asio_target out_var)
  set(aero_asio_candidate_targets
    Asio::Asio
    asio::asio
    asio
  )

  foreach(aero_asio_candidate_target IN LISTS aero_asio_candidate_targets)
    if(TARGET ${aero_asio_candidate_target})
      set(${out_var} ${aero_asio_candidate_target} PARENT_SCOPE)
      return()
    endif()
  endforeach()

  set(${out_var} "" PARENT_SCOPE)
endfunction()

function(_aero_enable_bundled_asio asio_library_target)
  message(STATUS "[AERO-ASIO] Fetching asio-1.36.0 from git...")

  FetchContent_Declare(asio
    GIT_REPOSITORY  https://github.com/chriskohlhoff/asio
    GIT_TAG         asio-1-36-0
    GIT_SHALLOW     TRUE
  )
  FetchContent_MakeAvailable(asio)

  if(DEFINED asio_SOURCE_DIR)
    target_include_directories(${asio_library_target} INTERFACE "${asio_SOURCE_DIR}/asio/include")
  endif()
endfunction()

function(aero_use_asio dependent_target)
  if(NOT TARGET aero_asio_library)
    add_library(aero_asio_library INTERFACE)
  endif()

  target_compile_definitions(aero_asio_library INTERFACE ${AERO_ASIO_DEFINITIONS})

  _aero_select_first_existing_asio_target(aero_asio_target)

  if(NOT aero_asio_target)
    find_package(asio CONFIG QUIET)
    _aero_select_first_existing_asio_target(aero_asio_target)
  endif()

  if(NOT aero_asio_target)
    find_package(Asio QUIET)
    _aero_select_first_existing_asio_target(aero_asio_target)
  endif()

  if(aero_asio_target)
    message(STATUS "[AERO-ASIO] Using asio (found target ${aero_asio_target})")
    target_link_libraries(aero_asio_library INTERFACE ${aero_asio_target})
  elseif(AERO_USE_BUNDLED_ASIO)
    message(STATUS "[AERO-ASIO] Using bundled asio")
    _aero_enable_bundled_asio(aero_asio_library)
  else()
    message(FATAL_ERROR "Standalone Asio was not found. Install Asio or set -DAERO_USE_BUNDLED_ASIO=ON")
  endif()

  target_link_libraries(${dependent_target} INTERFACE aero_asio_library)
endfunction()
