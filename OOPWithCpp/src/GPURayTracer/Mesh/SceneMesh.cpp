//
// Created by forwardfax3 on 15/04/2026.
//

#include "SceneMesh.hpp"
#include "VulkanSceneMesh.hpp"


namespace OWC
{
    std::unique_ptr<SceneMesh> SceneMesh::CreateFromGLTFModelWithMeshIndex(const tg3_model& gltfMesh, i32 meshIndex, u32 customInstancesIndex, const std::unordered_map<uSize, uSize>& gltfBufferPosToIndicesOffset, uSize verticesBufferOffset, const std::shared_ptr<Graphics::GeneralBuffer>& GPUBuffer, std::vector<GPUData>& GPUData, std::vector<Vec3p>& verticesPos, std::vector<VertexData>& verticesData)
    {
        // only vulkan supported at the moment
        return std::make_unique<VulkanSceneMesh>(gltfMesh, meshIndex, customInstancesIndex, gltfBufferPosToIndicesOffset, verticesBufferOffset, GPUBuffer, GPUData, verticesPos, verticesData);
    }

    std::unique_ptr<SceneMesh> SceneMesh::CreateFromAssimpSceneWithMeshIndex(const aiScene& scene, const std::unordered_map<aiFace*, u64>& assimpFacePtrToGPUOffset, u32 meshIndex, uSize vertexDataOffset, u32 customInstancesIndex, const std::shared_ptr<Graphics::GeneralBuffer>& GPUBuffer, std::vector<GPUData>& GPUData, std::vector<Vec3p>& verticesPos, std::vector<VertexData>& verticesData)
    {
        // only vulkan supported at the moment
        return std::make_unique<VulkanSceneMesh>(scene, assimpFacePtrToGPUOffset, meshIndex, vertexDataOffset, customInstancesIndex, GPUBuffer, GPUData, verticesPos, verticesData);
    }
} // OWC