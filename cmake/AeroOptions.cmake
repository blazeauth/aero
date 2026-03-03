include_guard(GLOBAL)

option(AERO_BUILD_EXAMPLES "Build aero examples" ${PROJECT_IS_TOP_LEVEL})
option(AERO_BUILD_TESTS "Build aero tests" ${PROJECT_IS_TOP_LEVEL})
option(AERO_BUILD_NETWORK_TESTS "Build aero network tests (connect to the public network)" ${PROJECT_IS_TOP_LEVEL})
option(AERO_BUILD_NETWORK_TLS_TESTS "Build aero network TLS tests (connect to the public network)" OFF)

set(AERO_TLS_BACKEND "none" CACHE STRING "TLS backend: wolfssl, openssl, none")
set_property(CACHE AERO_TLS_BACKEND PROPERTY STRINGS wolfssl openssl none)

option(AERO_USE_BUNDLED_ASIO "Fetch ASIO using FetchContent if not found on system or in targets" ${PROJECT_IS_TOP_LEVEL})
option(AERO_USE_BUNDLED_UTFCPP "Fetch utfcpp using FetchContent if not found on system or in targets" ${PROJECT_IS_TOP_LEVEL})
