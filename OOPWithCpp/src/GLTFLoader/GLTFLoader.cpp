//
// Created by forwardfax3 on 17/04/2026.
//


#include "GLTFLoader.hpp"
#include "Application.hpp"
#include "Log.hpp"

#include "SceneMesh.hpp"
#include "GLTFLayer.hpp"
#include "GLTFLoaderHelper.hpp"
#include <tiny_gltf_v3.h>
#include <filesystem>
#include <thread>

#include "glm/ext/matrix_transform.hpp"
#include "glm/gtc/type_ptr.hpp"


namespace OWC
{
    GLTFLoader::GLTFLoader(const std::string_view gltfFilePath, const std::string_view layerName)
    {
        tg3_parse_options options;
        tg3_parse_options_init(&options);

        tg3_error_stack errorStack;
        errorStack.has_error = false; // explicitly set this to false
        tg3_model model;
        tg3_parse_file(&model, &errorStack, gltfFilePath.data(), gltfFilePath.size(), &options);

        if (errorStack.has_error)
        {
            for (u32 i = 0; i < errorStack.count; i++)
                switch (const tg3_error_entry& e = errorStack.entries[i]; e.severity)
                {
                case TG3_SEVERITY_INFO:
                    Log<LogLevel::Trace>("TinyGLTF: {}", e.message);
                    break;
                case TG3_SEVERITY_WARNING:
                    Log<LogLevel::Warn>("TinyGLTF: {}", e.message);
                    break;
                case TG3_SEVERITY_ERROR:
                    Log<LogLevel::Error>("TinyGLTF: {}", e.message);
                    break;
                default:
                    Log<LogLevel::Error>("Unknown TinyGLTF message: {}", e.message);
                    break;
                }
            return;
        }

        const auto sceneIndex = model.default_scene == -1 ? 0 : model.default_scene;
        const auto& scene = model.scenes[sceneIndex];

        uSize totalNumOfPrimitives = 0;
        uSize totalNumOfVertices = 0;
        uSize totalSizeOfIndices = 0;
        for (uSize i = 0; i < model.meshes_count; i++)
        {
            auto& mesh = model.meshes[i];
            totalNumOfPrimitives += mesh.primitives_count;
            for (uSize j = 0; j < mesh.primitives_count; j++)
            {
                auto& prim = mesh.primitives[j];

                const auto pos = extractAttribute(prim, "POSITION", model);
                const auto indices = model.accessors[prim.indices];

                totalNumOfVertices += pos.count;
                totalSizeOfIndices += indices.count * (indices.component_type == TG3_COMPONENT_TYPE_UNSIGNED_SHORT ? sizeof(u16) : sizeof(u32));
            }
        }

        std::unordered_map<uSize, uSize> gltfBufferPosToIndicesOffset;
        std::vector<std::byte> indicesTotalData;
        indicesTotalData.resize(totalSizeOfIndices);
        uSize indicesOffset = 0;
        for (uSize i = 0; i < model.meshes_count; i++)
        {
            auto& mesh = model.meshes[i];
            for (uSize j = 0; j < mesh.primitives_count; j++)
            {
                auto& prim = mesh.primitives[j];
                const auto indices = model.accessors[prim.indices];

                const auto indicesData = extractAttribute(indices, model);
                gltfBufferPosToIndicesOffset[indicesData.offset] = indicesOffset;
                uSize sizeOfIndices = indices.count * (indices.component_type == TG3_COMPONENT_TYPE_UNSIGNED_SHORT ? sizeof(u16) : sizeof(u32));
                std::memcpy(indicesTotalData.data() + indicesOffset, model.buffers[indicesData.bufferIndex].data.data + indicesData.offset, sizeOfIndices);
                indicesOffset += sizeOfIndices;
            }
        }

        u32 numberOfLights = 0;
        for (u32 i = 0; i < scene.nodes_count; i++)
            FindNumberOfLightsInScene(model, scene.nodes[i], numberOfLights);

        std::vector<VertexData> verticesData;
        std::vector<Vec3p> verticesPos;
        verticesData.reserve(totalNumOfVertices);
        verticesPos.reserve(totalNumOfVertices);
        const uSize verticesDataSize = totalNumOfVertices * sizeof(VertexData);
        const uSize verticesPosDataSize = totalNumOfVertices * sizeof(Vec3p);
        
        std::vector<GPUData> GPUDatas;
        GPUDatas.reserve(totalNumOfPrimitives);

        const uSize LightBufferSize = numberOfLights * sizeof(GPULightData);
        const uSize geometryBufferSize = totalNumOfPrimitives * sizeof(GPUData);
        const uSize materialBufferSize = model.materials_count * sizeof(GPUMaterialData);
        constexpr uSize indicesBufferOffset = 0;
        const uSize verticesPosBufferOffset = alignUp(indicesBufferOffset + totalSizeOfIndices, alignof(Vec3p));
        const uSize verticesBufferOffset = alignUp(verticesPosBufferOffset + verticesPosDataSize, alignof(VertexData));
        m_LightBufferOffset = alignUp(verticesBufferOffset + verticesDataSize, alignof(GPULightData));
        m_GeometryBufferOffset = alignUp(m_LightBufferOffset + LightBufferSize, alignof(GPUData));
        m_MaterialBufferOffset = alignUp(m_GeometryBufferOffset + geometryBufferSize, alignof(GPUMaterialData));

        constexpr uSize maxAlignment = std::max({ alignof(Vec3p), alignof(VertexData), alignof(GPULightData), alignof(GPUData), alignof(GPUMaterialData) });

        const auto bufferSize = m_MaterialBufferOffset + materialBufferSize;
        m_GPUBuffer = Graphics::GeneralBuffer::CreateGeneralBuffer(bufferSize, maxAlignment);
        m_GPUBuffer->UpdateBufferData(std::bit_cast<const u8*>(indicesTotalData.data()), totalSizeOfIndices, indicesBufferOffset);

        bool colourThreadDone = true;
        bool normalThreadDone = true;
        bool metallicRoughnessThreadDone = true;
        bool emissiveTextureThreadDone = true;

        if (model.materials_count > 0)
        {
            std::vector<u32> colourTexturesOrderIndex;
            std::vector<u32> normalTexturesOrderIndex;
            std::vector<u32> metallicRoughnessTexturesOrderIndex;
            std::vector<u32> emissiveTexturesOrderIndex;

            for (uSize i = 0; i < model.materials_count; i++)
            {
                const tg3_material& mat = model.materials[i];

                if (mat.pbr_metallic_roughness.base_color_texture.index != -1 && std::ranges::find(colourTexturesOrderIndex, model.textures[mat.pbr_metallic_roughness.base_color_texture.index].source) == colourTexturesOrderIndex.end())
                    colourTexturesOrderIndex.emplace_back(model.textures[mat.pbr_metallic_roughness.base_color_texture.index].source);
                if (mat.pbr_metallic_roughness.metallic_roughness_texture.index != -1 && std::ranges::find(metallicRoughnessTexturesOrderIndex, model.textures[mat.pbr_metallic_roughness.metallic_roughness_texture.index].source) == metallicRoughnessTexturesOrderIndex.end())
                    metallicRoughnessTexturesOrderIndex.emplace_back(model.textures[mat.pbr_metallic_roughness.metallic_roughness_texture.index].source);
                if (mat.normal_texture.index != -1 && std::ranges::find(normalTexturesOrderIndex, model.textures[mat.normal_texture.index].source) == normalTexturesOrderIndex.end())
                    normalTexturesOrderIndex.emplace_back(model.textures[mat.normal_texture.index].source);
                if (mat.emissive_texture.index != -1 && std::ranges::find(emissiveTexturesOrderIndex, model.textures[mat.emissive_texture.index].source) == emissiveTexturesOrderIndex.end())
                    emissiveTexturesOrderIndex.emplace_back(model.textures[mat.emissive_texture.index].source);
            }

            std::string materialDataToPrint = "\n\n";
            std::vector<GPUMaterialData> materialData;
            materialData.reserve(model.materials_count);
            for (uSize i = 0; i < model.materials_count; i++)
            {
                const tg3_material& mat = model.materials[i];

                materialDataToPrint += std::format("\tMaterial Data:\n"
                        "\t\tMetallic Roughness Base Colour Factor: {}, {}, {}, {}\n"
                        "\t\tMetallic Roughness Metallic Factor {}\n"
                        "\t\tMetallic Roughness Roughness Factor {}\n"
                        "\t\tMetallic Roughness Base Colour Texture Index: original {}, true {}, coords {}\n"
                        "\t\tMetallic Roughness Base Matallic Roughness Texture Index: original {}, true {}, coords {}\n"
                        "\t\tEmissive Factor: {}, {}, {}\n"
                        "\t\tNormal Texture Index: original {}, true {}, coords {}\n"
                        "\t\tEmissive Texture Index: original {}, true {}, coords {}\n"
                        "\t\tAlpha Cutoff: {}\n\n",
                        mat.pbr_metallic_roughness.base_color_factor[0],
                        mat.pbr_metallic_roughness.base_color_factor[1],
                        mat.pbr_metallic_roughness.base_color_factor[2],
                        mat.pbr_metallic_roughness.base_color_factor[3],
                        mat.pbr_metallic_roughness.metallic_factor,
                        mat.pbr_metallic_roughness.roughness_factor,
                        mat.pbr_metallic_roughness.base_color_texture.index,
                        mat.pbr_metallic_roughness.base_color_texture.index != -1 ? std::distance(colourTexturesOrderIndex.begin(), std::ranges::find(colourTexturesOrderIndex, mat.pbr_metallic_roughness.base_color_texture.index)) : -1,
                        mat.pbr_metallic_roughness.base_color_texture.tex_coord,
                        mat.pbr_metallic_roughness.metallic_roughness_texture.index,
                        mat.pbr_metallic_roughness.metallic_roughness_texture.index != -1 ? std::distance(metallicRoughnessTexturesOrderIndex.begin(), std::ranges::find(metallicRoughnessTexturesOrderIndex, mat.pbr_metallic_roughness.metallic_roughness_texture.index)) : -1,
                        mat.pbr_metallic_roughness.metallic_roughness_texture.tex_coord,
                        mat.emissive_factor[0],
                        mat.emissive_factor[1],
                        mat.emissive_factor[2],
                        mat.normal_texture.index,
                        (mat.normal_texture.index != -1 ? std::distance(normalTexturesOrderIndex.begin(), std::ranges::find(normalTexturesOrderIndex, mat.normal_texture.index)) : -1),
                        mat.normal_texture.tex_coord,
                        mat.emissive_texture.index,
                        (mat.emissive_texture.index != -1 ? std::distance(emissiveTexturesOrderIndex.begin(), std::ranges::find(emissiveTexturesOrderIndex, mat.emissive_texture.index)) : -1),
                        mat.emissive_texture.tex_coord,
                        mat.alpha_cutoff
                    );

                materialData.emplace_back(
                    Vec4p(glm::make_vec4(mat.pbr_metallic_roughness.base_color_factor)),
                    static_cast<f32>(mat.pbr_metallic_roughness.metallic_factor),
                    static_cast<f32>(mat.pbr_metallic_roughness.roughness_factor),
                    (mat.pbr_metallic_roughness.base_color_texture.index != -1 ? std::distance(colourTexturesOrderIndex.begin(), std::ranges::find(colourTexturesOrderIndex, model.textures[mat.pbr_metallic_roughness.base_color_texture.index].source)) : -1),
                    mat.pbr_metallic_roughness.base_color_texture.tex_coord,
                    (mat.pbr_metallic_roughness.metallic_roughness_texture.index != -1 ? std::distance(metallicRoughnessTexturesOrderIndex.begin(), std::ranges::find(metallicRoughnessTexturesOrderIndex, model.textures[mat.pbr_metallic_roughness.metallic_roughness_texture.index].source)) : -1),
                    mat.pbr_metallic_roughness.metallic_roughness_texture.tex_coord,
                    Vec3p(glm::make_vec3(mat.emissive_factor)),
                    (mat.normal_texture.index != -1 ? std::distance(normalTexturesOrderIndex.begin(), std::ranges::find(normalTexturesOrderIndex, model.textures[mat.normal_texture.index].source)) : -1),
                    mat.normal_texture.tex_coord,
                    (mat.emissive_texture.index != -1 ? std::distance(emissiveTexturesOrderIndex.begin(), std::ranges::find(emissiveTexturesOrderIndex, model.textures[mat.emissive_texture.index].source)) : -1),
                    mat.emissive_texture.tex_coord,
                    static_cast<f32>(mat.alpha_cutoff)
                );
            }

            Log<LogLevel::Debug>("{}", materialDataToPrint);

            colourThreadDone = false;
            normalThreadDone = false;
            metallicRoughnessThreadDone = false;
            emissiveTextureThreadDone = false;
            std::jthread colourTextureThread([&, colourTexturesOrderIndex = std::move(colourTexturesOrderIndex)]()
            {
                const std::filesystem::path gltfPath(gltfFilePath);
                if (!colourTexturesOrderIndex.empty())
                    m_ColourTextureArray = Graphics::TextureArray::CreateColourTextureArray(model, colourTexturesOrderIndex, gltfPath.parent_path().string());
                colourThreadDone = true;
            });
            std::jthread normalTextureThread([&, normalTexturesOrderIndex = std::move(normalTexturesOrderIndex)]()
            {
                const std::filesystem::path gltfPath(gltfFilePath);
                if (!normalTexturesOrderIndex.empty())
                    m_NormalTextureArray = Graphics::TextureArray::CreateNormalTextureArray(model, normalTexturesOrderIndex, gltfPath.parent_path().string());
                normalThreadDone = true;
            });
            std::jthread metallicRoughnessTextureThread([&, metallicRoughnessTexturesOrderIndex = std::move(metallicRoughnessTexturesOrderIndex)]()
            {
                const std::filesystem::path gltfPath(gltfFilePath);
                if (!metallicRoughnessTexturesOrderIndex.empty())
                    m_MetallicRoughnessTextureArray = Graphics::TextureArray::CreateMetallicRoughnessTextureArray(model, metallicRoughnessTexturesOrderIndex, gltfPath.parent_path().string());
                metallicRoughnessThreadDone = true;
            });
            std::jthread emissiveTextureThread([&, emissiveTexturesOrderIndex = std::move(emissiveTexturesOrderIndex)]()
            {
                const std::filesystem::path gltfPath(gltfFilePath);
                if (!emissiveTexturesOrderIndex.empty())
                    m_EmissiveTextureArray = Graphics::TextureArray::CreateEmissiveTextureArray(model, emissiveTexturesOrderIndex, gltfPath.parent_path().string());
                emissiveTextureThreadDone = true;
            });
            colourTextureThread.detach();
            normalTextureThread.detach();
            metallicRoughnessTextureThread.detach();
            emissiveTextureThread.detach();
            //if (!colourTexturesOrderIndex.empty())
            //    m_ColourTextureArray = Graphics::TextureArray::CreateColourTextureArray(model, colourTexturesOrderIndex, gltfPath.parent_path().string());
            //if (!normalTexturesOrderIndex.empty())
            //    m_NormalTextureArray = Graphics::TextureArray::CreateNormalTextureArray(model, normalTexturesOrderIndex, gltfPath.parent_path().string());
            //if (!metallicRoughnessTexturesOrderIndex.empty())
            //    m_MetallicRoughnessTextureArray = Graphics::TextureArray::CreateMetallicRoughnessTextureArray(model, metallicRoughnessTexturesOrderIndex, gltfPath.parent_path().string());
            //if (!emissiveTexturesOrderIndex.empty())
            //    m_EmissiveTextureArray = Graphics::TextureArray::CreateEmissiveTextureArray(model, emissiveTexturesOrderIndex, gltfPath.parent_path().string());

            //if (colourTextureThread.joinable())
            //    colourTextureThread.join();
            //if (normalTextureThread.joinable())
            //    normalTextureThread.join();
            //if (metallicRoughnessTextureThread.joinable())
            //    metallicRoughnessTextureThread.join();
            //if (emissiveTextureThread.joinable())
            //    emissiveTextureThread.join();

            m_TextureArraySampler = Graphics::TextureArraySampler::CreateTextureArraySampler(model.samplers[0]); // for now only 1 sampler is supported
            m_GPUBuffer->UpdateBufferData(std::bit_cast<u8*>(materialData.data()), materialBufferSize, m_MaterialBufferOffset);
        }

        u32 customInstancesIndex = 0;

        std::vector<GPULightData> lightData;
        lightData.reserve(numberOfLights);

        std::map<u32, std::unique_ptr<SceneMesh>> meshMap;
        std::vector<std::pair<Mat4, u32>> meshIndexes;

        for (u32 i = 0; i < scene.nodes_count; i++)
            IterateThroughNodes(model, scene.nodes[i], gltfBufferPosToIndicesOffset, verticesPosBufferOffset, Mat4(1.0f), customInstancesIndex, GPUDatas, verticesPos, verticesData, lightData, meshMap, meshIndexes);

        if (verticesPosDataSize > 0)
            m_GPUBuffer->UpdateBufferData(std::bit_cast<u8*>(verticesPos.data()), verticesPosDataSize, verticesPosBufferOffset);
        if (verticesDataSize > 0)
            m_GPUBuffer->UpdateBufferData(std::bit_cast<u8*>(verticesData.data()), verticesDataSize, verticesBufferOffset);
        if (geometryBufferSize > 0)
            m_GPUBuffer->UpdateBufferData(std::bit_cast<u8*>(GPUDatas.data()), geometryBufferSize, m_GeometryBufferOffset);
        if (LightBufferSize > 0)
            m_GPUBuffer->UpdateBufferData(std::bit_cast<u8*>(lightData.data()), LightBufferSize, m_LightBufferOffset);

        m_TLAS = BaseTLAS::CreateTopLevelAccelerationStructure(meshMap, meshIndexes);
        m_NumberOfLights = static_cast<u32>(lightData.size());

        m_Layer = std::make_shared<GLTFLayer>(m_GPUBuffer, m_LightBufferOffset, std::move(lightData), layerName);
        Application::GetInstance().PushLayer(m_Layer);

        while (!colourThreadDone || !normalThreadDone || !metallicRoughnessThreadDone || !emissiveTextureThreadDone)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));

        if (errorStack.has_error)
            tg3_error_stack_free(&errorStack);
        tg3_model_free(&model);
    }

    GLTFLoader::~GLTFLoader()
    {
        Application::GetInstance().PopLayer(m_Layer);
    }

    void GLTFLoader::IterateThroughNodes(const tg3_model& model, u32 nodeIndex, const std::unordered_map<uSize, uSize>& gltfBufferPosToIndicesOffset, uSize verticesBufferOffset, Mat4 parentTransform, u32& customInstancesIndex, std::vector<GPUData>& GPUDatas, std::vector<Vec3p>& verticesPos, std::vector<VertexData>& verticesData, std::vector<GPULightData>& lightData, std::map<u32, std::unique_ptr<SceneMesh>>& meshes, std::vector<std::pair<Mat4, u32>>& meshIndexes)
    {
        const tg3_node& node = model.nodes[nodeIndex];

        // casts are used in this next section because tinygltf gives the matrix with doubles but vulkan uses floats, and we change it right away
        if (node.has_matrix)
            parentTransform = parentTransform * static_cast<Mat4>(glm::make_mat4(node.matrix));
        else
        {
            const auto floatRotation = static_cast<Vec4>(glm::make_vec4(node.rotation));

            const Mat4 translation = glm::translate(glm::mat4(1.0f), static_cast<Vec3>(glm::make_vec3(node.translation)));
            const Mat4 rotation = glm::mat4_cast(glm::quat(floatRotation.w, floatRotation.x, floatRotation.y, floatRotation.z));
            const Mat4 scale = glm::scale(Mat4(1.0f), static_cast<Vec3>(glm::make_vec3(node.scale)));

            parentTransform = parentTransform * (translation * rotation * scale);
        }

        if (const auto hasGPUInstancingEXT = doesGLTFExtensionExist(node, "EXT_mesh_gpu_instancing");
            hasGPUInstancingEXT.has_value())
        {
            // TODO: according to the gltf 2.0 spec 1 or 2 of (translation, rotation, scale) can be missing. This implementation currently doesnt support this usage, all 3 have to be present.

            const auto& value = hasGPUInstancingEXT.value();

            if (value.type != TG3_VALUE_OBJECT)
                Log<LogLevel::Warn>("EXT_mesh_gpu_instancing extension is not an object");

            std::span<Vec3p> translationBuffer;
            std::span<Vec4p> rotationBuffer;
            std::span<Vec3p> scaleBuffer;

            for (u32 j = 0; j < value.object_count; j++)
            {
                const auto& [entryName, entryValue] = value.object_data[j];

                for (u32 k = 0; k < entryValue.object_count; k++)
                {
                    const auto& [instanceName, instanceValue] = entryValue.object_data[k];
                    if (tg3_str_equals_cstr(instanceName, "TRANSLATION"))
                    {
                        const tg3_accessor& acc = model.accessors[instanceValue.int_val];
                        const auto [offset, count, byteStride, bufferIndex, hasData] = extractAttribute(acc, model);
                        assert(hasData);
                        assert(byteStride == sizeof(Vec3p));
                        translationBuffer = std::span(std::bit_cast<Vec3p*>(model.buffers[bufferIndex].data.data + offset), count);
                    }
                    else if (tg3_str_equals_cstr(instanceName, "ROTATION"))
                    {
                        const tg3_accessor& acc = model.accessors[instanceValue.int_val];
                        const auto [offset, count, byteStride, bufferIndex, hasData] = extractAttribute(acc, model);
                        assert(hasData);
                        assert(byteStride == sizeof(Vec4p));
                        rotationBuffer = std::span(std::bit_cast<Vec4p*>(model.buffers[bufferIndex].data.data + offset), count);
                    }
                    else if (tg3_str_equals_cstr(instanceName, "SCALE"))
                    {
                        const tg3_accessor& acc = model.accessors[instanceValue.int_val];
                        const auto [offset, count, byteStride, bufferIndex, hasData] = extractAttribute(acc, model);
                        assert(hasData);
                        assert(byteStride == sizeof(Vec3p));
                        scaleBuffer = std::span(std::bit_cast<Vec3p*>(model.buffers[bufferIndex].data.data + offset), count);
                    }
                }
            }

            if (translationBuffer.size() != rotationBuffer.size() || translationBuffer.size() != scaleBuffer.size())
                Log<LogLevel::Critical>("EXT_mesh_gpu_instancing extension has mismatched buffer sizes");

            if (!meshes.contains(node.mesh))
            {
                meshes.emplace(node.mesh, SceneMesh::CreateFromGLTFModelWithMeshIndex(model, node.mesh, customInstancesIndex, gltfBufferPosToIndicesOffset, verticesBufferOffset, m_GPUBuffer, GPUDatas, verticesPos, verticesData));
                customInstancesIndex += model.meshes[node.mesh].primitives_count;
            }

            for (size_t i = 0; i < translationBuffer.size(); ++i)
            {
                const Mat4 translation = glm::translate(glm::mat<4, 4, f32, glm::packed_highp>(1.0f), translationBuffer[i]);
                const Mat4 rotation = glm::mat4_cast(glm::quat(rotationBuffer[i].w, rotationBuffer[i].x, rotationBuffer[i].y, rotationBuffer[i].z));
                const Mat4 scale = glm::scale(glm::mat<4, 4, f32, glm::packed_highp>(1.0f), scaleBuffer[i]);

                meshIndexes.emplace_back(parentTransform * (translation * rotation * scale), node.mesh);
            }
        }
        else if (node.mesh != -1)
        {
            if (!meshes.contains(node.mesh))
            {
                meshes.emplace(node.mesh, SceneMesh::CreateFromGLTFModelWithMeshIndex(model, node.mesh, customInstancesIndex, gltfBufferPosToIndicesOffset, verticesBufferOffset, m_GPUBuffer, GPUDatas, verticesPos, verticesData));
                customInstancesIndex += model.meshes[node.mesh].primitives_count;
            }

            meshIndexes.emplace_back(parentTransform, node.mesh);
        }

        if (node.light != -1)
        {
            const tg3_light& light = model.lights[node.light];
            const std::string_view lightType = light.type.data;
            const Vec3p colour = glm::make_vec3(light.color);

            Log<LogLevel::Trace>("Found light of type {} with a range of {} and with intensity {} and colour ({}, {}, {})", lightType, light.range, light.intensity, colour.x, colour.y, colour.z);

            if (tg3_str_equals_cstr(light.type, "spot"))
            {
                lightData.emplace_back(
                    Vec3p(parentTransform[3]), // position
                    0, // type (0 for spot)
                    glm::normalize(Vec3p(-parentTransform[2])), // direction
                    light.intensity != 0.0 ? static_cast<f32>(light.intensity) : 10.0f, // intensity
                    colour, // colour
                    static_cast<f32>(light.range), // range
                    static_cast<f32>(light.spot.inner_cone_angle), // inner cone angle
                    static_cast<f32>(light.spot.outer_cone_angle)  // outer cone angle
                );
            }
            else if (tg3_str_equals_cstr(light.type, "point"))
            {
                //const auto radius = (glm::length(Vec3p(parentTransform[0])) + glm::length(Vec3p(parentTransform[1])) + glm::length(Vec3p(parentTransform[2]))) / 3.0f;
                constexpr auto radius = 0.1f;

                lightData.emplace_back(
                    Vec3p(parentTransform[3]), // position
                    1, // type (1 for point)
                    glm::normalize(Vec3p(-parentTransform[2])), // direction
                    light.intensity != 0.0 ? static_cast<f32>(light.intensity) : 10000.0f, // intensity
                    colour, // colour
                    static_cast<f32>(light.range), // range
                    radius < 1e-4 ? 0.0f : radius// radius
                );
            }
            else if (tg3_str_equals_cstr(light.type, "directional"))
            {
                lightData.emplace_back(
                    Vec3p(parentTransform[3]), // position
                    2, // type (2 for directional)
                    glm::normalize(Vec3p(-parentTransform[2])), // direction
                    light.intensity != 0.0 ? static_cast<f32>(light.intensity) : 1000.0f, // intensity
                    colour, // colour
                    static_cast<f32>(light.range) // range
                );
            }
            else
                Log<LogLevel::Warn>("Unknown light type: {}", lightType);
        }

        for (u32 i = 0; i < node.children_count; i++)
            IterateThroughNodes(model, node.children[i], gltfBufferPosToIndicesOffset, verticesBufferOffset, parentTransform, customInstancesIndex, GPUDatas, verticesPos, verticesData, lightData, meshes, meshIndexes);
    }

    void GLTFLoader::FindNumberOfLightsInScene(const tg3_model& model, const u32 nodeIndex, u32& numberOfLights)
    {
        const tg3_node& node = model.nodes[nodeIndex];
        if (node.light != -1)
            numberOfLights++;
        for (u32 i = 0; i < node.children_count; i++)
            FindNumberOfLightsInScene(model, node.children[i], numberOfLights);
    }
} // OWC
