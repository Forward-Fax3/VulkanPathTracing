//
// Created by forwardfax3 on 17/04/2026.
//


#include "SponzaPalace.hpp"
#include "Application.hpp"
#include "Log.hpp"

#include "SceneMesh.hpp"

#include <tiny_gltf_v3.h>
#include <filesystem>

#include "glm/ext/matrix_transform.hpp"
#include "glm/gtc/type_ptr.hpp"


namespace OWC
{
    SponzaPalace::SponzaPalace()
    {
        constexpr std::string_view filename = "./../../../../../GLTFs/Sponza/NewSponza_Main_glTF_003.gltf";

        tg3_parse_options options;
        tg3_parse_options_init(&options);

        tg3_error_stack errorStack;
        tg3_parse_file(&m_Model, &errorStack, filename.data(), filename.size(), &options);

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

        uSize numberOfGPUGLTFDatas = 0;
        for (uSize i = 0; i < m_Model.meshes_count; i++)
            numberOfGPUGLTFDatas += m_Model.meshes[i].primitives_count;

        m_GPUData.reserve(numberOfGPUGLTFDatas);
        m_GeometryBufferSize = numberOfGPUGLTFDatas * sizeof(GPUGLTFData);
        m_MaterialBufferSize = m_Model.materials_count * sizeof(GPUMaterialData);
        m_GeometryBufferOffset = alignUp(m_Model.buffers[0].data.count, alignof(GPUGLTFData));
        m_MaterialBufferOffset = alignUp(m_GeometryBufferOffset, alignof(GPUMaterialData)) + m_GeometryBufferSize;

        const auto bufferSize = m_MaterialBufferOffset + m_MaterialBufferSize;
        m_GPUBuffer = Graphics::GeneralBuffer::CreateGeneralBuffer(bufferSize);
        m_GPUBuffer->UpdateBufferData(std::bit_cast<const u8*>(m_Model.buffers[0].data.data), m_Model.buffers[0].data.count);

        const auto sceneIndex = m_Model.default_scene == -1 ? 0 : m_Model.default_scene;
        const auto& scene = m_Model.scenes[sceneIndex];
        u32 customInstancesIndex = 0;

        std::vector<GPULightData> lightData;

        std::map<i32, std::unique_ptr<SceneMesh>> meshMap;
        std::vector<std::pair<Mat4, i32>> meshIndexes;

        for (u32 i = 0; i < scene.nodes_count; i++)
            IterateThroughNodes(scene.nodes[i], Mat4(1.0f), customInstancesIndex, lightData, meshMap, meshIndexes);

        m_TLAS = BaseTLAS::CreateTopLevelAccelerationStructure(meshMap, meshIndexes);

        m_GPUBuffer->UpdateBufferData(std::bit_cast<u8*>(m_GPUData.data()), m_GeometryBufferSize, m_GeometryBufferOffset);
        m_LightBuffer = Graphics::GeneralBuffer::CreateGeneralBuffer(lightData.size() * sizeof(GPULightData));
        m_LightBuffer->UpdateBufferData(std::bit_cast<u8*>(lightData.data()));
        m_NumberOfLights = static_cast<u32>(lightData.size());

        m_SponzaPalaceLayer = std::make_shared<SponzaPalaceLayer>(m_LightBuffer, std::move(lightData));
        Application::GetInstance().PushLayer(m_SponzaPalaceLayer);

        std::vector<GPUMaterialData> materialData;
        materialData.reserve(m_Model.materials_count);
        for (uSize i = 0; i < m_Model.materials_count; i++)
        {
            const tg3_material& mat = m_Model.materials[i];
            materialData.emplace_back(
                glm::make_vec3(mat.pbr_metallic_roughness.base_color_factor),
                static_cast<f32>(mat.pbr_metallic_roughness.metallic_factor),
                static_cast<f32>(mat.pbr_metallic_roughness.roughness_factor),
                mat.pbr_metallic_roughness.base_color_texture.index,
                mat.pbr_metallic_roughness.metallic_roughness_texture.index,
                mat.normal_texture.index,
                mat.occlusion_texture.index,
                mat.emissive_texture.index,
                static_cast<f32>(mat.alpha_cutoff)
            );
        }

        m_GPUBuffer->UpdateBufferData(std::bit_cast<u8*>(materialData.data()), m_MaterialBufferSize, m_MaterialBufferOffset);

        tg3_error_stack_free(&errorStack);
    }

    SponzaPalace::~SponzaPalace()
    {
        tg3_model_free(&m_Model);
        Application::GetInstance().PopLayer(m_SponzaPalaceLayer);
    }

    void SponzaPalace::IterateThroughNodes(const u32 nodeIndex, Mat4 parentTransform, u32& customInstancesIndex, std::vector<GPULightData>& lightData, std::map<i32, std::unique_ptr<SceneMesh>>& meshes, std::vector<std::pair<Mat4, i32>>& meshIndexes)
    {
        const tg3_node& node = m_Model.nodes[nodeIndex];

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

        if (node.mesh != -1)
        {
            if (!meshes.contains(node.mesh))
            {
                meshes.emplace(node.mesh, SceneMesh::CreateFromGLTFModelWithMeshIndex(m_Model, node.mesh, customInstancesIndex, m_GPUBuffer, m_GPUData));
                customInstancesIndex += m_Model.meshes[node.mesh].primitives_count;
            }

            meshIndexes.emplace_back(parentTransform, node.mesh);
        }

        if (node.light != -1)
        {
            const tg3_light& light = m_Model.lights[node.light];
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
                    light.intensity != 0.0 ? static_cast<f32>(light.intensity) : 100.0f, // intensity
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
                    light.intensity != 0.0 ? static_cast<f32>(light.intensity) : 10.0f, // intensity
                    colour, // colour
                    static_cast<f32>(light.range) // range
                );
            }
            else
                Log<LogLevel::Warn>("Unknown light type: {}", lightType);
        }

        for (u32 i = 0; i < node.children_count; i++)
            IterateThroughNodes(node.children[i], parentTransform, customInstancesIndex, lightData, meshes, meshIndexes);
    }
} // OWC