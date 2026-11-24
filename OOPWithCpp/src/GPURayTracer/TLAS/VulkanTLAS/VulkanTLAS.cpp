//
// Created by forwardfax3 on 17/04/2026.
//

#include "VulkanTLAS.hpp"
#include "VulkanSceneMesh.hpp"
#include "VulkanBuffers.hpp"


namespace OWC
{
    VulkanTLAS::VulkanTLAS(const std::map<u32, std::unique_ptr<SceneMesh>>& meshes, const std::vector<std::pair<Mat4, u32>>& meshIndices)
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

        const auto& vkCore = VulkanCore::GetInstance();
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
            .setFlags(vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace | vk::BuildAccelerationStructureFlagBitsKHR::eAllowCompaction | vk::BuildAccelerationStructureFlagBitsKHR::eAllowDataAccess)
            .setGeometries(accelerationStructureGeometry);

        vk::AccelerationStructureBuildSizesInfoKHR buildSizes = device.getAccelerationStructureBuildSizesKHR(
            vk::AccelerationStructureBuildTypeKHR::eDevice,
            buildInfo,
            BLASInstances.size()
        );

        const vk::DeviceSize scratchBufferSize = alignUp(buildSizes.buildScratchSize, vkCore.GetAccelerationStructureProperties().minAccelerationStructureScratchOffsetAlignment);

        // create scratch data

        constexpr auto scratchBufferCreateInfoUsage = vk::BufferUsageFlags2CreateInfo()
            .setUsage(
                vk::BufferUsageFlagBits2::eShaderDeviceAddress |
                vk::BufferUsageFlagBits2::eAccelerationStructureBuildInputReadOnlyKHR |
                vk::BufferUsageFlagBits2::eAccelerationStructureStorageKHR |
                vk::BufferUsageFlagBits2::eStorageBuffer
            );

        const auto scratchDataCreateInfo = vk::BufferCreateInfo()
            .setPNext(&scratchBufferCreateInfoUsage)
            .setSize(scratchBufferSize)
            .setSharingMode(vk::SharingMode::eConcurrent)
            .setQueueFamilyIndices(queueIndices);

        const auto scratchDataAllocInfo = vma::AllocationCreateInfo()
            .setUsage(vma::MemoryUsage::eGpuOnly)
            .setFlags(vma::AllocationCreateFlagBits::eCanAlias)
            .setMinAlignment(vkCore.GetAccelerationStructureProperties().minAccelerationStructureScratchOffsetAlignment);

        auto scratchBufferResult = allocator.createBuffer(scratchDataCreateInfo, scratchDataAllocInfo);
        if (!scratchBufferResult)
            Log<LogLevel::Critical>("Failed to create scratch buffer for Vulkan TLAS. Error: {}", vk::to_string(scratchBufferResult.error()));
        const vma::raii::Buffer& scratchBuffer = scratchBufferResult.value();

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

        const vma::AllocationCreateInfo allocInfo = vma::AllocationCreateInfo()
            .setUsage(vma::MemoryUsage::eGpuOnly)
            .setFlags(vma::AllocationCreateFlagBits::eCanAlias)
            .setMinAlignment(vkCore.GetAccelerationStructureProperties().minAccelerationStructureScratchOffsetAlignment);

        auto scratchTLASBufferResult = allocator.createBuffer(bufferInfo, allocInfo);
        if (!scratchTLASBufferResult)
            Log<LogLevel::Critical>("Failed to create scratch buffer for Vulkan TLAS. Error: {}", vk::to_string(scratchTLASBufferResult.error()));
        const vma::raii::Buffer& scratchTLASBuffer = scratchTLASBufferResult.value();

        auto accelerationStructureCreateInfo = vk::AccelerationStructureCreateInfoKHR()
            .setType(vk::AccelerationStructureTypeKHR::eTopLevel)
            .setSize(buildSizes.accelerationStructureSize)
            .setBuffer(*scratchTLASBuffer);

        auto scratchTLAS = device.createAccelerationStructureKHR(accelerationStructureCreateInfo);
        if (!scratchTLAS)
            Log<LogLevel::Critical>("Failed to create scratch TLAS for Vulkan TLAS. Error: {}", vk::to_string(scratchTLAS.error()));

