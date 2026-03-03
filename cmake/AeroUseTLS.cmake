include_guard(GLOBAL)

include(AeroOptions)
include(CheckIPOSupported)

function(_aero_enable_wolfssl_openssl_compatibility interface_target)
  if(DEFINED wolfssl_SOURCE_DIR)
    # Add the openssl include directory to be able to use it as: "#include <openssl/...>"
    target_include_directories(${interface_target} INTERFACE "${wolfssl_SOURCE_DIR}/wolfssl")
    return()
  endif()

  if(TARGET wolfssl::wolfssl)
    get_target_property(wolfssl_interface_include_directories wolfssl::wolfssl INTERFACE_INCLUDE_DIRECTORIES)

    if(wolfssl_interface_include_directories)
      foreach(wolfssl_include_directory IN LISTS wolfssl_interface_include_directories)
        if(wolfssl_include_directory MATCHES "\\$<")
          continue()
        endif()

        if(EXISTS "${wolfssl_include_directory}/wolfssl/openssl/ssl.h")
          target_include_directories(${interface_target} INTERFACE "${wolfssl_include_directory}/wolfssl")
          return()
        endif()

        if(EXISTS "${wolfssl_include_directory}/openssl/ssl.h")
          target_include_directories(${interface_target} INTERFACE "${wolfssl_include_directory}")
          return()
        endif()
      endforeach()
    endif()
  endif()
endfunction()

function(_aero_try_enable_wolfssl tls_target out_found)
  if(NOT TARGET wolfssl::wolfssl)
    set(CMAKE_FIND_PACKAGE_PREFER_CONFIG TRUE)
    find_package(wolfssl CONFIG QUIET)
  endif()

  if(TARGET wolfssl::wolfssl)
    message(STATUS "[AERO-TLS] Using system WolfSSL (wolfssl::wolfssl)")
    target_link_libraries(${tls_target} INTERFACE wolfssl::wolfssl)
    _aero_enable_wolfssl_openssl_compatibility(${tls_target})
    target_compile_definitions(${tls_target} INTERFACE
      AERO_USE_WOLFSSL
      ASIO_USE_WOLFSSL
      WOLFSSL_OPTIONS_H
      OPENSSL_VERSION_NUMBER=0x10101000
    )
    set(${out_found} TRUE PARENT_SCOPE)
  else()
    set(${out_found} FALSE PARENT_SCOPE)
  endif()
endfunction()

function(_aero_try_enable_openssl tls_target out_found)
  if(NOT TARGET OpenSSL::SSL)
    find_package(OpenSSL QUIET)
  endif()

  if(TARGET OpenSSL::SSL)
    message(STATUS "[AERO-TLS] Using OpenSSL (OpenSSL::SSL)")
    target_link_libraries(${tls_target} INTERFACE OpenSSL::SSL OpenSSL::Crypto)
    target_compile_definitions(${tls_target} INTERFACE AERO_USE_OPENSSL)
    set(${out_found} TRUE PARENT_SCOPE)
  else()
    set(${out_found} FALSE PARENT_SCOPE)
  endif()
endfunction()

function(aero_use_tls_library dependent_target)
  if(AERO_TLS_BACKEND STREQUAL "none")
    message(FATAL_ERROR "AERO_TLS_BACKEND is 'none' but aero_use_tls_library() was called")
  endif()

  if(NOT TARGET aero_tls_library)
    add_library(aero_tls_library INTERFACE)
  endif()

  set(AERO_USE_TLS ON CACHE BOOL "" FORCE)
  target_compile_definitions(aero_tls_library INTERFACE AERO_USE_TLS)

  if(AERO_TLS_BACKEND STREQUAL "openssl")
    _aero_try_enable_openssl(aero_tls_library openssl_found)
    if(NOT openssl_found)
      message(FATAL_ERROR "OpenSSL backend selected but OpenSSL was not found (OpenSSL::SSL missing)")
    endif()

    target_compile_definitions(${dependent_target} INTERFACE AERO_USE_OPENSSL)
  elseif(AERO_TLS_BACKEND STREQUAL "wolfssl")
    _aero_try_enable_wolfssl(aero_tls_library wolfssl_found)    
    if(NOT wolfssl_found)
      message(FATAL_ERROR "WolfSSL backend selected but wolfssl::wolfssl was not found")
    endif()
  else()
    message(FATAL_ERROR "Unknown AERO_TLS_BACKEND value: ${AERO_TLS_BACKEND}")
  endif()

  target_link_libraries(${dependent_target} INTERFACE aero_tls_library)
endfunction()
