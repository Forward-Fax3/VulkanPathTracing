//
// Created by forwardfax3 on 17/04/2026.
//

#pragma once
#include <memory>
#include <map>

#include "Core.hpp"
#include "SceneMesh.hpp"


namespace OWC
{
    class BaseTLAS
    {
    public:
        BaseTLAS() = default;
        virtual ~BaseTLAS() = default;
        BaseTLAS(BaseTLAS&) = delete;
        BaseTLAS& operator=(BaseTLAS&) = delete;
        BaseTLAS(BaseTLAS&&) noexcept = delete;
        BaseTLAS& operator=(BaseTLAS&&) noexcept = delete;

        static std::shared_ptr<BaseTLAS> CreateTopLevelAccelerationStructure(const std::map<u32, std::unique_ptr<SceneMesh>>& meshes, const std::vector<std::pair<Mat4, u32>>& meshIndices);
    };
}
