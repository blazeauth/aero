include_guard(GLOBAL)

include(FetchContent)

function(_aero_try_enable_gtest dependent_target)
  if(NOT TARGET GTest::gtest_main)
    find_package(GTest QUIET)
  endif()

  if(TARGET GTest::gtest_main)
    message(STATUS "[AERO] Using GTest (GTest::gtest_main)")
  endif()
endfunction()

function(_aero_enable_bundled_gtest dependent_target)
  message(STATUS "[AERO] Fetching GoogleTest v1.17.0 from git...")

  FetchContent_Declare(google_test
    GIT_REPOSITORY  https://github.com/google/googletest
    GIT_TAG         v1.17.0
    GIT_SHALLOW     ON
  )
  FetchContent_MakeAvailable(google_test)
endfunction()

function(aero_use_google_test dependent_target)
  if (NOT TARGET GTest::gtest_main)
    _aero_try_enable_gtest(${dependent_target})

    # Not found on system
    if (NOT TARGET GTest::gtest_main)
      _aero_enable_bundled_gtest(${dependent_target})
    endif()
  endif()

  target_link_libraries(${dependent_target} PRIVATE GTest::gtest_main)
endfunction()
