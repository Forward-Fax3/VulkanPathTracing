#pragma once
#include "Core.hpp"

#include <format>


namespace OWC
{
	enum class LogLevel : u8
	{
		NewLine,
		Trace,
		Debug,
		Warn,
		Error,
		Critical
	};

	template<LogLevel level, typename... Args>
	void Log(std::format_string<Args...> str, Args&&... args);
}

#include "Log.inl"
