#include "Camera.hpp"
#include "Ray.hpp"
#include "OWCRand.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/euler_angles.hpp>

#include <ranges>
#include <algorithm>


namespace OWC
{
	RTCamera::~RTCamera()
	{
		m_EndThreads = true;

		for (const ThreadData& renderThreadData : m_RenderThreadsData)
			while (!renderThreadData.IsFinished)
				std::this_thread::yield();
	}

	RenderPassReturnData RTCamera::SingleThreadedRenderPass(const std::shared_ptr<BaseHitable>& hittables)
	{
		if (m_SingleThreadedModeNeedsSetup)
		{
			m_SingleThreadedModeNeedsSetup = false;
			m_MultiThreadedModeNeedsSetup = true;

			// yield all threads before destroying their data
			m_EndThreads = true;
			for (const ThreadData& renderThreadData : m_RenderThreadsData)
				while (!renderThreadData.IsFinished)
					std::this_thread::yield();

			m_RenderThreadsData.clear();
			m_RenderThreads.clear();
			m_EndThreads = false;
			m_RenderThreadsData.resize(1);
			m_RenderThreads.reserve(1);
			ThreadData& renderThreadData = m_RenderThreadsData[0];
			m_RenderThreads.emplace_back(
				[this](ThreadData& data)
				{
					ThreadedRenderPass(data);
				},
				std::ref(m_RenderThreadsData[0])
			);
			renderThreadData.StartIndex = 0;
			renderThreadData.Step = 1;
			renderThreadData.Hittables = hittables;
			m_RenderThreads[0].detach();
			UpdateCameraSettings();
		}
		// guaranteed to be only be one thread data in single threaded mode
		else if (m_RenderThreadsData[0].IsFinished)
		{
			std::ranges::move(m_SampleAccumulationBuffer, m_Pixels.begin());
			m_RenderThreadsData[0].Hittables = hittables;
			m_RenderThreadsData[0].IsFinished = false;
			return true;
		}

		return false;
	}

	OWC::RenderPassReturnData RTCamera::MultiThreadedRenderPass(const std::shared_ptr<BaseHitable>& hittables)
	{
		if (m_MultiThreadedModeNeedsSetup)
		{
			m_MultiThreadedModeNeedsSetup = false;
			m_SingleThreadedModeNeedsSetup = true;

			// yield all threads before destroying their data
			m_EndThreads = true;
			for (const ThreadData& renderThreadData : m_RenderThreadsData)
				while (!renderThreadData.IsFinished)
					std::this_thread::yield();

			m_RenderThreadsData.clear();
			m_RenderThreads.clear();
			m_EndThreads = false;
			const uSize threadCount = std::thread::hardware_concurrency() - 1; // leave one core free for main thread
			m_RenderThreadsData.resize(threadCount);
			m_RenderThreads.reserve(threadCount);
			for (uSize i = 0; i != threadCount; i++)
			{
				m_RenderThreads.emplace_back(
					[this](ThreadData& data)
					{
						ThreadedRenderPass(data);
					},
					std::ref(m_RenderThreadsData[i])
				);

				ThreadData& renderThreadData = m_RenderThreadsData[i];
				renderThreadData.StartIndex = i;
				renderThreadData.Step = threadCount;
				renderThreadData.Hittables = hittables;
			}
			for (std::jthread& thread : m_RenderThreads)
				thread.detach();
			UpdateCameraSettings();
		}
		else
		{
			bool notAllFinished = false; // reduce number of comparisons
			for (const ThreadData& renderThreadData : m_RenderThreadsData)
				notAllFinished |= !renderThreadData.IsFinished;

			if (notAllFinished)
				return false;

			std::ranges::move(m_SampleAccumulationBuffer, m_Pixels.begin());
			for (ThreadData& renderThreadData : m_RenderThreadsData)
			{
				renderThreadData.Hittables = hittables;
				renderThreadData.IsFinished = false;
			}
			return true;
		}
		return false;
	}

