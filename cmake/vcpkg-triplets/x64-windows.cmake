set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)

# TODO: reconsider once vcpkg ships a wolfSSL release with
# https://github.com/wolfSSL/wolfssl/pull/10785 (merged 2026-07-21, not in 5.9.2).
# For builds with undefined WOLFSSL_HAVE_ERROR_QUEUE it makes the
# .ERR_get_error() return 0 instead of NOT_COMPILED_IN (174), which is enough
# for asio to load PEM certificates on Windows.
#
# The per-thread queue below additionally gives real error codes and
# messages. However, within the aero repository, our only concerns are
# simplicity, a successful build, and high-quality, test-passing builds.
# Users must figure out everything else on their own, including configuring a
# suitable SSL backend build.
# See https://github.com/blazeauth/aero/pull/67 for more details
if(PORT STREQUAL "wolfssl")
  set(VCPKG_C_FLAGS "${VCPKG_C_FLAGS} -DWOLFSSL_HAVE_ERROR_QUEUE -DERROR_QUEUE_PER_THREAD")
  set(VCPKG_CXX_FLAGS "${VCPKG_CXX_FLAGS} -DWOLFSSL_HAVE_ERROR_QUEUE -DERROR_QUEUE_PER_THREAD")
endif()
