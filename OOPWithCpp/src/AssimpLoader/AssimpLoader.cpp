//
// Created by forwardfax3 on 17/04/2026.
//


#include "AssimpLoader.hpp"
#include "Application.hpp"
#include "Log.hpp"

#include "SceneMesh.hpp"
#include <filesystem>
#include <span>

#include "assimp/DefaultLogger.hpp"
#include "assimp/Importer.hpp"
#include "assimp/postprocess.h"
#include "assimp/cfileio.h"
#include "assimp/ProgressHandler.hpp"
#include "assimp/GltfMaterial.h"

#include "glm/ext/matrix_transform.hpp"
#include "glm/gtc/type_ptr.hpp"


namespace OWC
{
    namespace
    {
        class OWCAssimpLogger : public Assimp::ProgressHandler
        {
        public:
            bool Update(const float percentage) override
            {
                Log<LogLevel::Trace>("Assimp loading progress: {:.2f}%", percentage * 100.0f);
                return true;
            }

            void UpdateFileRead(int currentStep, int numberOfSteps) override
            {
                Log<LogLevel::Trace>("Assimp file read step: {}/{}", currentStep, numberOfSteps);
            }

            void UpdatePostProcess(int step, int numberOfSteps) override
            {
                Log<LogLevel::Trace>("Assimp processing step: {}/{}", step, numberOfSteps);
            }
        };

        bool operator>(const aiColor3D& color, const aiColor3D& other)
        {
            return (color.r > other.r) || (color.g > other.g) || (color.b > other.b);
        }
    }

