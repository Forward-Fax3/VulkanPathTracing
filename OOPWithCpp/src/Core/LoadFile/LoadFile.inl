#pragma once
#include "Log.hpp"
#include <filesystem>
#include <fstream>
#include <vector>
#include <optional>
#include <ranges>


namespace OWC
{
	template<typename T>
	requires(sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8) &&
		std::is_trivially_copyable_v<T> &&
		std::is_same_v<T, std::remove_cv_t<T>>
	std::optional<std::vector<T>> LoadFileToBytecode(const std::string& filepath, const bool maybeAbsolute = false)
	{
		std::ifstream fileStream;
		namespace fs = std::filesystem;

		std::string normalized = filepath;
		std::ranges::replace(normalized, '\\', '/');

		std::error_code ec;

		if (const fs::path relativePath = normalized; relativePath.is_absolute())
		{
			if (fs::is_regular_file(relativePath, ec))
			{
				if (!maybeAbsolute)
					Log<LogLevel::Warn>("LoadFileToBytecode: Absolute file path detected: {}. Consider using a relative path.", filepath);
				fileStream.open(relativePath, std::ios::binary | std::ios::ate);
			}
			else if (ec)
			{
				Log<LogLevel::Error>("LoadFileToBytecode: Error while checking if absolute file path exists: {}. Error: {}", filepath, ec.message());
				return std::nullopt;
			}
			else
			{
				Log<LogLevel::Error>("LoadFileToBytecode: Absolute file path does not exist or is not a regular file: {}", filepath);
				return std::nullopt;
			}
		}
		else
		{
			fs::path dir = fs::current_path(ec);
			if (ec)
			{
				Log<LogLevel::Error>("LoadFileToBytecode: current_path failed: {}", ec.message());
				return std::nullopt;
			}

			std::error_code statEc;
			while (true)
			{
				if (const auto candidate = dir / relativePath; fs::is_regular_file(candidate, statEc))
				{
					fileStream.open(candidate, std::ios::binary | std::ios::ate);
					if (fileStream.is_open())
						break;
				}
				else if (statEc)
					Log<LogLevel::Trace>("LoadFileToBytecode: Error while checking if candidate file exists: {}. Error: {}", candidate.string(), statEc.message());

				fs::path parent = dir.parent_path();
				if (parent == dir) // reached the root
					break;

				dir = std::move(parent);
			}
		}

		if (!fileStream.is_open())
		{
			Log<LogLevel::Error>("LoadFileToBytecode: Failed to open file: {}", filepath);
			return std::nullopt;
		}

		const std::streamsize fileSize = fileStream.tellg();
		if (fileSize < 0)
		{
			Log<LogLevel::Error>("LoadFileToBytecode: Failed to determine file size for file: {}", filepath);
			return std::nullopt;
		}

		fileStream.seekg(0, std::ios::beg);
		const auto byteCount = static_cast<size_t>(fileSize);

		if (byteCount % sizeof(T) != 0)
		{
			Log<LogLevel::Error>("LoadFileToBytecode: File size is not aligned with type size for file: {}", filepath);
			return std::nullopt;
		}

		std::vector<T> bytecode(byteCount / sizeof(T));
		if (!fileStream.read(reinterpret_cast<char*>(bytecode.data()), fileSize))
		{
			Log<LogLevel::Error>("LoadFileToBytecode: Failed to read file: {}", filepath);
			return std::nullopt;
		}

		return bytecode;
	}
}