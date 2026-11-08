#pragma once
#include <vector>
#include <string>
#include <optional>


namespace OWC
{
	template<typename T>
	std::optional<std::vector<T>> LoadFileToBytecode(const std::string& filepath, bool maybeAbsolute = false);
}

#include "LoadFile.inl"
