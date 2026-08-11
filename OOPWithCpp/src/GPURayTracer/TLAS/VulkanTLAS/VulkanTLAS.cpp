//
// Created by forwardfax3 on 17/04/2026.
//

#include "VulkanTLAS.hpp"
#include "VulkanSceneMesh.hpp"
#include "VulkanUniformBuffer.hpp"


namespace OWC
{
    VulkanTLAS::VulkanTLAS(const std::map<i32, std::unique_ptr<SceneMesh>>& meshes, const std::vector<std::pair<Mat4, i32>>& meshIndices)
    {
        CreateBLASes(meshes);

        const auto& device = Graphics::VulkanCore::GetInstance().GetDevice();
        const auto blasInstances = std::ranges::to<std::vector<vk::AccelerationStructureInstanceKHR>>(meshIndices | std::views::transform([this, &meshes, &device](const std::pair<Mat4, i32>& meshIndexPair) -> vk::AccelerationStructureInstanceKHR
        {
            return {
                ConvertMat4ToVulkanTransform(meshIndexPair.first),
                dynamic_cast<VulkanSceneMesh*>(meshes.at(meshIndexPair.second).get())->GetCustomInstanceIndex(),
                0xFF,
                0,
                vk::GeometryInstanceFlagBitsKHR::eTriangleCullDisable,
                device.getAccelerationStructureAddressKHR(vk::AccelerationStructureDeviceAddressInfoKHR().setAccelerationStructure(*this->m_BLASSes.at(meshIndexPair.second)))
            };
        }));

        CreateTLAS(blasInstances);
    }