        OWC::Log<LogLevel::Trace>("created scratch TLAS: ID: 0x{:x}", std::bit_cast<u64>(std::bit_cast<const VkAccelerationStructureKHR>(*scratchTLAS.value())));

        buildInfo.setScratchData(vk::DeviceOrHostAddressKHR().setDeviceAddress(vkCore.GetDevice().getBufferAddress(vk::BufferDeviceAddressInfo().setBuffer(*scratchBuffer))))
            .setDstAccelerationStructure(scratchTLAS.value());

        auto cmd = vkCore.GetSingleTimeComputeCommandBuffer();
        constexpr auto barrierData = vk::MemoryBarrier2()
            .setSrcStageMask(vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR)
            .setSrcAccessMask(vk::AccessFlagBits2::eAccelerationStructureWriteKHR)
            .setDstStageMask(vk::PipelineStageFlagBits2::eRayTracingShaderKHR)
            .setDstAccessMask(vk::AccessFlagBits2::eAccelerationStructureReadKHR);

        const auto buildRangesPtr = &accelerationStructureBuildRangeInfo;

        if (const auto beginResult = cmd.begin(vk::CommandBufferBeginInfo().setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit)); !beginResult)
            Log<LogLevel::Critical>("Failed to begin Vulkan command buffer: {}", vk::to_string(beginResult.error()));
        cmd.buildAccelerationStructuresKHR(buildInfo, buildRangesPtr);
        cmd.pipelineBarrier2(vk::DependencyInfo().setMemoryBarriers(barrierData));
        if (const auto endResult = cmd.end(); !endResult)
            Log<LogLevel::Critical>("Failed to end Vulkan command buffer: {}", vk::to_string(endResult.error()));

        auto fence = device.createFence(vk::FenceCreateInfo());
        if (!fence)
            Log<LogLevel::Critical>("Failed to create fence for Vulkan TLAS build. Error: {}", vk::to_string(fence.error()));
        auto cmdSubmitInfo = vk::CommandBufferSubmitInfo().setCommandBuffer(cmd);
        if (const auto submitResult = vkCore.GetComputeQueue().submit2(vk::SubmitInfo2().setCommandBufferInfos(cmdSubmitInfo), *fence); !submitResult)
            Log<LogLevel::Critical>("Failed to submit Vulkan command buffer: {}", vk::to_string(submitResult.error()));
        if (const auto waitFenceResult = device.waitForFences(*fence.value(), VK_TRUE, std::numeric_limits<uint64_t>::max()); waitFenceResult != vk::Result::eSuccess)
            Log<LogLevel::Critical>("Failed to wait for acceleration structure build fence: {}", vk::to_string(waitFenceResult));
        
        // Skip TLAS compaction — the TLAS is tiny (~35KB) and compact-copy on AMD RX 9070 XT
        // causes GPU-AV VUID-RuntimeSpirv-OpRayQueryInitializeKHR-06352 (AS reported as "not built").
        m_TLASBuffer = std::move(scratchTLASBufferResult).value();
        m_TLAS = std::move(scratchTLAS).value();

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

    void VulkanTLAS::CreateBLASes(const std::map<u32, std::unique_ptr<SceneMesh>>& meshes)
    {
        using namespace OWC::Graphics;

        const auto& vkCore = VulkanCore::GetInstance();
        const auto& device = vkCore.GetDevice();
        const auto& allocator = vkCore.GetVulkanMemoryAllocator();
        const auto& queueIndices = vkCore.GetAllUniqueQueuesIndices();

        const uSize accelerationStructureAlignment = vkCore.GetAccelerationStructureProperties().minAccelerationStructureScratchOffsetAlignment;

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
                .setFlags(vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace | vk::BuildAccelerationStructureFlagBitsKHR::eAllowCompaction | vk::BuildAccelerationStructureFlagBitsKHR::eAllowDataAccess)
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

        const vma::AllocationCreateInfo allocInfo = vma::AllocationCreateInfo()
            .setUsage(vma::MemoryUsage::eGpuOnly)
            .setFlags(vma::AllocationCreateFlagBits::eCanAlias)
            .setMinAlignment(accelerationStructureAlignment);

        constexpr auto scratchBufferCreateInfoUsage = vk::BufferUsageFlags2CreateInfo()
            .setUsage(
                vk::BufferUsageFlagBits2::eShaderDeviceAddress |
                vk::BufferUsageFlagBits2::eAccelerationStructureBuildInputReadOnlyKHR |
                vk::BufferUsageFlagBits2::eAccelerationStructureStorageKHR |
                vk::BufferUsageFlagBits2::eStorageBuffer
            );

        const auto scratchBufferCreateInfo = vk::BufferCreateInfo()
            .setPNext(&scratchBufferCreateInfoUsage)
            .setSize(scratchBufferSize)
            .setSharingMode(vk::SharingMode::eConcurrent)
            .setQueueFamilyIndices(queueIndices);

        auto scratchBufferResult = allocator.createBuffer(scratchBufferCreateInfo, allocInfo);
        if (!scratchBufferResult)
            Log<LogLevel::Critical>("Failed to create scratch buffer for Vulkan TLAS build. Error: {}", vk::to_string(scratchBufferResult.error()));
        const auto& scratchBuffer = scratchBufferResult.value();

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

        auto scratchBLASBufferResult = allocator.createBuffer(bufferInfo, allocInfo);
        if (!scratchBLASBufferResult)
            Log<LogLevel::Critical>("Failed to create scratch buffer for Vulkan TLAS build. Error: {}", vk::to_string(scratchBLASBufferResult.error()));
        const auto& scratchBLASBuffer = scratchBLASBufferResult.value();

        vk::DeviceSize scratchBufferOffset = 0;
        vk::DeviceSize scratchBLASesOffset = 0;

        vk::DeviceAddress scratchBufferDeviceAddress = device.getBufferAddress(vk::BufferDeviceAddressInfo().setBuffer(*scratchBuffer));

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

            auto scratchBLASResult = device.createAccelerationStructureKHR(accelerationStructureCreateInfo);
            if (!scratchBLASResult)
                Log<LogLevel::Critical>("Failed to create scratch BLAS for Vulkan TLAS build. Error: {}", vk::to_string(scratchBLASResult.error()));
            scratchBLASes.emplace_back(std::move(scratchBLASResult).value());

            buildGeometryInfos.emplace_back(vk::AccelerationStructureTypeKHR::eBottomLevel,
                (vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace | vk::BuildAccelerationStructureFlagBitsKHR::eAllowCompaction | vk::BuildAccelerationStructureFlagBitsKHR::eAllowDataAccess),
                static_cast<vk::BuildAccelerationStructureModeKHR>(0),
                VK_NULL_HANDLE,
                *scratchBLASes.back(),
                geometries.size(),
                geometries.data(),
                nullptr,
                vk::DeviceOrHostAddressKHR().setDeviceAddress(scratchBufferDeviceAddress + scratchBufferOffset),
                nullptr);

            scratchBLASesOffset += alignUp(buildSizes.accelerationStructureSize, accelerationStructureAlignment);
            scratchBufferOffset += alignUp(buildSizes.buildScratchSize, vkCore.GetAccelerationStructureProperties().minAccelerationStructureScratchOffsetAlignment);

            OWC::Log<LogLevel::Trace>("created scratch BLAS: ID: 0x{:x}", std::bit_cast<u64>(std::bit_cast<const VkAccelerationStructureKHR>(*scratchBLASes.back())));

            buildRangeInfos.emplace_back(buildRanges.data());
            fullSizeBLASSize.emplace_back(buildSizes.accelerationStructureSize);
        }

        auto cmd = vkCore.GetSingleTimeComputeCommandBuffer();

        auto queryPoolResult = device.createQueryPool(vk::QueryPoolCreateInfo()
            .setQueryType(vk::QueryType::eAccelerationStructureCompactedSizeKHR)
            .setQueryCount(scratchBLASes.size()));
        if (!queryPoolResult)
            Log<LogLevel::Critical>("Failed to create query pool for Vulkan TLAS build. Error: {}", vk::to_string(queryPoolResult.error()));

        auto BLASesNonOwning = std::ranges::to<std::vector<vk::AccelerationStructureKHR>>(scratchBLASes | std::views::transform([](const auto& blas) { return *blas; }));

        if (const auto cmdBeginResult = cmd.begin(vk::CommandBufferBeginInfo().setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit)); !cmdBeginResult)
            Log<LogLevel::Critical>("Failed to begin single time compute command buffer for Vulkan TLAS build. Error: {}", vk::to_string(cmdBeginResult.error()));
        cmd.buildAccelerationStructuresKHR(buildGeometryInfos, buildRangeInfos);
        cmd.resetQueryPool(*queryPoolResult.value(), 0, buildGeometryInfos.size());
        cmd.writeAccelerationStructuresPropertiesKHR(BLASesNonOwning, vk::QueryType::eAccelerationStructureCompactedSizeKHR, *queryPoolResult.value(), 0);
        if (const auto cmdEndResult = cmd.end(); !cmdEndResult)
            Log<LogLevel::Critical>("Failed to end single time compute command buffer for Vulkan TLAS build. Error: {}", vk::to_string(cmdEndResult.error()));

        auto fence = device.createFence(vk::FenceCreateInfo());
        if (!fence)
            Log<LogLevel::Critical>("Failed to create fence for Vulkan TLAS build. Error: {}", vk::to_string(fence.error()));
        auto cmdSubmitInfo = vk::CommandBufferSubmitInfo().setCommandBuffer(cmd);
        if (const auto submitResult = vkCore.GetComputeQueue().submit2(vk::SubmitInfo2().setCommandBufferInfos(cmdSubmitInfo), *fence.value()); !submitResult)
            Log<LogLevel::Critical>("Failed to submit command buffer for Vulkan TLAS build. Error: {}", vk::to_string(submitResult.error()));
        if(device.waitForFences(*fence.value(), VK_TRUE, std::numeric_limits<uint64_t>::max()) != vk::Result::eSuccess)
            Log<LogLevel::Critical>("Failed to wait for acceleration structure build fence");

        const auto [result, sizes] = (*device).getQueryPoolResults<vk::DeviceSize>(*queryPoolResult.value(), 0, scratchBLASes.size(), sizeof(vk::DeviceSize) * scratchBLASes.size(), sizeof(vk::DeviceSize));
        if (result != vk::Result::eSuccess)
            Log<LogLevel::Critical>("Failed to get compacted size for BLASes. Error: {}", vk::to_string(result));

        vk::DeviceSize trueBLASesSize = 0;

        for (const auto size : sizes)
            trueBLASesSize += alignUp(size, accelerationStructureAlignment);

        bufferInfo.setSize(trueBLASesSize);
        auto BLASesBufferResult = allocator.createBuffer(bufferInfo, allocInfo);
        if (!BLASesBufferResult)
            Log<LogLevel::Critical>("Failed to create buffer for compacted BLASes. Error: {}", vk::to_string(BLASesBufferResult.error()));
        m_BLASesBuffer = std::move(BLASesBufferResult).value();

        m_BLASSes.reserve(meshes.size());

        vk::DeviceSize trueBLASesSizeOffset = 0;

        cmd = vkCore.GetSingleTimeComputeCommandBuffer();
        if (const auto cmdResult = cmd.begin(vk::CommandBufferBeginInfo().setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit)); !cmdResult)
            Log<LogLevel::Critical>("Failed to begin command buffer for Vulkan TLAS build. Error: {}", vk::to_string(cmdResult.error()));

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

            auto BLASResult = device.createAccelerationStructureKHR(accelerationStructureCreateInfo);
            if (!BLASResult)
                Log<LogLevel::Critical>("Failed to create BLAS for mesh {}. Error: {}", meshIndex, vk::to_string(BLASResult.error()));
            m_BLASSes.emplace_back(std::move(BLASResult).value());

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
        if (const auto cmdResult = cmd.end(); !cmdResult)
            Log<LogLevel::Critical>("Failed to end command buffer for Vulkan TLAS build. Error: {}", vk::to_string(cmdResult.error()));

        if (const auto fenceResult = device.resetFences(*fence.value()); !fenceResult)
            Log<LogLevel::Critical>("Failed to reset acceleration structure copy fence. Error: {}", vk::to_string(fenceResult.error()));
        cmdSubmitInfo = vk::CommandBufferSubmitInfo().setCommandBuffer(cmd);
        if (const auto submitResult = vkCore.GetComputeQueue().submit2(vk::SubmitInfo2().setCommandBufferInfos(cmdSubmitInfo), *fence.value()); !submitResult)
            Log<LogLevel::Critical>("Failed to submit command buffer for Vulkan TLAS build. Error: {}", vk::to_string(submitResult.error()));
        if(device.waitForFences(*fence.value(), VK_TRUE, std::numeric_limits<uint64_t>::max()) != vk::Result::eSuccess)
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
