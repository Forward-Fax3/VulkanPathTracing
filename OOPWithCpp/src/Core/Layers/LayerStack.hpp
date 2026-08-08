#pragma once
#include "Core.hpp"
#include <list>
#include <memory>

#include "Layer.hpp"
#include "BaseEvent.hpp"


namespace OWC
{
	class LayerStack
	{
	public:
		OWC_FORCE_INLINE LayerStack() = default;
		OWC_FORCE_INLINE ~LayerStack()
		{
			m_IsShuttingDown = true;
			m_Layers.clear();
		}

		// delete copy constructor and copy assignment operator
		LayerStack(LayerStack&) = delete;
		LayerStack& operator=(LayerStack&) = delete;

		// delete move constructor and move assignment operator
		LayerStack(LayerStack&&) = delete;
		LayerStack& operator=(LayerStack&&) = delete;

		OWC_FORCE_INLINE void PushLayer(const std::shared_ptr<Layer>& layer)
		{
			m_Layers.insert(std::next(m_Layers.begin(), m_LayerInsertIndex), layer);
			m_LayerInsertIndex++;
		}
		
		OWC_FORCE_INLINE void PopLayer(const std::shared_ptr<Layer>& layer)
		{
			if (m_IsShuttingDown)
				return;

			m_Layers.erase(std::ranges::find(m_Layers, layer));
			m_LayerInsertIndex--;
		}

		OWC_FORCE_INLINE void PushOverlay(const std::shared_ptr<Layer>& overlay)
		{
			m_Layers.emplace_back(overlay);
		}

		OWC_FORCE_INLINE void ClearLayers()
		{
			m_IsShuttingDown = true;
			m_Layers.clear();
			m_IsShuttingDown = false;
		}

		void OnUpdate() const;
		void ImGuiRender() const;
		void OnEvent(BaseEvent& event) const;

	private:
		std::list<std::shared_ptr<Layer>> m_Layers;
		i32 m_LayerInsertIndex = 0;
		bool m_IsShuttingDown = false;
	};
}
