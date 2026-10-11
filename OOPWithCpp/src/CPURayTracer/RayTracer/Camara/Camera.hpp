#pragma once
#include "immintrin.h"
#include "Core.hpp"
#include "Ray.hpp"
#include "BaseHittable.hpp"

#include <vector>
#include <thread>
#include <memory>


namespace OWC
{
	using RenderPassReturnData = bool;

	struct CameraRenderSettings
	{
		Vec3 Position{ 0.0f };
		Vec3 Rotation{ 0.0f };

		Vec2 ScreenSize{ 800.0f, 600.0f };

		f32 FOV = 30.0f;
		f32 FocalLength = 5.0f;
		Vec3 Vup{ 0.0f, 1.0f, 0.0f };

		i32 NumberOfSamplesPerPass = 1;
		i32 MaxBounces = 16;
	};

	class RTCamera
	{
	private:
		struct ThreadData
		{
			uSize StartIndex = 0;
			uSize Step = 0;
			std::shared_ptr<BaseHitable> Hittables = nullptr;
			bool IsFinished = false;
		};

	public:
		RTCamera() = delete;
		explicit RTCamera(std::vector<Colour>& pixels) : m_Pixels(pixels) {}
		~RTCamera();

		RTCamera(const RTCamera&) = delete;
		RTCamera& operator=(const RTCamera&) = delete;
		RTCamera(RTCamera&&) = delete;
		RTCamera& operator=(RTCamera&&) = delete;

		OWC_FORCE_INLINE CameraRenderSettings& GetSettings() { return m_Settings; }

		RenderPassReturnData SingleThreadedRenderPass(const std::shared_ptr<BaseHitable>& hittables);
		RenderPassReturnData MultiThreadedRenderPass(const std::shared_ptr<BaseHitable>& hittables);

		void UpdateCameraSettings();

	private:
		Ray CreateRay(uSize i, uSize j) const;

		Colour RayColour(Ray& ray, size_t bouncedColoursOffset, const std::shared_ptr<BaseHitable>& hittables);

		void ThreadedRenderPass(ThreadData& data);

	private:
		CameraRenderSettings m_Settings;

		Point m_Pixel100Location = Point(0.0f);
		Vec3 m_PixelDeltaU = Vec3(0.0f);
		Vec3 m_PixelDeltaV = Vec3(0.0f);

		std::vector<ThreadData> m_RenderThreadsData;
		std::vector<std::jthread> m_RenderThreads;

		std::vector<Colour>& m_Pixels;
		std::vector<Colour> m_SampleAccumulationBuffer;
		std::vector<ColourX2> m_BouncedColours;
		i32 m_ActiveMaxBounces = 0;
		bool m_EndThreads = false;
		bool m_SingleThreadedModeNeedsSetup = true;
		bool m_MultiThreadedModeNeedsSetup = true;
	};
}
