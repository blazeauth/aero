#pragma once

#ifndef __has_cpp_attribute
#define AERO_LIFETIMEBOUND
#elif __has_cpp_attribute(msvc::lifetimebound)
#define AERO_LIFETIMEBOUND [[msvc::lifetimebound]]
#elif __has_cpp_attribute(clang::lifetimebound)
#define AERO_LIFETIMEBOUND [[clang::lifetimebound]]
#else
#define AERO_LIFETIMEBOUND
#endif
