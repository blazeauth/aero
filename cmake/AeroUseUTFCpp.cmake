include_guard(GLOBAL)

include(FetchContent)

function(_aero_select_first_existing_utfcpp_target out_var)
  set(aero_utfcpp_candidate_targets
    utfcpp::utfcpp
    utf8cpp::utf8cpp
    utf8::cpp
    utfcpp
    utf8cpp
  )

  foreach(aero_utfcpp_candidate_target IN LISTS aero_utfcpp_candidate_targets)
    if(TARGET ${aero_utfcpp_candidate_target})
      set(${out_var} ${aero_utfcpp_candidate_target} PARENT_SCOPE)
      return()
    endif()
  endforeach()

  set(${out_var} "" PARENT_SCOPE)
endfunction()

function(_aero_enable_bundled_utfcpp utfcpp_library_target)
  message(STATUS "[AERO-UTFCPP] Using bundled utfcpp. Fetching v4.0.8 from git...")

  FetchContent_Declare(utfcpp
    GIT_REPOSITORY  https://github.com/nemtrif/utfcpp/
    GIT_TAG         v4.0.8
    GIT_SHALLOW     TRUE
  )
  FetchContent_MakeAvailable(utfcpp)

  if(DEFINED utfcpp_SOURCE_DIR)
    target_include_directories(${utfcpp_library_target} INTERFACE "${utfcpp_SOURCE_DIR}/source")
  endif()
endfunction()

function(aero_use_utfcpp dependent_target)
  if(NOT TARGET aero_utfcpp_library)
    add_library(aero_utfcpp_library INTERFACE)
  endif()

  _aero_select_first_existing_utfcpp_target(aero_utfcpp_target)

  if(NOT aero_utfcpp_target)
    find_package(utf8cpp CONFIG QUIET)
    _aero_select_first_existing_utfcpp_target(aero_utfcpp_target)
  endif()

  if(NOT aero_utfcpp_target)
    find_package(utfcpp CONFIG QUIET)
    _aero_select_first_existing_utfcpp_target(aero_utfcpp_target)
  endif()

  if(aero_utfcpp_target)
    message(STATUS "[AERO-UTFCPP] Using system utfcpp (found target ${aero_utfcpp_target})")
    target_link_libraries(aero_utfcpp_library INTERFACE ${aero_utfcpp_target})
  elseif(AERO_USE_BUNDLED_UTFCPP)
    _aero_enable_bundled_utfcpp(aero_utfcpp_library)
  else()
    message(FATAL_ERROR "utfcpp was not found. Install utfcpp/utf8cpp or set -DAERO_USE_BUNDLED_UTFCPP=ON")
  endif()

  target_link_libraries(${dependent_target} INTERFACE aero_utfcpp_library)
endfunction()
