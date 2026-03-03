include(AeroUseGTest)
include(AeroUseMagicEnum)
include(GoogleTest)

file(GLOB_RECURSE aero_unit_test_sources CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_LIST_DIR}/*.cpp"
)

if(NOT AERO_USE_TLS)
  list(FILTER aero_unit_test_sources EXCLUDE REGEX "tls[/\\\\].*\\.cpp$")
endif()

list(FILTER aero_unit_test_sources EXCLUDE REGEX "websocket[/\\\\]network[/\\\\].*\\.cpp$")

add_executable(aero_tests)
aero_use_google_test(aero_tests)
aero_use_magic_enum(aero_tests)

target_sources(aero_tests PRIVATE ${aero_unit_test_sources})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${aero_unit_test_sources})
target_compile_features(aero_tests PRIVATE cxx_std_23)
target_include_directories(aero_tests PRIVATE ${aero_INCLUDE_DIR} ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(aero_tests PRIVATE GTest::gtest_main aero::aero)

gtest_discover_tests(aero_tests)

if(AERO_BUILD_NETWORK_TESTS)
  file(GLOB_RECURSE aero_network_test_sources CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/websocket/network/tcp/*.cpp"
  )

  if(AERO_BUILD_NETWORK_TLS_TESTS AND AERO_USE_TLS)
    file(GLOB_RECURSE aero_network_tls_test_sources CONFIGURE_DEPENDS
      "${CMAKE_CURRENT_LIST_DIR}/websocket/network/tls/*.cpp"
    )
    list(APPEND aero_network_test_sources ${aero_network_tls_test_sources})
  endif()

  add_executable(aero_network_tests)
  aero_use_google_test(aero_network_tests)
  aero_use_magic_enum(aero_network_tests)

  target_sources(aero_network_tests PRIVATE ${aero_network_test_sources})
  source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${aero_network_test_sources})
  target_compile_features(aero_network_tests PRIVATE cxx_std_23)
  target_include_directories(aero_network_tests PRIVATE ${aero_INCLUDE_DIR} ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(aero_network_tests PRIVATE GTest::gtest_main aero::aero)
  gtest_discover_tests(aero_network_tests)
endif()
