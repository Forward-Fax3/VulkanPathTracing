#if defined(_WIN32) || defined(_WIN64)
#include <Windows.h>
#include <intrin.h>
#else
#include <cpuid.h>
#include <dlfcn.h>
#endif
#include <string>
#include <array>
#include <type_traits>
#include <cctype>
#include <memory>
#include <bit>
#include <optional>
#include <format>
#include <algorithm>
#include <string_view>
#include <filesystem>
#include <print>
// that's a lot of std library!!


constexpr std::u8string_view AVX512 = u8"AVX512";
constexpr std::u8string_view AVX2 = u8"AVX2";
constexpr std::u8string_view SSE4_2 = u8"SSE4_2";

#if defined(_WIN32) || defined(_WIN64)
constexpr std::u8string_view DLL = u8".dll";
#else
constexpr std::u8string_view DLL = u8".so";
#endif

#if !((defined(unix) || defined(__MINGW32__) || defined(__MINGW64__) || defined(__unix__)) || defined(_WIN32) || defined(_WIN64))
#error "Unknown platform"
#endif

constexpr std::u8string_view OOP_WITH_CPP = u8"GraphicsProgrammingWithCpp/libGraphicsProgrammingWithCpp";

using Start = void (*)(int, char**);

constexpr int leaf0 = 0;
constexpr int leaf1 = 1;
constexpr int leaf7 = 7;
constexpr int leafExtBase = -2147483648; // -2147483648 == 0x80000000
constexpr int leafExt1 = -2147483647; // -2147483647 == 0x80000001


namespace
{
	enum class ISALevel
	{
		None = 0,
		X86_64_V2,
		X86_64_V3,
		X86_64_V4
	};

	std::string_view GetISALevelName(const ISALevel level) noexcept
	{
		switch (level)
		{
		case ISALevel::None: return "None";
		case ISALevel::X86_64_V2: return "SSE4.2";
		case ISALevel::X86_64_V3: return "AVX2";
		case ISALevel::X86_64_V4: return "AVX512";
		default: return "Unknown";
		}
	}

	class DLLCloser
	{
	public:
#if defined(_WIN32)
		using Handle = HMODULE;
		void operator()(const HMODULE h) const noexcept { FreeLibrary(h); }
#else
		using Handle = void*;
		void operator()(void* h) const noexcept { dlclose(h); }
#endif
	};

	class SupportToken
	{
	public:
		SupportToken() = delete;
		explicit operator bool() const noexcept { return m_supported; }

	protected:
		explicit SupportToken(const bool supported) noexcept : m_supported(supported) {}

	private:
		bool m_supported;
	};
}

static const char* ToCharPtr(const std::u8string_view str) noexcept
{
	return reinterpret_cast<const char*>(str.data());
}

using DLLHandle = std::unique_ptr<std::remove_pointer_t<DLLCloser::Handle>, DLLCloser>;

static DLLHandle OpenDLL(const std::u8string_view path) noexcept
{
#if defined(_WIN32)
	DLLHandle dllHandle{ LoadLibraryA(ToCharPtr(path)) };
	if (!dllHandle)
	{
		std::u8string fixedPath{ path };
		std::ranges::replace(fixedPath, u8'/', u8'\\');
		std::println("Fail to Load DLL. Tring path: {}\n", ToCharPtr(fixedPath));
		dllHandle = DLLHandle{ LoadLibraryA(ToCharPtr(fixedPath)) };
	}
	return dllHandle;
#else
	DLLHandle dllHandle{ dlopen(ToCharPtr(path), RTLD_LAZY) };
	if (!dllHandle)
	{
		std::u8string fixedPath{ path };
		std::ranges::replace(fixedPath, u8'/', u8'\\');
		std::println("Fail to Load DLL. Tring path: {}\n", ToCharPtr(fixedPath));
		dllHandle = DLLHandle{ dlopen(ToCharPtr(fixedPath), RTLD_LAZY) };
	}
	return dllHandle;
#endif
}

