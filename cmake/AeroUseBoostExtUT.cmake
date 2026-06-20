include_guard(GLOBAL)

include(FetchContent)

function(_aero_select_first_existing_boost_ext_ut_target out_var)
  set(aero_boost_ext_ut_candidate_targets
    Boost::ut
    boost_ut::ut
    ut::ut
    ut
  )

  foreach(aero_boost_ext_ut_candidate_target IN LISTS aero_boost_ext_ut_candidate_targets)
    if(TARGET ${aero_boost_ext_ut_candidate_target})
      set(${out_var} ${aero_boost_ext_ut_candidate_target} PARENT_SCOPE)
      return()
    endif()
  endforeach()

  set(${out_var} "" PARENT_SCOPE)
endfunction()

function(_aero_try_enable_boost_ext_ut)
  if(NOT TARGET Boost::ut)
    set(CMAKE_FIND_PACKAGE_PREFER_CONFIG TRUE)
    find_package(ut CONFIG QUIET)
  endif()

  _aero_select_first_existing_boost_ext_ut_target(aero_boost_ext_ut_target)
  if(aero_boost_ext_ut_target)
    message(STATUS "[AERO] Using boost-ext/ut (found target ${aero_boost_ext_ut_target})")
  endif()
endfunction()

function(_aero_enable_bundled_boost_ext_ut)
  message(STATUS "[AERO] Fetching boost-ext/ut v2.3.1 from git...")

  FetchContent_Declare(boost_ext_ut
    GIT_REPOSITORY  https://github.com/boost-ext/ut
    GIT_TAG         v2.3.1
    GIT_SHALLOW     TRUE
  )
  FetchContent_MakeAvailable(boost_ext_ut)
endfunction()

function(aero_use_boost_ext_ut dependent_target)
  _aero_select_first_existing_boost_ext_ut_target(aero_boost_ext_ut_target)

  if(NOT aero_boost_ext_ut_target)
    _aero_try_enable_boost_ext_ut()
    _aero_select_first_existing_boost_ext_ut_target(aero_boost_ext_ut_target)
  endif()

  if(NOT aero_boost_ext_ut_target)
    _aero_enable_bundled_boost_ext_ut()
    _aero_select_first_existing_boost_ext_ut_target(aero_boost_ext_ut_target)
  endif()

  if(NOT aero_boost_ext_ut_target)
    message(FATAL_ERROR "boost-ext/ut target was not found after enabling the dependency")
  endif()

  get_target_property(aero_dependent_target_type ${dependent_target} TYPE)
  if(aero_dependent_target_type STREQUAL "INTERFACE_LIBRARY")
    target_link_libraries(${dependent_target} INTERFACE ${aero_boost_ext_ut_target})
  else()
    target_link_libraries(${dependent_target} PRIVATE ${aero_boost_ext_ut_target})
  endif()
endfunction()