    AssimpLoader::AssimpLoader(const std::string_view gltfFilePath, const std::string_view layerName)
    {
        Assimp::DefaultLogger::create("Assimp Logger", Assimp::DefaultLogger::VERBOSE);

        Assimp::Importer importer;
        importer.SetPropertyInteger(AI_CONFIG_FAVOUR_SPEED, 1);
        importer.SetPropertyInteger(AI_CONFIG_GLOB_MEASURE_TIME, 1);
        importer.SetPropertyInteger(AI_CONFIG_PP_SBP_REMOVE, aiPrimitiveType_POINT | aiPrimitiveType_LINE);
        importer.SetProgressHandler(new OWCAssimpLogger());

        // the way the `aiProcess_JoinIdenticalVertices` works makes it incredibly slow on windows so this flag is disabled
#if (defined(_WIN32) || defined(_WIN64)) && !defined(DIST)
        constexpr u32 flags = aiProcess_Triangulate | aiProcess_GenNormals | aiProcess_CalcTangentSpace | aiProcess_FindInstances | aiProcess_ValidateDataStructure | aiProcess_RemoveRedundantMaterials | aiProcess_OptimizeGraph | aiProcess_FindDegenerates | aiProcess_SortByPType;
#else
        constexpr u32 flags = aiProcess_Triangulate | aiProcess_GenNormals | aiProcess_CalcTangentSpace | aiProcess_JoinIdenticalVertices | aiProcess_FindInstances | aiProcess_ValidateDataStructure | aiProcess_RemoveRedundantMaterials | aiProcess_OptimizeGraph | aiProcess_FindDegenerates | aiProcess_SortByPType;
#endif

        const aiScene* scene_ptr = importer.ReadFile(gltfFilePath.data(), flags);

        Assimp::DefaultLogger::kill();

        if (scene_ptr == nullptr)
        {
            Log<LogLevel::Critical>("Failed to load Assimp model file: {}", importer.GetErrorString());
            std::unreachable();
        }

        const aiScene& scene = *scene_ptr;

        uSize numOfVertices = 0;
        uSize indicesDataSize = 0;
        for (uSize i = 0; i < scene.mNumMeshes; i++)
        {
            const aiMesh& mesh = *scene.mMeshes[i];
            numOfVertices += mesh.mNumVertices;
            indicesDataSize += mesh.mNumFaces * 3 * sizeof(u32);
        }

        std::vector<VertexData> verticesData;
        std::vector<Vec3p> verticesPos;
        verticesData.reserve(numOfVertices);
        verticesPos.reserve(numOfVertices);
        uSize verticesDataSize = numOfVertices * sizeof(VertexData);
        uSize verticesPosDataSize = numOfVertices * sizeof(Vec3p);

        std::vector<GPUData> GPUDatas;
        GPUDatas.reserve(scene.mNumMeshes);

        uSize lightsBufferSize = scene.mNumLights * sizeof(GPULightData);
        uSize geometryBufferSize = scene.mNumMeshes * sizeof(GPUData);
        uSize materialBufferSize = scene.mNumMaterials * sizeof(GPUMaterialData);
        constexpr uSize indicesDataOffset = 0;
        const uSize verticesPosDataOffset = alignUp(indicesDataOffset + indicesDataSize, alignof(Vec3p));
        const uSize verticesDataOffset = alignUp(verticesPosDataOffset + verticesPosDataSize, alignof(VertexData));
        m_LightBufferOffset = alignUp(verticesDataOffset + verticesDataSize, alignof(GPULightData));
        m_GeometryBufferOffset = alignUp(m_LightBufferOffset + lightsBufferSize, alignof(GPUData));
        m_MaterialBufferOffset = alignUp(m_GeometryBufferOffset + geometryBufferSize, alignof(GPUMaterialData));

        constexpr uSize maxAlignment = std::max({ alignof(Vec3p), alignof(VertexData), alignof(GPULightData), alignof(GPUData), alignof(GPUMaterialData) });

        const auto bufferSize = m_MaterialBufferOffset + materialBufferSize;
        m_GPUBuffer = Graphics::GeneralBuffer::CreateGeneralBuffer(bufferSize, maxAlignment);

        std::unordered_map<aiFace*, u64> assimpFacePtrToGPUOffset;
        u64 offset = 0;
        std::vector<u32> GPUIndicesData;
        GPUIndicesData.reserve(indicesDataSize / sizeof(u32));
        for (u32 i = 0; i < scene.mNumMeshes; i++)
        {
            assimpFacePtrToGPUOffset[scene.mMeshes[i]->mFaces] = offset;
            const aiMesh& mesh = *scene.mMeshes[i];
            offset += 3 * sizeof(u32) * mesh.mNumFaces;
            for (u32 j = 0; j < mesh.mNumFaces; j++)
            {
                const aiFace& face = mesh.mFaces[j];
                assert(face.mNumIndices == 3 && "Expected fully triangulated mesh (check Assimp import flags)");
                GPUIndicesData.emplace_back(face.mIndices[0]);
                GPUIndicesData.emplace_back(face.mIndices[1]);
                GPUIndicesData.emplace_back(face.mIndices[2]);
            }
        }

        m_GPUBuffer->UpdateBufferData(std::bit_cast<const u8*>(GPUIndicesData.data()), indicesDataSize, indicesDataOffset);

        u32 customInstancesIndex = 0;

        std::map<u32, std::unique_ptr<SceneMesh>> meshMap;
        std::vector<std::pair<Mat4, u32>> meshIndexes;
        Mat4 parentTransform(1.0f);
        IterateThroughNodes(scene.mRootNode, scene, assimpFacePtrToGPUOffset, verticesPosDataOffset, parentTransform, customInstancesIndex, meshMap, meshIndexes, GPUDatas, verticesPos, verticesData);

        m_GPUBuffer->UpdateBufferData(std::bit_cast<u8*>(verticesPos.data()), verticesPosDataSize, verticesPosDataOffset);
        m_GPUBuffer->UpdateBufferData(std::bit_cast<u8*>(verticesData.data()), verticesDataSize, verticesDataOffset);
        m_GPUBuffer->UpdateBufferData(std::bit_cast<u8*>(GPUDatas.data()), geometryBufferSize, m_GeometryBufferOffset);
        m_TLAS = BaseTLAS::CreateTopLevelAccelerationStructure(meshMap, meshIndexes);

        std::vector<GPULightData> lightData;
        if (scene.mNumLights > 0)
        {
            for (u32 i = 0; i < scene.mNumLights; i++)
            {
                const aiLight& light = *scene.mLights[i];
                OWC_NO_OP;

                if (light.mType == aiLightSource_SPOT)
                {
                    lightData.emplace_back(
                        Vec3p(light.mPosition.x, light.mPosition.y, light.mPosition.z), // position
                        0, // type (0 for spot)
                        glm::normalize(Vec3p(light.mDirection.x, light.mDirection.y, light.mDirection.z)), // direction
                        light.mAttenuationQuadratic != 0.0 ? static_cast<f32>(light.mAttenuationQuadratic) : 10.0f, // intensity
                        Vec3p(light.mColorAmbient.r, light.mColorAmbient.g, light.mColorAmbient.b), // colour
                        static_cast<f32>(light.mAngleOuterCone), // range
                        static_cast<f32>(light.mAngleInnerCone), // inner cone angle
                        static_cast<f32>(light.mAngleOuterCone)  // outer cone angle
                    );
                }
                else if (light.mType == aiLightSource_POINT)
                {
                    lightData.emplace_back(
                        Vec3p(light.mPosition.x, light.mPosition.y, light.mPosition.z), // position
                        1, // type (1 for point)
                        glm::normalize(Vec3p(light.mDirection.x, light.mDirection.y, light.mDirection.z)), // direction
                        light.mAttenuationQuadratic != 0.0 ? static_cast<f32>(light.mAttenuationQuadratic) : 10000.0f, // intensity
                        Vec3p(light.mColorAmbient.r, light.mColorAmbient.g, light.mColorAmbient.b), // colour
                        light.mAttenuationQuadratic != 0.0 ? static_cast<f32>(light.mAttenuationQuadratic) : 10000.0f, // range
                        0.1// radius
                    );
                }
                else if (light.mType == aiLightSource_DIRECTIONAL)
                {
                    lightData.emplace_back(
                        Vec3p(light.mPosition.x, light.mPosition.y, light.mPosition.z), // position
                        2, // type (2 for directional)
                        glm::normalize(Vec3p(light.mDirection.x, light.mDirection.y, light.mDirection.z)), // direction
                        light.mAttenuationQuadratic != 0.0 ? static_cast<f32>(light.mAttenuationQuadratic) : 10.0f, // intensity
                        Vec3p(light.mColorAmbient.r, light.mColorAmbient.g, light.mColorAmbient.b), // colour
                        light.mAttenuationQuadratic != 0.0 ? static_cast<f32>(light.mAttenuationQuadratic) : 10.0f // range
                    );
                }
            }
            OWC_NO_OP;

            m_GPUBuffer->UpdateBufferData(std::bit_cast<u8*>(lightData.data()), lightData.size() * sizeof(GPULightData), m_LightBufferOffset);
            m_NumberOfLights = static_cast<u32>(lightData.size());
        }

        m_Layer = std::make_shared<AssimpLayer>(m_GPUBuffer, m_LightBufferOffset, std::move(lightData), layerName);
        Application::GetInstance().PushLayer(m_Layer);

        if (scene.mNumMaterials > 0)
        {
            std::vector<u32> colourTexturesOrderIndex;
            std::vector<u32> normalTexturesOrderIndex;
            std::vector<u32> metallicRoughnessTexturesOrderIndex;
            std::vector<u32> emissiveTexturesOrderIndex;

            for (uSize i = 0; i < scene.mNumMaterials; i++)
            {
                //const aiMaterial* mat = scene.mMaterials[i];

                //if (mat->GetTextureCount(aiTextureType_DIFFUSE) > 0)
                //{
                    //aiString texture;
                    //mat->GetTexture(aiTextureType_DIFFUSE, 0, &texture);
                    //OWC_NO_OP;
                    //std::ranges::find(colourTexturesOrderIndex, scene.mTextures[->index].source) == colourTexturesOrderIndex.end())
                    //colourTexturesOrderIndex.emplace_back(scene.mTextures[mat->GetTexture(aiTextureType_DIFFUSE, 0)->index].source);
                //}
                //if (mat->GetTextureCount(aiTextureType_METALNESS) > 0 && std::ranges::find(metallicRoughnessTexturesOrderIndex, scene.mTextures[mat->GetTexture(aiTextureType_METALNESS, 0)->index].source) == metallicRoughnessTexturesOrderIndex.end())
                //    metallicRoughnessTexturesOrderIndex.emplace_back(scene.mTextures[mat->GetTexture(aiTextureType_METALNESS, 0)->index].source);
                //if (mat->GetTextureCount(aiTextureType_NORMALS) > 0 && std::ranges::find(normalTexturesOrderIndex, scene.mTextures[mat->GetTexture(aiTextureType_NORMALS, 0)->index].source) == normalTexturesOrderIndex.end())
                //    normalTexturesOrderIndex.emplace_back(scene.mTextures[mat->GetTexture(aiTextureType_NORMALS, 0)->index].source);
                //if (mat->GetTextureCount(aiTextureType_EMISSION) > 0 && std::ranges::find(emissiveTexturesOrderIndex, scene.mTextures[mat->GetTexture(aiTextureType_EMISSION, 0)->index].source) == emissiveTexturesOrderIndex.end())
                //    emissiveTexturesOrderIndex.emplace_back(scene.mTextures[mat->GetTexture(aiTextureType_EMISSION, 0)->index].source);
            }

            std::string materialDataToPrint = "\n\n";
            std::vector<GPUMaterialData> materialData;
            materialData.reserve(scene.mNumMaterials);
            for (uSize i = 0; i < scene.mNumMaterials; i++)
            {
                const aiMaterial& mat = *scene.mMaterials[i];

                std::string str = std::format("Material {}:", i);
                for (u32 j = 0; j < mat.mNumProperties; j++)
                {
                    aiMaterialProperty& prop = *mat.mProperties[j];
                    str = std::format("{}\n\tProperty {}:\n"
                                      "\t\tTypename: {}\n"
                                      "\t\tSemantic: {}\n",
                                      str,
                                      j,
                                      prop.mKey.C_Str(),
                                      prop.mSemantic);

                    if (prop.mType == aiPTI_Float)
                    {
                        str = std::format("{}\t\tData:", str);
                        for (u32 k = 0; k < prop.mDataLength / sizeof(float); k++)
                            str = std::format("{} {}", str, *reinterpret_cast<float*>(prop.mData + k * sizeof(float)));
                        str = std::format("{}\n", str);
                    }
                    else if (prop.mType == aiPTI_Double)
                    {
                        str = std::format("{}\t\tData:", str);
                        for (u32 k = 0; k < prop.mDataLength / sizeof(double); k++)
                            str = std::format("{} {}", str, *reinterpret_cast<double*>(prop.mData + k * sizeof(double)));
                        str = std::format("{}\n", str);
                    }
                    else if (prop.mType == aiPTI_String)
                    {
                        str = std::format("{}\t\tData: ", str);
                        for (u32 k = 0; k < prop.mDataLength; k++)
                            str = std::format("{}{}", str, *(prop.mData + k));
                        str = std::format("{}\n", str);
                    }
                    else if (prop.mType == aiPTI_Integer)
                    {
                        str = std::format("{}\t\tData:", str);
                        for (u32 k = 0; k < prop.mDataLength / sizeof(int); k++)
                            str = std::format("{} {}", str, *reinterpret_cast<int*>(prop.mData + k * sizeof(int)));
                        str = std::format("{}\n", str);
                    }
                }

                Log<LogLevel::Trace>("{}",str);

                /*materialDataToPrint += std::format("\tMaterial Data:\n"
                        "\t\tMetallic Roughness Base Colour Factor: {}, {}, {}, {}\n"
                        "\t\tMetallic Roughness Metallic Factor {}\n"
                        "\t\tMetallic Roughness Roughness Factor {}\n"
                        "\t\tMetallic Roughness Base Colour Texture Index: original {}, true {}, coords {}\n"
                        "\t\tMetallic Roughness Base Metallic Roughness Texture Index: original {}, true {}, coords {}\n"
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
                    (mat.pbr_metallic_roughness.base_color_texture.index != -1 ? std::distance(colourTexturesOrderIndex.begin(), std::ranges::find(colourTexturesOrderIndex, m_Model.textures[mat.pbr_metallic_roughness.base_color_texture.index].source)) : -1),
                    mat.pbr_metallic_roughness.base_color_texture.tex_coord,
                    (mat.pbr_metallic_roughness.metallic_roughness_texture.index != -1 ? std::distance(metallicRoughnessTexturesOrderIndex.begin(), std::ranges::find(metallicRoughnessTexturesOrderIndex, m_Model.textures[mat.pbr_metallic_roughness.metallic_roughness_texture.index].source)) : -1),
                    mat.pbr_metallic_roughness.metallic_roughness_texture.tex_coord,
                    Vec3p(glm::make_vec3(mat.emissive_factor)),
                    (mat.normal_texture.index != -1 ? std::distance(normalTexturesOrderIndex.begin(), std::ranges::find(normalTexturesOrderIndex, m_Model.textures[mat.normal_texture.index].source)) : -1),
                    mat.normal_texture.tex_coord,
                    (mat.emissive_texture.index != -1 ? std::distance(emissiveTexturesOrderIndex.begin(), std::ranges::find(emissiveTexturesOrderIndex, m_Model.textures[mat.emissive_texture.index].source)) : -1),
                    mat.emissive_texture.tex_coord,
                    static_cast<f32>(mat.alpha_cutoff)
                    );*/

                aiColor4D baseColor;
                mat.Get(AI_MATKEY_BASE_COLOR, baseColor);

                if (baseColor == aiColor4D(0.0f))
                {
                    aiColor3D diffuseColour;
                    ai_real opacity;
                    mat.Get(AI_MATKEY_COLOR_DIFFUSE, diffuseColour);
                    mat.Get(AI_MATKEY_OPACITY, opacity);
                    baseColor = aiColor4D(diffuseColour.r, diffuseColour.g, diffuseColour.b, opacity != 0.0f ? opacity : 1.0f);
                }

                ai_real metallicFactor;
                mat.Get(AI_MATKEY_METALLIC_FACTOR, metallicFactor);
                ai_real roughnessFactor;
                mat.Get(AI_MATKEY_ROUGHNESS_FACTOR, roughnessFactor);
                aiColor3D emissiveFactor;
                mat.Get(AI_MATKEY_COLOR_EMISSIVE, emissiveFactor);
                aiColor3D emissiveIntensity;
                mat.Get(AI_MATKEY_EMISSIVE_INTENSITY, emissiveIntensity);
                if (emissiveFactor > aiColor3D(0.0f) && emissiveIntensity > aiColor3D(0.0f))
                    emissiveFactor = emissiveFactor * emissiveIntensity;
                else if (emissiveIntensity > aiColor3D(0.0f))
                    emissiveFactor = emissiveIntensity;
                ai_real alphaCutoff;
                mat.Get(AI_MATKEY_OPACITY, alphaCutoff);

                OWC_NO_OP;

                materialData.emplace_back(
                    Vec4p(glm::make_vec4(&baseColor.r)),
                    static_cast<f32>(metallicFactor),
                    static_cast<f32>(roughnessFactor),
                    -1,
                    0,
                    -1,
                    0,
                    Vec3p(glm::make_vec3(&emissiveFactor.r)),
                    -1,
                    0,
                    -1,
                    0,
                    static_cast<f32>(alphaCutoff)
                );
            }

            Log<LogLevel::Debug>("{}", materialDataToPrint);

            m_GPUBuffer->UpdateBufferData(std::bit_cast<u8*>(materialData.data()), materialBufferSize, m_MaterialBufferOffset);

            /*std::filesystem::path gltfPath(gltfFilePath);
            m_TextureArraySampler = Graphics::TextureArraySampler::CreateTextureArraySampler(m_Model.samplers[0]); // for now only 1 sampler is supported
            if (!colourTexturesOrderIndex.empty())
                m_ColourTextureArray = Graphics::TextureArray::CreateColourTextureArray(m_Model, colourTexturesOrderIndex, gltfPath.parent_path().string());
            if (!normalTexturesOrderIndex.empty())
                m_NormalTextureArray = Graphics::TextureArray::CreateNormalTextureArray(m_Model, normalTexturesOrderIndex, gltfPath.parent_path().string());
            if (!metallicRoughnessTexturesOrderIndex.empty())
                m_MetallicRoughnessTextureArray = Graphics::TextureArray::CreateMetallicRoughnessTextureArray(m_Model, metallicRoughnessTexturesOrderIndex, gltfPath.parent_path().string());
            if (!emissiveTexturesOrderIndex.empty())
                m_EmissiveTextureArray = Graphics::TextureArray::CreateEmissiveTextureArray(m_Model, emissiveTexturesOrderIndex, gltfPath.parent_path().string());
                */
        }
    }