static Start FindEntry(const DLLHandle& lib) noexcept
{
#if defined(_WIN32)
	return std::bit_cast<Start>(GetProcAddress(lib.get(), "EntryPoint"));
#else
	return std::bit_cast<Start>(dlsym(lib.get(), "EntryPoint"));
#endif
}

static uint64_t XGetBV() noexcept
{
#if defined(_MSC_VER) && !defined(__clang__)
	return _xgetbv(0);
#else
	uint32_t lo;
	uint32_t hi;
	__asm__ volatile("xgetbv" : "=a"(lo), "=d"(hi) : "c"(0));
	return (static_cast<uint64_t>(hi) << 32) | static_cast<uint64_t>(lo);
#endif
}

class X86_64_V2SupportToken;
class X86_64_V3SupportToken;
class X86_64_V4SupportToken;

static X86_64_V2SupportToken CheckX86_64_V2Support() noexcept;
static X86_64_V3SupportToken CheckX86_64_V3Support(X86_64_V2SupportToken) noexcept;
static X86_64_V4SupportToken CheckX86_64_V4Support(X86_64_V3SupportToken) noexcept;

class X86_64_V2SupportToken : public SupportToken
{
	explicit X86_64_V2SupportToken(const bool supported) noexcept : SupportToken(supported) {}
	friend X86_64_V2SupportToken CheckX86_64_V2Support() noexcept;
};

class X86_64_V3SupportToken : public SupportToken
{
	explicit X86_64_V3SupportToken(const bool supported) noexcept : SupportToken(supported) {}
	friend X86_64_V3SupportToken CheckX86_64_V3Support(X86_64_V2SupportToken) noexcept;
};

class X86_64_V4SupportToken : public SupportToken
{
	explicit X86_64_V4SupportToken(const bool supported) noexcept : SupportToken(supported) {}
	friend X86_64_V4SupportToken CheckX86_64_V4Support(X86_64_V3SupportToken) noexcept;
};

static X86_64_V4SupportToken CheckX86_64_V4Support(const X86_64_V3SupportToken x86_64_V3Support) noexcept
{
	// Check if the CPU supports x86_64_V4
	if (!x86_64_V3Support) // this makes sure that XSAVE and OSXSAVE are available
		return X86_64_V4SupportToken{ false };

	std::array<int, 4> cpuInfo{};
	__cpuidex(cpuInfo.data(), leaf7, 0);

	constexpr int EBXFeatureBits = (1 << 16) | (1 << 17) | (1 << 28) | (1 << 30) | (1 << 31);
	const bool doesHaveEBXFeatures = (cpuInfo[1] & EBXFeatureBits) == EBXFeatureBits; // checks the EBX register for AVX512F, AVX512DQ, AVX512CD, AVX512BW, AVX512VL

	constexpr int ECXfn7FeatureBits = (1 << 1) | (1 << 6);
	const bool doesHaveECXfn7Features = (cpuInfo[2] & ECXfn7FeatureBits) == ECXfn7FeatureBits; // checks the ECX register for AVX512VBMI, AVX512VBMI2. stops skylake-x's poor AVX512 implementation from running

	const uint64_t xcr0 = XGetBV();
	constexpr uint64_t XCR0_AVX512 = (1 << 1) | (1 << 2) | (1 << 5) | (1 << 6) | (1 << 7); // XMM, YMM, opmask, ZMM_Hi256, Hi16_ZMM states
	const bool OSDoesHaveAVXSupport = (xcr0 & XCR0_AVX512) == XCR0_AVX512; // checks xcr0 for AVX register support
	return X86_64_V4SupportToken{ doesHaveEBXFeatures && doesHaveECXfn7Features && OSDoesHaveAVXSupport };
}

