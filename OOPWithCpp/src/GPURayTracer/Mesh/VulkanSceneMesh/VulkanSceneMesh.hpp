//
// Created by forwardfax3 on 21/04/2026.
//

#pragma once
#include "SceneMesh.hpp"
#include "VulkanUniformBuffer.hpp"

#include <tiny_gltf_v3.h>


namespace OWC
{
    class VulkanSceneMesh : public SceneMesh
    {
    public:
        VulkanSceneMesh() = delete;
        explicit VulkanSceneMesh(const tg3_model& model, i32 meshIndex, u32 customInstancesIndex, const std::unordered_map<uSize, uSize>& gltfBufferPosToIndicesOffset, uSize verticesPosOffset, const std::shared_ptr<Graphics::GeneralBuffer>& GPUBuffer, std::vector<GPUData>& GPUData, std::vector<Vec3p>& verticesPos, std::vector<VertexData>& verticesData);
        explicit VulkanSceneMesh(const aiScene& scene, const std::unordered_map<aiFace*, u64>& assimpFacePtrToGPUOffset, u32 meshIndex, uSize vertexPosOffset, u32 customInstancesIndex, const std::shared_ptr<Graphics::GeneralBuffer>& GPUBuffer, std::vector<GPUData>& GPUData, std::vector<Vec3p>& verticesPos, std::vector<VertexData>& verticesData);
        ~VulkanSceneMesh() override = default;

        VulkanSceneMesh(const VulkanSceneMesh&) = delete;
        VulkanSceneMesh(VulkanSceneMesh&&) = delete;
        VulkanSceneMesh& operator=(const VulkanSceneMesh&) = delete;
        VulkanSceneMesh& operator=(VulkanSceneMesh&&) = delete;

        //[[nodiscard]] const AttributeData& GetAttributeData(const std::string& attributeName) const override;

        [[nodiscard]] u32 GetCustomInstanceIndex() const { return m_CustomInstanceIndex; }
        //[[nodiscard]] vk::AccelerationStructureKHR GetAccelerationStructure() const { return m_AccelerationStructure; }
        //[[nodiscard]] vk::Buffer GetAccelerationStructureBuffer() const { return m_Buffer; }
        //[[nodiscard]] vk::DeviceAddress GetAccelerationStructureBufferDeviceAddress() const { return m_BufferDeviceAddress; }
        [[nodiscard]] const std::vector<vk::AccelerationStructureGeometryKHR>& GetGeometries() const { return m_Geometries; }
        [[nodiscard]] const std::vector<u32>& GetPrimitiveCount() const { return m_PrimitiveCount; }
        [[nodiscard]] const std::vector<vk::AccelerationStructureBuildRangeInfoKHR>& GetBuildRanges() const { return m_BuildRanges; }
        [[nodiscard]] const std::vector<vk::AccelerationStructureGeometryTrianglesDataKHR>& GetTriangles() const { return m_Triangles; }

    private:
        //vma::raii::Buffer m_Buffer = nullptr;
        //vk::DeviceAddress m_BufferDeviceAddress = vk::DeviceAddress();
        //vk::raii::AccelerationStructureKHR m_AccelerationStructure = nullptr;
        std::vector<vk::AccelerationStructureGeometryKHR> m_Geometries;
        std::vector<u32> m_PrimitiveCount;
        std::vector<vk::AccelerationStructureBuildRangeInfoKHR> m_BuildRanges;
        std::vector<vk::AccelerationStructureGeometryTrianglesDataKHR> m_Triangles;
        u32 m_CustomInstanceIndex;
    };
} // OWC
