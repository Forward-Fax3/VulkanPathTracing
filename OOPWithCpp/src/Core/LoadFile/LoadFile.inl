#include "Log.hpp"
#include <fstream>
#include <vector>
#include <csignal>


namespace OWC
{
	template<typename T>
	std::vector<T> LoadFileToBytecode(const std::string& filepath) requires(sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4)
	{
		std::ifstream fileStream(filepath, std::ios::binary | std::ios::ate);

		if (!fileStream.is_open())
		{
			std::string filePathCopy = filepath;

			for (auto& c : filePathCopy)
			{
				if (c == '\\')
					c = '/';
			}

			fileStream.open(filePathCopy, std::ios::binary | std::ios::ate);

			if (!fileStream.is_open())
			{
//				raise(SIGTRAP);

				for (size_t i = 0; i < 16; i++)
				{
					filePathCopy = "../" + filePathCopy;
					fileStream.open(filePathCopy, std::ios::binary | std::ios::ate);

					if (fileStream.is_open())
						goto FileOpened;
				}

				Log<LogLevel::Error>("LoadFileToBytecode: Failed to open file: {}", filepath);
				return {};
			}
		}

		FileOpened:

		const std::streamsize fileSize = fileStream.tellg();
		fileStream.seekg(0, std::ios::beg);

		if (fileSize % sizeof(T) != 0)
		{
			Log<LogLevel::Error>("LoadFileToBytecode: File size is not aligned with type size for file: {}", filepath);
			return {};
		}

		std::vector<T> bytecode(fileSize / sizeof(T));
		if (!fileStream.read(reinterpret_cast<char*>(bytecode.data()), fileSize))
		{
			Log<LogLevel::Error>("LoadFileToBytecode: Failed to read file: {}", filepath);
			return {};
		}

		return bytecode;
	}
}