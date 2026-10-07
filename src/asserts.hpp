#pragma once

#include <cstdlib>
#include <print>

#ifdef DEBUG
#define DEBUG_ASSERT(expr)                                  \
	do                                                      \
	{                                                       \
		if (!(expr))                                        \
		{                                                   \
			std::println("DEBUG_ASSERT failed: {}", #expr); \
			std::exit(EXIT_FAILURE);                        \
		}                                                   \
	} while (false)
#define DEBUG_ASSERT_FMT(expr, fmt, ...)      \
	do                                        \
	{                                         \
		if (!(expr))                          \
		{                                     \
			std::println(fmt, ##__VA_ARGS__); \
			std::exit(EXIT_FAILURE);          \
		}                                     \
	} while (false)
#else
#define DEBUG_ASSERT(expr) ((void)0)
#define DEBUG_ASSERT_FMT(expr, fmt, ...) ((void)0)
#endif

#define RELEASE_ASSERT(expr)                                  \
	do                                                        \
	{                                                         \
		if (!(expr))                                          \
		{                                                     \
			std::println("RELEASE_ASSERT failed: {}", #expr); \
			std::exit(EXIT_FAILURE);                          \
		}                                                     \
	} while (false)
#define RELEASE_ASSERT_FMT(expr, fmt, ...)    \
	do                                        \
	{                                         \
		if (!(expr))                          \
		{                                     \
			std::println(fmt, ##__VA_ARGS__); \
			std::exit(EXIT_FAILURE);          \
		}                                     \
	} while (false)