	void RTCamera::UpdateCameraSettings()
	{
		for (const ThreadData& data : m_RenderThreadsData)
			while (!data.IsFinished)
				std::this_thread::yield();

		m_SampleAccumulationBuffer.resize(m_Pixels.size());
		for (Colour& pixel : m_SampleAccumulationBuffer)
			pixel = Colour(0.0f);

		m_ActiveMaxBounces = m_Settings.MaxBounces;
		m_BouncedColours.resize((m_ActiveMaxBounces + 2) * m_RenderThreadsData.size()); // +1 for lost ray colour and +1 for low bounce count working correctly

		Vec3 rotationInRadians = glm::radians(m_Settings.Rotation);
		Mat4 rotationMatrix = glm::eulerAngleYXZ(rotationInRadians.y, rotationInRadians.x, rotationInRadians.z);
		Vec3 forward = glm::normalize(Vec3(rotationMatrix * Vec4(0.0f, 0.0f, -1.0f, 0.0f)));
		Vec3 right = glm::normalize(Vec3(rotationMatrix * Vec4(1.0f, 0.0, 0.0, 0.0)));
		Vec3 up = glm::normalize(Vec3(rotationMatrix * Vec4(0.0f, -1.0, 0.0, 0.0)));

		f32 aspectRatio = m_Settings.ScreenSize.x / m_Settings.ScreenSize.y;
		f32 viewportHeight = 2.0f * m_Settings.FocalLength * glm::tan(glm::radians(m_Settings.FOV) * 0.5f);
		f32 viewportWidth = aspectRatio * viewportHeight;

		Point viewportU = viewportWidth * right;
		Point viewportV = viewportHeight * -up;

		m_PixelDeltaU = viewportU / m_Settings.ScreenSize.x;
		m_PixelDeltaV = viewportV / m_Settings.ScreenSize.y;

		Point viewportUpperLeft = m_Settings.Position - (m_Settings.FocalLength * forward) - 0.5f * (viewportU + viewportV);
		m_Pixel100Location = viewportUpperLeft + 0.5f * (m_PixelDeltaU + m_PixelDeltaV);
	}

	Ray RTCamera::CreateRay(uSize i, uSize j) const
	{
		Vec2 randomOffset = Rand::LinearFastRandVec2(Vec2(0.0f), Vec2(1.0f)) + Vec2(j, i);
		Vec3 rayDirection = m_Pixel100Location +
			(randomOffset.x * m_PixelDeltaU) +
			(randomOffset.y * m_PixelDeltaV);

		return Ray{ m_Settings.Position, rayDirection };
	}

	Colour RTCamera::RayColour(Ray& ray, size_t bouncedColoursOffset, const std::shared_ptr<BaseHitable>& hittables)
	{
		bool missed = false;
		bool scattered = true;
		i32 i = 0;

		for (; i != m_ActiveMaxBounces + 1 && scattered; i++)
		{
			HitData hitData;
			Interval tRange(0.001f, std::numeric_limits<f32>::max());
			bool hasHit = hittables->IsHit(ray, tRange, hitData);
			if (!hasHit)
			{
				m_BouncedColours[bouncedColoursOffset + i][0] = Colour(0.0f);
				m_BouncedColours[bouncedColoursOffset + i][1] = hittables->BackgroundColour(ray);
				missed = true;
				break;
			}

			m_BouncedColours[bouncedColoursOffset + i][0] = hitData.material->Albedo(hitData);
			m_BouncedColours[bouncedColoursOffset + i][1] = hitData.material->Emitted(ray, hitData);
			scattered = hitData.material->Scatter(ray, hitData);
		}

		if (i == m_ActiveMaxBounces + 1)
		{
			m_BouncedColours[bouncedColoursOffset + m_ActiveMaxBounces + 1] = ColourX2(0.0f);
			missed = true;
		}

		if (missed)
			i++;

		Colour finalColour(0.0f);
		while (i != 0)
		{
			i--;
			finalColour *= m_BouncedColours[bouncedColoursOffset + i][0];
			finalColour += m_BouncedColours[bouncedColoursOffset + i][1];
		}

		return finalColour;
	}

	void RTCamera::ThreadedRenderPass(ThreadData& data)
	{
		while (true)
		{
			data.IsFinished = true;
			while (data.IsFinished && !m_EndThreads) { OWC_NO_OP; }
				//std::this_thread::yield();

			if (m_EndThreads)
				break;

			uSize bouncedColoursOffset = (m_ActiveMaxBounces + 2) * data.StartIndex; // reuse start index for offset
			Vec2us screenSize(m_Settings.ScreenSize);

			for (uSize pixel = data.StartIndex; pixel < screenSize.x * screenSize.y; pixel += data.Step)
				for (i32 sample = 0; sample != m_Settings.NumberOfSamplesPerPass && !m_EndThreads; sample++)
				{
					Ray ray = CreateRay(pixel / screenSize.x, pixel % screenSize.x);

					m_SampleAccumulationBuffer[pixel] += RayColour(ray, bouncedColoursOffset, data.Hittables);
				}
		}
	}
}
