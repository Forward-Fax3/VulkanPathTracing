#if defined(_WIN32) || defined(_WIN64)
#include <Windows.h>
#include <intrin.h>
#else
#include <cpuid.h>
#include <dlfcn.h>
#endif
#include <string>
#include <iostream>
#include <fstream>
#include <array>

#include <filesystem>
#include <print>


#define AVX512 u8"AVX512"
#define AVX2 u8"AVX2"
#define SSE4_2 u8"SSE4_2"

#if defined(_WIN32) || defined(_WIN64)
#define DLL u8".dll"
#else
#define DLL u8".so"
#endif

#if defined(unix) || defined(__MINGW32__ ) || defined(__MINGW64__) || defined(__unix__) || (defined(__APPLE__) && defined(__MACH__))
#define OOP_WITH_CPP u8"GraphicsProgrammingWithCpp/libGraphicsProgrammingWithCpp"
#else
#define OOP_WITH_CPP u8"GraphicsProgrammingWithCpp\\GraphicsProgrammingWithCpp"
#endif

using Start = void (*)(int, char**);


static char* ToCharPtr(const std::u8string& str)
{
	return std::bit_cast<char*>(str.c_str());
}

template <size_t N>
static char* ToCharPtr(const std::array<char8_t, N>& arr)
{
	return std::bit_cast<char*>(arr.data());
}

static bool AVX512Supported()
{
	// Check if the CPU supports AVX512
	std::array<int, 4> cpuInfo{};
	__cpuidex(cpuInfo.data(), 7, 0);

	int EBXFeatureBits = (1 << 16) | (1 << 17) | (1 << 28) | (1 << 30) | (1 << 31);
	bool doesHaveEBXFeatures = (cpuInfo[1] & EBXFeatureBits) == EBXFeatureBits; // checks the EBX register for AVX512F, AVX512DQ, AVX512CD, AVX512BW, AVX512VL

	int ECXFeatureBits = (1 << 1) | (1 << 6);
	bool doesHaveECXFeatures = (cpuInfo[2] & ECXFeatureBits) == ECXFeatureBits; // checks the ECX register for AVX512VBMI, AVX512VBMI2

	return doesHaveEBXFeatures && doesHaveECXFeatures;
}

static bool AVX2AndFMA3Supported()
{
	std::array<int, 4> cpuInfo{};
	__cpuidex(cpuInfo.data(), 7, 0);

	int EBXfn7FeatureBits = (1 << 3) | (1 << 5) | (1 << 8); // AVX2, BMI1, and BMI2 are all required to use AVX2 instructions, so we check for all of them here
	bool doesHaveEBXfn7Features = (cpuInfo[1] & EBXfn7FeatureBits) == EBXfn7FeatureBits; // checks the EBX register for AVX2

	__cpuidex(cpuInfo.data(), 1, 0);

	int ECXfn1FeatureBits = (1 << 12) | (1 << 22) | (1 << 26) | (1 << 27) | (1 << 28) | (1 << 29); // FMA3, MOVBE, XSAVE, OSXSAVE, AVX, and XSAVE are all required to use AVX2 instructions, so we check for all of them here
	bool doesHaveECXfn1Features = (cpuInfo[2] & ECXfn1FeatureBits) == ECXfn1FeatureBits; // checks the ECX register for FMA3

	__cpuidex(cpuInfo.data(), 0x80000001, 0);

	int ECXfn80000001FeatureBits = (1 << 5); // LZCNT is required for x86-64-v3
	bool doesHaveECXfn80000001Features = (cpuInfo[2] & ECXfn80000001FeatureBits) == ECXfn80000001FeatureBits; // checks the ECX register for LZCNT

	return doesHaveEBXfn7Features && doesHaveECXfn1Features && doesHaveECXfn80000001Features;
}

static bool SSE4_2Supported()
{
	std::array<int, 4> cpuInfo{};

	__cpuidex(cpuInfo.data(), 1, 0);

	int ECXFeatureBits = (1 << 0) | (1 << 9) | (1 << 13) | (1 << 19) | (1 << 20) | (1 << 23); // SSE3, SSSE3, cx16, SSE4.1, SSE4.2 required for x86-64-v2
	bool doesHaveECXfn1Features = (cpuInfo[2] & ECXFeatureBits) == ECXFeatureBits; // checks the ECX register for SSE4.2


	__cpuidex(cpuInfo.data(), 0x80000001, 0);

	ECXFeatureBits = (1 << 0); //; LAHF/SAHF is required for x86-64-v2
	bool doesHaveECXfn80000001Features = (cpuInfo[2] & ECXFeatureBits) == ECXFeatureBits; // checks the ECX register for LAHF/SAHF

	return doesHaveECXfn1Features && doesHaveECXfn80000001Features;
}

int main(int argc, char** argv)
{
	std::u8string path(u8"./../" OOP_WITH_CPP);

	std::println("{}", std::filesystem::current_path().string());

	if (AVX512Supported())
		path += AVX512;
	else if (AVX2AndFMA3Supported())
		path += AVX2;
	else if (SSE4_2Supported()) // SEE4_2
		path += SSE4_2;
	else
	{
#if defined(_WIN32) || defined(_WIN64)
		MessageBoxA(nullptr, "CPU does not support a minimum of SSE4.2, this is required to run this application", "CPU Not Supported", MB_OK | MB_ICONERROR);
#endif
		return 1;
	}

	path += DLL;
	std::println("Loading DLL from path: {}\n", ToCharPtr(path));
#if defined(_WIN32) || defined(_WIN64)
	HMODULE dll = LoadLibraryA(ToCharPtr(path));
#else
	void* dll = dlopen(ToCharPtr(path), RTLD_LAZY);
#endif

	if (dll == nullptr)
	{
#if defined(_WIN32) || defined(_WIN64)
		std::array<char8_t, 1024> error{};
		FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, nullptr, GetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_SYS_DEFAULT), ToCharPtr(error), 1024, nullptr);
		MessageBoxA(nullptr, "Failed to load DLL", ToCharPtr(error), MB_OK | MB_ICONERROR);
#else
	std::println("Failed to load DLL: {}", dlerror());
#endif
		return 2;
	}

#if defined(_WIN32) || defined(_WIN64)
	const auto EntryPoint = std::bit_cast<Start>(GetProcAddress(dll, "EntryPoint"));
#else
	const auto EntryPoint = std::bit_cast<Start>(dlsym(dll, "EntryPoint"));
#endif

	if (EntryPoint == nullptr)
	{
#if defined(_WIN32) || defined(_WIN64)
		std::array<char8_t, 1024> error{};
		FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, nullptr, GetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_SYS_DEFAULT), ToCharPtr(error), 1024, nullptr);
		MessageBoxA(nullptr, "Failed to load entry point", ToCharPtr(error), MB_OK | MB_ICONERROR);
#endif
		return 3;
	}

	EntryPoint(argc, argv);

#if defined(_WIN32) || defined(_WIN64)
	FreeLibrary(dll);
#else
	dlclose(dll);
#endif
}