static X86_64_V3SupportToken CheckX86_64_V3Support(const X86_64_V2SupportToken x86_64_V2Support) noexcept
{
	// early out if x86_64_v2 is not supported
	if (!x86_64_V2Support)
		return X86_64_V3SupportToken{ false };

	std::array<int, 4> cpuInfo{};
	__cpuidex(cpuInfo.data(), leaf0, 0);
	if (cpuInfo[0] < 7)
		return X86_64_V3SupportToken{ false }; // CPU does not support the required CPUID function for AVX2

	__cpuidex(cpuInfo.data(), leaf1, 0);
	constexpr int ECXfn1FeatureBits = (1 << 12) | (1 << 22) | (1 << 26) | (1 << 27) | (1 << 28) | (1 << 29); // FMA3, MOVBE, XSAVE, OSXSAVE, AVX, F16C. XSAVE and OSXSAVE are required to use AVX2 instructions, so we check for both of them here
	const bool doesHaveECXfn1Features = (cpuInfo[2] & ECXfn1FeatureBits) == ECXfn1FeatureBits; // checks the ECX register for FMA3

	if (!doesHaveECXfn1Features) // early out for missing OSXSAVE
		return X86_64_V3SupportToken{ false };

	__cpuidex(cpuInfo.data(), leaf7, 0);

	constexpr int EBXfn7FeatureBits = (1 << 3) | (1 << 5) | (1 << 8); // AVX2, BMI1, and BMI2 are all required to use AVX2 instructions, so we check for all of them here
	const bool doesHaveEBXfn7Features = (cpuInfo[1] & EBXfn7FeatureBits) == EBXfn7FeatureBits; // checks the EBX register for AVX2

	__cpuidex(cpuInfo.data(), leafExt1, 0);

	constexpr int ECXfn80000001FeatureBits = (1 << 5); // LZCNT is required for x86-64-v3
	const bool doesHaveECXfn80000001Features = (cpuInfo[2] & ECXfn80000001FeatureBits) == ECXfn80000001FeatureBits; // checks the ECX register for LZCNT

	const uint64_t xcr0 = XGetBV();
	constexpr uint64_t XCR0_AVX = (1 << 1) | (1 << 2); // XMM and YMM state
	const bool OSDoesHaveAVXSupport = (xcr0 & XCR0_AVX) == XCR0_AVX; // checks xcr0 for AVX register support
	return X86_64_V3SupportToken{ doesHaveEBXfn7Features && doesHaveECXfn80000001Features && OSDoesHaveAVXSupport};
}

static X86_64_V2SupportToken CheckX86_64_V2Support() noexcept
{
	std::array<int, 4> cpuInfo{};
	__cpuidex(cpuInfo.data(), leafExtBase, 0);
	if (static_cast<uint32_t>(cpuInfo[0]) < 0x80000001)
		return X86_64_V2SupportToken{ false }; // CPU does not support the required CPUID function for LAHF/SAHF

	__cpuidex(cpuInfo.data(), leaf1, 0);

	constexpr  int ECXFeatureBits1 = (1 << 0) | (1 << 9) | (1 << 13) | (1 << 19) | (1 << 20) | (1 << 23); // SSE3, SSSE3, cx16, SSE4.1, SSE4.2, POPCNT required for x86-64-v2
	const bool doesHaveECXfn1Features = (cpuInfo[2] & ECXFeatureBits1) == ECXFeatureBits1; // checks the ECX register for SSE4.2

	__cpuidex(cpuInfo.data(), leafExt1, 0);

	constexpr int ECXFeatureBits2 = (1 << 0); // LAHF/SAHF is required for x86-64-v2
	const bool doesHaveECXfn80000001Features = (cpuInfo[2] & ECXFeatureBits2) == ECXFeatureBits2; // checks the ECX register for LAHF/SAHF

	return X86_64_V2SupportToken{ doesHaveECXfn1Features && doesHaveECXfn80000001Features };
}

static std::optional<ISALevel> ParseOverride(const int argc, char* const* argv) noexcept
{
	auto EqualsIgnoreCase = [](const std::string_view a, const std::string_view b) noexcept
	{
		return std::ranges::equal(a, b, [](const unsigned char x, const unsigned char y)
		{
			return std::tolower(x) == std::tolower(y);
		});
	};

	for (int i = 1; i < argc; ++i)
	{
		const std::string_view arg{ argv[i] };
		if (EqualsIgnoreCase(arg, "--force-avx512"))
			return ISALevel::X86_64_V4;
		if (EqualsIgnoreCase(arg, "--force-avx2"))
			return ISALevel::X86_64_V3;
		if (EqualsIgnoreCase(arg, "--force-sse4_2"))
			return ISALevel::X86_64_V2;
	}
	return std::nullopt;
}