    AssimpLoader::~AssimpLoader()
    {
        //Application::GetInstance().PopLayer(m_Layer);
    }

    void AssimpLoader::IterateThroughNodes(const aiNode* node_ptr, const aiScene& scene, const std::unordered_map<aiFace*, u64>& assimpFacePtrToGPUOffset, const uSize vertexDataOffset, Mat4 parentTransform, u32& customInstancesIndex, std::map<u32, std::unique_ptr<SceneMesh>>& meshes, std::vector<std::pair<Mat4, u32>>& meshIndexes, std::vector<GPUData>& GPUDatas, std::vector<Vec3p>& verticesPos, std::vector<VertexData>& verticesData)
    {
        const aiNode& node = *node_ptr;

        parentTransform = parentTransform * glm::transpose(glm::make_mat4(&node.mTransformation[0][0]));

        if (node.mNumMeshes > 0)
        {
            for (u32 i = 0; i < node.mNumMeshes; i++)
            {
                const u32 meshIndex = node.mMeshes[i];

                if (!meshes.contains(meshIndex))
                {
                    meshes.emplace(meshIndex, SceneMesh::CreateFromAssimpSceneWithMeshIndex(scene, assimpFacePtrToGPUOffset, meshIndex, vertexDataOffset, customInstancesIndex, m_GPUBuffer, GPUDatas, verticesPos, verticesData));
                    customInstancesIndex++;
                }

                meshIndexes.emplace_back(parentTransform, meshIndex);
            }
        }

        for (u32 i = 0; i < node.mNumChildren; i++)
            IterateThroughNodes(node.mChildren[i], scene, assimpFacePtrToGPUOffset, vertexDataOffset, parentTransform, customInstancesIndex, meshes, meshIndexes, GPUDatas, verticesPos, verticesData);
    }
} // OWC
