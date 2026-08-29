include_guard(GLOBAL)

option(AERO_BUILD_EXAMPLES "Build aero examples" ${PROJECT_IS_TOP_LEVEL})
option(AERO_BUILD_TESTS "Build aero tests" ${PROJECT_IS_TOP_LEVEL})

set(AERO_TLS_BACKEND "none" CACHE STRING "TLS backend: wolfssl, openssl, none")
set_property(CACHE AERO_TLS_BACKEND PROPERTY STRINGS wolfssl openssl none)

option(AERO_USE_BUNDLED_ASIO "Fetch ASIO using FetchContent if not found on system or in targets" ${PROJECT_IS_TOP_LEVEL})
