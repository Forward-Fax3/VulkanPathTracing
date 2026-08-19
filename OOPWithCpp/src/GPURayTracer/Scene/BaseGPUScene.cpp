//
// Created by forwardfax3 on 16/04/2026.
//

#include "BaseGPUScene.hpp"


namespace OWC
{
    std::weak_ptr<Graphics::TextureArraySampler> BaseGPUScene::s_EmptyTextureArraySampler;
    std::weak_ptr<Graphics::TextureArray> BaseGPUScene::s_EmptyTextureArray;

    BaseGPUScene::BaseGPUScene()
    {
        if (s_EmptyTextureArray.expired())
        {
            m_EmptyTextureArraySampler = Graphics::TextureArraySampler::CreateEmptyTextureArraySampler();
            s_EmptyTextureArraySampler = m_EmptyTextureArraySampler;
            m_EmptyTextureArray = Graphics::TextureArray::CreateEmptyTextureArray();
            s_EmptyTextureArray = m_EmptyTextureArray;
        }
        else
        {
            m_EmptyTextureArraySampler = s_EmptyTextureArraySampler.lock();
            m_EmptyTextureArray = s_EmptyTextureArray.lock();
        }
    }
}
