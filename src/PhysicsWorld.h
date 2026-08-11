#pragma once

#include "BlastSupportModel.h"

#include <vector>

#include "PxPhysicsAPI.h"
#include "extensions/PxDefaultCpuDispatcher.h"

namespace blast_demo
{
class PhysicsWorld
{
public:
    PhysicsWorld();
    ~PhysicsWorld();

    bool init();
    void shutdown();
    void rebuild(const BlastSupportModel& model);
    void syncFromModel(BlastSupportModel& model);
    void simulate(float dt);

    bool transformFor(int nodeId, float* out16) const;
    bool hasBody(int nodeId) const;
    bool isDynamic(int nodeId) const;
    void removeBody(int nodeId);
    int bodyCount() const { return static_cast<int>(m_bodies.size()); }

    // Visual debris bodies: not tied to any NodeState, spawned when a member
    // is destroyed and removed automatically after a fixed lifetime.
    void spawnFragments(const std::vector<FragmentSpawnInfo>& fragments, const BlastSupportModel& model);
    int fragmentCount() const { return static_cast<int>(m_fragments.size()); }
    bool fragmentTransformFor(int index, float* out16) const;
    void fragmentExtentsFor(int index, float& hx, float& hy, float& hz) const;
    int fragmentNodeIdFor(int index) const;

private:
    struct Body
    {
        physx::PxRigidActor* actor = nullptr;
        bool dynamic = false;
        float hx = 1.0f;
        float hy = 1.0f;
        float hz = 1.0f;
    };

    struct FragmentBody
    {
        physx::PxRigidDynamic* actor = nullptr;
        float hx = 1.0f;
        float hy = 1.0f;
        float hz = 1.0f;
        float age = 0.0f;
        int sourceNodeId = 0;
    };

    void createBody(const BlastSupportModel& model, int nodeId);
    void convertToDynamic(const BlastSupportModel& model, int nodeId);
    void destroyFragment(FragmentBody& fragment);
    void removeExpiredFragments(float dt);

    physx::PxFoundation* m_foundation = nullptr;
    physx::PxPhysics* m_physics = nullptr;
    physx::PxScene* m_scene = nullptr;
    physx::PxDefaultCpuDispatcher* m_dispatcher = nullptr;
    physx::PxMaterial* m_material = nullptr;
    std::vector<Body> m_bodies;
    std::vector<FragmentBody> m_fragments;
    static constexpr float kFragmentLifetime = 5.0f;
};
}
