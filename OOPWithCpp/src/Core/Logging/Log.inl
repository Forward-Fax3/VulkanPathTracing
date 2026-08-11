#pragma once
#include <iostream>
#include <utility>
#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#endif
#undef min // undefine min macro to avoid conflicts with std::numeric_limits and glm
#undef max // undefine max macro to avoid conflicts with std::numeric_limits and glm

#include "Core.hpp"


namespace OWC
{
	template<LogLevel level>
	inline void Log()
	{
		using enum LogLevel;
		if constexpr (level == NewLine)
			std::cout << '\n';
		else
			static_assert(false, "Log function called without message format string!");
	}

	template<LogLevel level, typename... Args>
	inline void Log(const std::format_string<Args...> str, Args&&... args)
	{
		using enum LogLevel;
		if constexpr (level == Trace)
		{
#if defined(_WIN32) || defined(_WIN64)
			SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), 8);
#endif
			std::cout << "[Trace]: " << std::format(str, std::forward<Args>(args)...) << '\n' << std::flush;
		}
		else if constexpr (level == Debug)
		{
#if defined(_WIN32) || defined(_WIN64)
			SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), 232);
#endif
			std::cout << "[Debug]: " << std::format(str, std::forward<Args>(args)...) << std::flush;

#if defined(_WIN32) || defined(_WIN64)
			SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), 8);
#endif
			std::cout << '\n';
		}
		else if constexpr (level == Warn)
		{
#if defined(_WIN32) || defined(_WIN64)
			SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), 14);
#endif
			std::cout << "[Warning]: " << std::format(str, std::forward<Args>(args)...) << std::flush;

#if defined(_WIN32) || defined(_WIN64)
			SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), 8);
#endif
			std::cout << '\n';
		}
		else if constexpr (level == Error)
		{
#if defined(_WIN32) || defined(_WIN64)
			SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), 13);
#endif
			std::cout << "[Error]: " << std::format(str, std::forward<Args>(args)...) << std::flush;

#if defined(_WIN32) || defined(_WIN64)
			SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), 8);
#endif
			std::cout << '\n';
		}
		else if constexpr (level == Critical)
		{
			if constexpr (!IsDistributionMode())
			{
#if defined(_WIN32) || defined(_WIN64)
				SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), 192);
#endif
				std::cout << "[Critical]: " << std::format(str, std::forward<Args>(args)...) << std::flush;

#if defined(_WIN32) || defined(_WIN64)
				SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), 8);
#endif
				std::cout << '\n';
#ifndef DIST
				OWCDebugBreak();
#endif
			}
#if defined(_WIN32) || defined(_WIN64)
			else
				MessageBoxA(nullptr, std::format(str, std::forward<Args>(args)...).c_str(), "Critical Error", MB_OK | MB_ICONERROR);
#endif
			
			exit(EXIT_FAILURE);
		}
		else if constexpr (level == NewLine)
		{
			static_assert(false, "Log function called with NewLine LogLevel but with a string this can not be done!");
		}
		else // compile-time safeguard
		{
			static_assert(false, "Unknown LogLevel!");
		}
	}
}