    void VulkanTLAS::CreateTLAS(const std::vector<vk::AccelerationStructureInstanceKHR>& BLASInstances)
    {
        using namespace OWC::Graphics;

        const auto& vkCore = VulkanCore::GetConstInstance();
        const auto& device = vkCore.GetDevice();
        const auto& allocator = vkCore.GetVulkanMemoryAllocator();
        const auto& queueIndices = vkCore.GetAllUniqueQueuesIndices();

        VulkanGeneralBuffer buffer(BLASInstances.size() * sizeof(vk::AccelerationStructureInstanceKHR));
        buffer.UpdateBufferData(std::bit_cast<const u8*>(BLASInstances.data()));

        auto accelerationStructureInstanceData = vk::AccelerationStructureGeometryInstancesDataKHR()
            .setData(vk::DeviceOrHostAddressConstKHR().setDeviceAddress(buffer.GetBufferDeviceAddress()));

        auto accelerationStructureGeometry = vk::AccelerationStructureGeometryKHR()
            .setGeometry(accelerationStructureInstanceData)
            .setGeometryType(vk::GeometryTypeKHR::eInstances);

        auto accelerationStructureBuildRangeInfo = vk::AccelerationStructureBuildRangeInfoKHR()
            .setPrimitiveCount(static_cast<u32>(BLASInstances.size()));

        auto buildInfo = vk::AccelerationStructureBuildGeometryInfoKHR()
            .setMode(vk::BuildAccelerationStructureModeKHR::eBuild)
            .setType(vk::AccelerationStructureTypeKHR::eTopLevel)
            .setFlags(vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace | vk::BuildAccelerationStructureFlagBitsKHR::eAllowCompaction | vk::BuildAccelerationStructureFlagBitsKHR::eLowMemory)
            .setGeometries(accelerationStructureGeometry);

        vk::AccelerationStructureBuildSizesInfoKHR buildSizes = device.getAccelerationStructureBuildSizesKHR(
            vk::AccelerationStructureBuildTypeKHR::eDevice,
            buildInfo,
            BLASInstances.size()
        );

        const vk::DeviceSize scratchBufferSize = alignUp(buildSizes.buildScratchSize, vkCore.GetAccelerationStructureProperties().minAccelerationStructureScratchOffsetAlignment);

        VulkanGeneralBuffer scratchBuffer(scratchBufferSize);

        constexpr auto bufferUsageInfo2 = vk::BufferUsageFlags2CreateInfo()
            .setUsage(
                vk::BufferUsageFlagBits2::eShaderDeviceAddress |
                vk::BufferUsageFlagBits2::eAccelerationStructureBuildInputReadOnlyKHR |
                vk::BufferUsageFlagBits2::eAccelerationStructureStorageKHR
            );

        auto bufferInfo = vk::BufferCreateInfo()
            .setPNext(&bufferUsageInfo2)
            .setSize(buildSizes.accelerationStructureSize)
            .setSharingMode(vk::SharingMode::eConcurrent)
            .setQueueFamilyIndices(queueIndices);

        constexpr vma::AllocationCreateInfo allocInfo = vma::AllocationCreateInfo()
            .setUsage(vma::MemoryUsage::eGpuOnly)
            .setFlags(vma::AllocationCreateFlagBits::eDedicatedMemory);

        vma::raii::Buffer scratchTLASBuffer(allocator, bufferInfo, allocInfo);

        auto accelerationStructureCreateInfo = vk::AccelerationStructureCreateInfoKHR()
            .setType(vk::AccelerationStructureTypeKHR::eTopLevel)
            .setSize(buildSizes.accelerationStructureSize)
            .setBuffer(*scratchTLASBuffer);

        auto scratchTLAS = device.createAccelerationStructureKHR(accelerationStructureCreateInfo);

        OWC::Log<LogLevel::Trace>("created scratch TLAS: ID: 0x{:x}", std::bit_cast<u64>(std::bit_cast<const VkAccelerationStructureKHR>(*scratchTLAS)));

        buildInfo.setScratchData(vk::DeviceOrHostAddressKHR().setDeviceAddress(scratchBuffer.GetBufferDeviceAddress()))
            .setDstAccelerationStructure(scratchTLAS);

        auto cmd = vkCore.GetSingleTimeComputeCommandBuffer();
        constexpr auto barrierData = vk::MemoryBarrier2()
            .setSrcStageMask(vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR)
            .setSrcAccessMask(vk::AccessFlagBits2::eAccelerationStructureWriteKHR)
            .setDstStageMask(vk::PipelineStageFlagBits2::eRayTracingShaderKHR)
            .setDstAccessMask(vk::AccessFlagBits2::eAccelerationStructureReadKHR);

        const auto buildRangesPtr = &accelerationStructureBuildRangeInfo;

        cmd.begin(vk::CommandBufferBeginInfo().setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
        cmd.buildAccelerationStructuresKHR(buildInfo, buildRangesPtr);
        cmd.pipelineBarrier2(vk::DependencyInfo().setMemoryBarriers(barrierData));
        cmd.end();
        
        auto fence = device.createFence(vk::FenceCreateInfo());
        auto cmdSubmitInfo = vk::CommandBufferSubmitInfo().setCommandBuffer(cmd);
        vkCore.GetComputeQueue().submit2(vk::SubmitInfo2().setCommandBufferInfos(cmdSubmitInfo), *fence);
        if (device.waitForFences(*fence, VK_TRUE, std::numeric_limits<uint64_t>::max()) != vk::Result::eSuccess)
            Log<LogLevel::Critical>("Failed to wait for acceleration structure build fence");
        
        // Skip TLAS compaction — the TLAS is tiny (~35KB) and compact-copy on AMD RX 9070 XT
        // causes GPU-AV VUID-RuntimeSpirv-OpRayQueryInitializeKHR-06352 (AS reported as "not built").
        m_TLASBuffer = std::move(scratchTLASBuffer);
        m_TLAS = std::move(scratchTLAS);

        //// TLAS compaction (commented out — see above)
        // vk::raii::QueryPool queryPool(device, vk::QueryPoolCreateInfo()
        // 	.setQueryType(vk::QueryType::eAccelerationStructureCompactedSizeKHR)
        // 	.setQueryCount(1));
        //
        // cmd.resetQueryPool(*queryPool, 0, 1);
        // cmd.writeAccelerationStructuresPropertiesKHR(*scratchTLAS, vk::QueryType::eAccelerationStructureCompactedSizeKHR, *queryPool, 0);
        // cmd.pipelineBarrier2(vk::DependencyInfo().setMemoryBarriers(barrierData));
        // cmd.end();
        //
        // auto fence = device.createFence(vk::FenceCreateInfo());
        // auto cmdSubmitInfo = vk::CommandBufferSubmitInfo().setCommandBuffer(cmd);
        // vkCore.GetComputeQueue().submit2(vk::SubmitInfo2().setCommandBufferInfos(cmdSubmitInfo), *fence);
        // if (device.waitForFences(*fence, VK_TRUE, std::numeric_limits<uint64_t>::max()) != vk::Result::eSuccess)
        //     Log<LogLevel::Critical>("Failed to wait for acceleration structure build fence");
        //
        // const auto [result, sizes] = (*device).getQueryPoolResults<vk::DeviceSize>(
        // 	*queryPool, 0, 1, sizeof(vk::DeviceSize), sizeof(vk::DeviceSize));
        //
        // if (result != vk::Result::eSuccess)
        // {
        // 	Log<LogLevel::Error>(
        // 		"Failed to get query pool results for TLAS compaction size, will use originally created TLAS");
        // 	m_TLASBuffer = std::move(scratchTLASBuffer);
        // 	m_TLAS = std::move(scratchTLAS);
        // 	return;
        // }
        //
        // Log<LogLevel::Trace>("TLAS starting size {}, compaction size: {}", buildSizes.accelerationStructureSize, sizes[0]);
        //
        // bufferInfo.setSize(sizes[0]);
        // m_TLASBuffer = vma::raii::Buffer(allocator, bufferInfo, allocInfo);
        // accelerationStructureCreateInfo.setSize(sizes[0]).setBuffer(*m_TLASBuffer);
        // m_TLAS = device.createAccelerationStructureKHR(accelerationStructureCreateInfo);
        //
        // OWC::Log<LogLevel::Trace>("created compacted TLAS: ID: 0x{:x}", std::bit_cast<u64>(std::bit_cast<const VkAccelerationStructureKHR>(*(m_TLAS))));
        //
        // const auto copyAccelerationStructureInfo = vk::CopyAccelerationStructureInfoKHR()
        // 	.setSrc(scratchTLAS)
        // 	.setDst(m_TLAS)
        // 	.setMode(vk::CopyAccelerationStructureModeKHR::eCompact);
        //
        // // vkCmdCopyAccelerationStructuresKHR executes in the COPY pipeline stage, not BUILD.
        // // GPU-AV tracks AS build state per pipeline stage — using eAccelerationStructureBuildKHR
        // // as the source stage here causes GPU-AV to not recognize the compacted destination AS
        // // as "built", triggering VUID-RuntimeSpirv-OpRayQueryInitializeKHR-06352 on AMD drivers.
        // constexpr auto barrierDataCopy = vk::MemoryBarrier2()
        // 	.setSrcStageMask(vk::PipelineStageFlagBits2::eAccelerationStructureCopyKHR)
        // 	.setSrcAccessMask(vk::AccessFlagBits2::eAccelerationStructureWriteKHR)
        // 	.setDstStageMask(vk::PipelineStageFlagBits2::eRayTracingShaderKHR)
        // 	.setDstAccessMask(vk::AccessFlagBits2::eAccelerationStructureReadKHR);
        //
        // cmd = vkCore.GetSingleTimeComputeCommandBuffer();
        // cmd.begin(vk::CommandBufferBeginInfo().setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
        // cmd.copyAccelerationStructureKHR(copyAccelerationStructureInfo);
        // cmd.pipelineBarrier2(vk::DependencyInfo().setMemoryBarriers(barrierDataCopy));
        // cmd.end();
        //
        // device.resetFences(*fence);
        // cmdSubmitInfo = vk::CommandBufferSubmitInfo().setCommandBuffer(cmd);
        // vkCore.GetComputeQueue().submit2(vk::SubmitInfo2().setCommandBufferInfos(cmdSubmitInfo), *fence);
        // if (device.waitForFences(*fence, VK_TRUE, std::numeric_limits<uint64_t>::max()) != vk::Result::eSuccess)
        // 	Log<LogLevel::Critical>("Failed to wait for acceleration structure copy fence");
    }

    void VulkanTLAS::CreateBLASes(const std::map<i32, std::unique_ptr<SceneMesh>>& meshes)
    {
        using namespace OWC::Graphics;

        const auto& vkCore = VulkanCore::GetInstance();
        const auto& device = vkCore.GetDevice();
        const auto& allocator = vkCore.GetVulkanMemoryAllocator();
        const auto& queueIndices = vkCore.GetAllUniqueQueuesIndices();

        constexpr uSize accelerationStructureAlignment = 256;

        std::vector<vk::AccelerationStructureBuildSizesInfoKHR> buildSizesList;
        std::vector<vk::raii::AccelerationStructureKHR> scratchBLASes;
        std::vector<vk::AccelerationStructureBuildGeometryInfoKHR> buildGeometryInfos;
        std::vector<const vk::AccelerationStructureBuildRangeInfoKHR*> buildRangeInfos;

        buildSizesList.reserve(meshes.size());
        scratchBLASes.reserve(meshes.size());
        buildGeometryInfos.reserve(meshes.size());
        buildRangeInfos.reserve(meshes.size());

        vk::DeviceSize scratchBufferSize = 0;
        vk::DeviceSize scratchBLASesSize = 0;

        for (const auto& mesh : std::views::values(meshes))
        {
            const auto& vulkanMesh = *dynamic_cast<VulkanSceneMesh*>(mesh.get());
            const auto& geometries = vulkanMesh.GetGeometries();
            const auto& primitiveCount = vulkanMesh.GetPrimitiveCount();

            auto buildInfo = vk::AccelerationStructureBuildGeometryInfoKHR()
                .setMode(vk::BuildAccelerationStructureModeKHR::eBuild)
                .setType(vk::AccelerationStructureTypeKHR::eBottomLevel)
                .setFlags(vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace | vk::BuildAccelerationStructureFlagBitsKHR::eAllowCompaction | vk::BuildAccelerationStructureFlagBitsKHR::eLowMemory)
                .setGeometries(geometries);

            const auto buildSizes = device.getAccelerationStructureBuildSizesKHR(
                vk::AccelerationStructureBuildTypeKHR::eDevice,
                buildInfo,
                primitiveCount
            );

            scratchBufferSize += alignUp(buildSizes.buildScratchSize, vkCore.GetAccelerationStructureProperties().minAccelerationStructureScratchOffsetAlignment);
            scratchBLASesSize += alignUp(buildSizes.accelerationStructureSize, accelerationStructureAlignment);
            buildSizesList.emplace_back(buildSizes);
        }

        VulkanGeneralBuffer scratchBuffer(scratchBufferSize);

        constexpr auto bufferUsageInfo2 = vk::BufferUsageFlags2CreateInfo()
            .setUsage(
                vk::BufferUsageFlagBits2::eShaderDeviceAddress |
                vk::BufferUsageFlagBits2::eAccelerationStructureBuildInputReadOnlyKHR |
                vk::BufferUsageFlagBits2::eAccelerationStructureStorageKHR
            );

        auto bufferInfo = vk::BufferCreateInfo()
            .setPNext(&bufferUsageInfo2)
            .setSize(scratchBLASesSize)
            .setSharingMode(vk::SharingMode::eConcurrent)
            .setQueueFamilyIndices(queueIndices);

        constexpr vma::AllocationCreateInfo allocInfo = vma::AllocationCreateInfo()
            .setUsage(vma::MemoryUsage::eGpuOnly)
            .setFlags(vma::AllocationCreateFlagBits::eDedicatedMemory);

        auto scratchBLASBuffer = vma::raii::Buffer(allocator, bufferInfo, allocInfo);

        vk::DeviceSize scratchBufferOffset = 0;
        vk::DeviceSize scratchBLASesOffset = 0;

        std::vector<vk::DeviceSize> fullSizeBLASSize;
        fullSizeBLASSize.reserve(meshes.size());

        for (uSize i = 0; i < meshes.size(); i++)
        {
            const auto meshIndex = static_cast<i32>(i);
            const auto& vulkanMesh = *dynamic_cast<VulkanSceneMesh*>(meshes.at(meshIndex).get());
            const auto& geometries = vulkanMesh.GetGeometries();
            const auto& buildRanges = vulkanMesh.GetBuildRanges();
            const auto& buildSizes = buildSizesList.at(i);

            auto accelerationStructureCreateInfo = vk::AccelerationStructureCreateInfoKHR()
                .setType(vk::AccelerationStructureTypeKHR::eBottomLevel)
                .setSize(buildSizes.accelerationStructureSize)
                .setBuffer(scratchBLASBuffer)
                .setOffset(scratchBLASesOffset);

            scratchBLASes.emplace_back(device.createAccelerationStructureKHR(accelerationStructureCreateInfo));

            buildGeometryInfos.emplace_back(vk::AccelerationStructureTypeKHR::eBottomLevel,
                (vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace | vk::BuildAccelerationStructureFlagBitsKHR::eAllowCompaction | vk::BuildAccelerationStructureFlagBitsKHR::eLowMemory),
                static_cast<vk::BuildAccelerationStructureModeKHR>(0),
                VK_NULL_HANDLE,
                *scratchBLASes.back(),
                geometries.size(),
                geometries.data(),
                nullptr,
                vk::DeviceOrHostAddressKHR().setDeviceAddress(scratchBuffer.GetBufferDeviceAddress() + scratchBufferOffset),
                nullptr);

            scratchBLASesOffset += alignUp(buildSizes.accelerationStructureSize, accelerationStructureAlignment);
            scratchBufferOffset += alignUp(buildSizes.buildScratchSize, vkCore.GetAccelerationStructureProperties().minAccelerationStructureScratchOffsetAlignment);

            OWC::Log<LogLevel::Trace>("created scratch BLAS: ID: 0x{:x}", std::bit_cast<u64>(std::bit_cast<const VkAccelerationStructureKHR>(*scratchBLASes.back())));

            buildRangeInfos.emplace_back(buildRanges.data());
            fullSizeBLASSize.emplace_back(buildSizes.accelerationStructureSize);
        }

        auto cmd = vkCore.GetSingleTimeComputeCommandBuffer();

        vk::raii::QueryPool queryPool(device, vk::QueryPoolCreateInfo()
            .setQueryType(vk::QueryType::eAccelerationStructureCompactedSizeKHR)
            .setQueryCount(scratchBLASes.size()));

        auto BLASesNonOwning = std::ranges::to<std::vector<vk::AccelerationStructureKHR>>(scratchBLASes | std::views::transform([](const auto& blas) { return *blas; }));

        cmd.begin(vk::CommandBufferBeginInfo().setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
        cmd.buildAccelerationStructuresKHR(buildGeometryInfos, buildRangeInfos);
        cmd.resetQueryPool(*queryPool, 0, buildGeometryInfos.size());
        cmd.writeAccelerationStructuresPropertiesKHR(BLASesNonOwning, vk::QueryType::eAccelerationStructureCompactedSizeKHR, *queryPool, 0);
        cmd.end();

        auto fence = device.createFence(vk::FenceCreateInfo());
        auto cmdSubmitInfo = vk::CommandBufferSubmitInfo().setCommandBuffer(cmd);
        vkCore.GetComputeQueue().submit2(vk::SubmitInfo2().setCommandBufferInfos(cmdSubmitInfo), *fence);
        if(device.waitForFences(*fence, VK_TRUE, std::numeric_limits<uint64_t>::max()) != vk::Result::eSuccess)
            Log<LogLevel::Critical>("Failed to wait for acceleration structure build fence");

        const auto [result, sizes] = (*device).getQueryPoolResults<vk::DeviceSize>(*queryPool, 0, scratchBLASes.size(), sizeof(vk::DeviceSize) * scratchBLASes.size(), sizeof(vk::DeviceSize));
        if (result != vk::Result::eSuccess)
            Log<LogLevel::Critical>("Failed to get compacted size for BLASes");

        vk::DeviceSize trueBLASesSize = 0;

        for (const auto size : sizes)
            trueBLASesSize += alignUp(size, accelerationStructureAlignment);

        bufferInfo.setSize(trueBLASesSize);
        m_BLASesBuffer = vma::raii::Buffer(allocator, bufferInfo, allocInfo);

        m_BLASSes.reserve(meshes.size());

        vk::DeviceSize trueBLASesSizeOffset = 0;

        cmd = vkCore.GetSingleTimeComputeCommandBuffer();
        cmd.begin(vk::CommandBufferBeginInfo().setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

        for (uSize i = 0; i < meshes.size(); i++)
        {
            const auto meshIndex = static_cast<i32>(i);
            Log<LogLevel::Trace>("Mesh {} BLAS starting size {}, compaction size: {}", meshIndex, fullSizeBLASSize[i], sizes[i]);

            const auto accelerationStructureCreateInfo = vk::AccelerationStructureCreateInfoKHR()
                .setSize(sizes[i])
                .setBuffer(m_BLASesBuffer)
                .setOffset(trueBLASesSizeOffset)
                .setType(vk::AccelerationStructureTypeKHR::eBottomLevel);

            trueBLASesSizeOffset += alignUp(sizes[i], accelerationStructureAlignment);

            m_BLASSes.emplace_back(device.createAccelerationStructureKHR(accelerationStructureCreateInfo));

            vk::CopyAccelerationStructureInfoKHR copyAccelerationStructureInfo(
                *scratchBLASes[i],
                *m_BLASSes.back(),
                vk::CopyAccelerationStructureModeKHR::eCompact);

            OWC::Log<LogLevel::Trace>("created compacted BLAS: ID: 0x{:x}", std::bit_cast<u64>(std::bit_cast<const VkAccelerationStructureKHR>(*(m_BLASSes.back()))));

            cmd.copyAccelerationStructureKHR(copyAccelerationStructureInfo);
        }

        // vkCmdCopyAccelerationStructuresKHR executes in the COPY pipeline stage, not BUILD.
        // GPU-AV tracks AS build state per pipeline stage — using the wrong source stage
        // causes GPU-AV to not recognize the compacted destination AS as "built".
        constexpr auto barrierDataCopy = vk::MemoryBarrier2()
            .setSrcStageMask(vk::PipelineStageFlagBits2::eAccelerationStructureCopyKHR)
            .setSrcAccessMask(vk::AccessFlagBits2::eAccelerationStructureWriteKHR)
            .setDstStageMask(vk::PipelineStageFlagBits2::eRayTracingShaderKHR)
            .setDstAccessMask(vk::AccessFlagBits2::eAccelerationStructureReadKHR);

        cmd.pipelineBarrier2(vk::DependencyInfo().setMemoryBarriers(barrierDataCopy));
        cmd.end();

        device.resetFences(*fence);
        cmdSubmitInfo = vk::CommandBufferSubmitInfo().setCommandBuffer(cmd);
        vkCore.GetComputeQueue().submit2(vk::SubmitInfo2().setCommandBufferInfos(cmdSubmitInfo), *fence);
        if(device.waitForFences(*fence, VK_TRUE, std::numeric_limits<uint64_t>::max()) != vk::Result::eSuccess)
            Log<LogLevel::Critical>("Failed to wait for acceleration structure copy fence");
    }

    vk::TransformMatrixKHR VulkanTLAS::ConvertMat4ToVulkanTransform(const Mat4& transform)
    {
        static_assert(sizeof(Mat4) >= sizeof(vk::TransformMatrixKHR), "Mat4 must be at least as large as vk::TransformMatrixKHR to safely copy the data.");
        static_assert(std::is_same_v<Mat4::value_type, std::remove_cvref_t<decltype(vk::TransformMatrixKHR::matrix[0][0])>>, "Value types of Mat4 and vk::TransformMatrixKHR are not compatible.");

        // Vulkan expects row-major order, so we need to transpose the matrix
        const Mat4 transposed = glm::transpose(transform);

        vk::TransformMatrixKHR vulkanTransform;
        std::memcpy(&vulkanTransform.matrix, &transposed, sizeof(vk::TransformMatrixKHR));
        return vulkanTransform;
    }
}