static ISALevel DetectISA() noexcept
{
	const auto x86_64_V2Support = CheckX86_64_V2Support();
	if (!x86_64_V2Support)
		return ISALevel::None;
	const auto x86_64_V3Support = CheckX86_64_V3Support(x86_64_V2Support);
	if (!x86_64_V3Support)
		return ISALevel::X86_64_V2;
	if (!CheckX86_64_V4Support(x86_64_V3Support))
		return ISALevel::X86_64_V3;

	return ISALevel::X86_64_V4;
}

static ISALevel ChooseISA(const int argc, char* const* argv) noexcept
{
	const ISALevel detected = DetectISA();
	if (detected == ISALevel::None) // CPU is straight up not support
		return detected;

	if (const auto forced = ParseOverride(argc, argv))
	{
		if (*forced > detected)
		{
#if defined(_WIN32) || defined(_WIN64)
			MessageBoxA(nullptr, std::format("Requested ISA override, {}, is above what your CPU supports, will fallback to your CPU's max supported, {}", GetISALevelName(*forced), GetISALevelName(detected)).c_str(), "Requested ISA not supported", MB_OK | MB_ICONWARNING);
#else
			std::println("Requested ISA override, {}, is above what your CPU supports, will fallback to your CPU's max supported, {}", GetISALevelName(*forced), GetISALevelName(detected));
#endif
		}
		return std::min(*forced, detected); // never go above what the CPU supports
	}
	return detected;
}

int main(const int argc, char** argv)
{
	std::u8string path(u8"./../");
	path += OOP_WITH_CPP;

	std::println("{}\nC++ Version {}", std::filesystem::current_path().string(), __cplusplus);

	switch (ChooseISA(argc, argv))
	{
	case ISALevel::X86_64_V4:
		path += AVX512;
		break;
	case ISALevel::X86_64_V3:
		path += AVX2;
		break;
	case ISALevel::X86_64_V2:
		path += SSE4_2;
		break;
	case ISALevel::None:
		[[fallthrough]];
	default:
	{
#if defined(_WIN32) || defined(_WIN64)
		MessageBoxA(nullptr, "CPU does not support a minimum of SSE4.2, this is required to run this application", "CPU Not Supported", MB_OK | MB_ICONERROR);
#else
		std::println("CPU does not support a minimum of SSE4.2, this is required to run this application");
#endif
		return 1;
	}
	}

	path += DLL;
	std::println("Loading DLL from path: {}\n", ToCharPtr(path));
	const auto dll = OpenDLL(path);

	if (dll == nullptr)
	{
#if defined(_WIN32) || defined(_WIN64)
		std::array<wchar_t, 1024> error{};
		FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, GetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_SYS_DEFAULT), error.data(), static_cast<DWORD>(error.size()), nullptr);
		MessageBoxW(nullptr, error.data(), L"Failed to load DLL", MB_OK | MB_ICONERROR);
#else
		const auto e = dlerror();
		std::println("Failed to load DLL: {}", e ? e : "unknown error");
#endif
		return 2;
	}

	const auto EntryPoint = FindEntry(dll);

	if (EntryPoint == nullptr)
	{
#if defined(_WIN32) || defined(_WIN64)
		std::array<wchar_t, 1024> error{};
		FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, GetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_SYS_DEFAULT), error.data(), static_cast<DWORD>(error.size()), nullptr);
		MessageBoxW(nullptr, error.data(), L"Failed to load entry point", MB_OK | MB_ICONERROR);
#else
		const auto e = dlerror();
		std::println("Failed to load entry point: {}", e ? e : "unknown error");
#endif
		return 3;
	}

	EntryPoint(argc, argv);
}
