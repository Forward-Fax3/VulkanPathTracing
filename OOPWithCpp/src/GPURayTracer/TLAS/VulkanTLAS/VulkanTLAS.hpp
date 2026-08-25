//
// Created by forwardfax3 on 17/04/2026.
//

#pragma once
#include "BaseTLAS.hpp"
#include "VulkanUniformBuffer.hpp"

#include "vulkan/vulkan.hpp"


namespace OWC
{
    class VulkanTLAS : public BaseTLAS
    {
    public:
        VulkanTLAS(const std::map<u32, std::unique_ptr<SceneMesh>>& meshes, const std::vector<std::pair<Mat4, u32>>& meshIndices);
        ~VulkanTLAS() override = default;

        VulkanTLAS(const VulkanTLAS&) = delete;
        VulkanTLAS(VulkanTLAS&&) = delete;
        VulkanTLAS& operator=(const VulkanTLAS&) = delete;
        VulkanTLAS& operator=(VulkanTLAS&&) = delete;

        [[nodiscard]] const vk::raii::AccelerationStructureKHR& GetTLAS() const { return m_TLAS; }

    private:
        void CreateBLASes(const std::map<u32, std::unique_ptr<SceneMesh>>& meshes);
        void CreateTLAS(const std::vector<vk::AccelerationStructureInstanceKHR>& meshIndices);

        [[nodiscard]] static vk::TransformMatrixKHR ConvertMat4ToVulkanTransform(const Mat4& transform);

    private:
        vma::raii::Buffer m_BLASesBuffer = nullptr;
        std::vector<vk::raii::AccelerationStructureKHR> m_BLASSes;
        vma::raii::Buffer m_TLASBuffer = nullptr;
        vk::raii::AccelerationStructureKHR m_TLAS = nullptr;
    };
}
