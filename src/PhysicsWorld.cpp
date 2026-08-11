#include "PhysicsWorld.h"

#include "SceneLayout.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "extensions/PxRigidBodyExt.h"
#include "extensions/PxSimpleFactory.h"

namespace blast_demo
{
namespace
{
class DefaultAllocator : public physx::PxAllocatorCallback
{
public:
    void* allocate(size_t size, const char*, const char*, int) override { return std::malloc(size); }
    void deallocate(void* ptr) override { std::free(ptr); }
};

class DefaultErrorCallback : public physx::PxErrorCallback
{
public:
    void reportError(physx::PxErrorCode::Enum code, const char* message, const char* file, int line) override
    {
        (void)code;
        std::fprintf(stderr, "[PhysX] %s (%s:%d)\n", message, file ? file : "", line);
    }
};

bool shouldFall(const NodeState& node)
{
    // Collapse is driven purely by the confirmed structure state. During a
    // cascade the structure model schedules failures with a delay, so a member
    // may temporarily carry an over-capacity load or be marked Overloaded while
    // still standing; the physics layer must wait until the model actually
    // kills it (alive=false / Unsupported) instead of reacting early.
    return !node.alive || node.status == NodeStatus::Unsupported;
}

void writeMat44(const physx::PxMat44& m, float* out16)
{
    out16[0] = m.column0.x; out16[1] = m.column0.y; out16[2] = m.column0.z; out16[3] = m.column0.w;
    out16[4] = m.column1.x; out16[5] = m.column1.y; out16[6] = m.column1.z; out16[7] = m.column1.w;
    out16[8] = m.column2.x; out16[9] = m.column2.y; out16[10] = m.column2.z; out16[11] = m.column2.w;
    out16[12] = m.column3.x; out16[13] = m.column3.y; out16[14] = m.column3.z; out16[15] = m.column3.w;
}
}

PhysicsWorld::PhysicsWorld() = default;

PhysicsWorld::~PhysicsWorld()
{
    shutdown();
}

bool PhysicsWorld::init()
{
    static DefaultAllocator allocator;
    static DefaultErrorCallback errorCallback;
    m_foundation = PxCreateFoundation(PX_PHYSICS_VERSION, allocator, errorCallback);
    if (!m_foundation) return false;
    m_physics = PxCreatePhysics(PX_PHYSICS_VERSION, *m_foundation, physx::PxTolerancesScale(), true, nullptr);
    if (!m_physics) return false;

    physx::PxSceneDesc desc(m_physics->getTolerancesScale());
    desc.gravity = physx::PxVec3(0.0f, -9.81f, 0.0f);
    m_dispatcher = physx::PxDefaultCpuDispatcherCreate(2);
    if (!m_dispatcher) return false;
    desc.cpuDispatcher = m_dispatcher;
    desc.filterShader = physx::PxDefaultSimulationFilterShader;
    m_scene = m_physics->createScene(desc);
    if (!m_scene) return false;

    m_material = m_physics->createMaterial(0.5f, 0.5f, 0.6f);
    physx::PxRigidStatic* ground = PxCreatePlane(*m_physics, physx::PxPlane(0.0f, 1.0f, 0.0f, 0.0f), *m_material);
    if (ground) m_scene->addActor(*ground);
    return true;
}

void PhysicsWorld::shutdown()
{
    if (m_scene)
    {
        for (FragmentBody& fragment : m_fragments)
            destroyFragment(fragment);
    }
    m_fragments.clear();
    if (m_scene) { m_scene->release(); m_scene = nullptr; }
    if (m_dispatcher) { m_dispatcher->release(); m_dispatcher = nullptr; }
    if (m_physics) { m_physics->release(); m_physics = nullptr; }
    if (m_foundation) { m_foundation->release(); m_foundation = nullptr; }
    m_material = nullptr;
    m_bodies.clear();
}

void PhysicsWorld::createBody(const BlastSupportModel& model, int nodeId)
{
    const NodeState& node = model.nodes()[static_cast<size_t>(nodeId)];
    const BoxLayout layout = nodeLayout(node, model.activeColumns(), model.activeBlocks(), model.isHouse());
    const physx::PxTransform pose(physx::PxVec3(layout.cx, layout.cy, layout.cz));
    const physx::PxBoxGeometry geometry(layout.hx, layout.hy, layout.hz);
    Body body;
    body.hx = layout.hx;
    body.hy = layout.hy;
    body.hz = layout.hz;
    body.dynamic = shouldFall(node);
    if (body.dynamic)
    {
        physx::PxRigidDynamic* dyn = PxCreateDynamic(*m_physics, pose, geometry, *m_material, 1.0f);
        if (dyn)
        {
            physx::PxRigidBodyExt::setMassAndUpdateInertia(*dyn, node.mass > 0.0f ? node.mass : 10.0f);
            dyn->setLinearDamping(0.05f);
            dyn->setAngularDamping(0.5f);
            m_scene->addActor(*dyn);
            body.actor = dyn;
        }
    }
    else
    {
        physx::PxRigidStatic* st = PxCreateStatic(*m_physics, pose, geometry, *m_material);
        if (st)
        {
            m_scene->addActor(*st);
            body.actor = st;
        }
    }
    m_bodies[static_cast<size_t>(nodeId)] = body;
}

void PhysicsWorld::rebuild(const BlastSupportModel& model)
{
    if (m_scene)
    {
        for (Body& body : m_bodies)
        {
            if (body.actor)
            {
                m_scene->removeActor(*body.actor);
                body.actor->release();
                body.actor = nullptr;
            }
        }
        for (FragmentBody& fragment : m_fragments)
            destroyFragment(fragment);
    }
    m_bodies.assign(model.nodes().size(), Body());
    m_fragments.clear();
    for (size_t index = 0; index < model.nodes().size(); ++index)
    {
        if (model.nodes()[index].type != NodeType::Ground)
            createBody(model, static_cast<int>(index));
    }
}

void PhysicsWorld::syncFromModel(BlastSupportModel& model)
{
    if (!m_scene) return;
    std::vector<FragmentSpawnInfo> pending;
    model.takePendingFragments(pending);
    if (!pending.empty())
        spawnFragments(pending, model);
    if (model.nodes().size() != m_bodies.size())
    {
        rebuild(model);
        return;
    }
    for (size_t index = 0; index < model.nodes().size(); ++index)
    {
        const NodeState& node = model.nodes()[index];
        if (node.type == NodeType::Ground) continue;
        Body& body = m_bodies[index];
        if (!body.actor || body.dynamic) continue;
        // Collapse is driven purely by the structure model: a member that has
        // lost its Ground path (or overloaded) falls. This keeps 2D and 3D
        // consistent - no separate geometric support test here.
        if (shouldFall(node))
            convertToDynamic(model, static_cast<int>(index));
    }
}

void PhysicsWorld::convertToDynamic(const BlastSupportModel& model, int nodeId)
{
    Body& body = m_bodies[static_cast<size_t>(nodeId)];
    if (!body.actor) return;
    const physx::PxTransform pose = body.actor->getGlobalPose();
    m_scene->removeActor(*body.actor);
    body.actor->release();
    body.actor = nullptr;
    body.dynamic = true;
    const NodeState& node = model.nodes()[static_cast<size_t>(nodeId)];
    physx::PxRigidDynamic* dyn = PxCreateDynamic(*m_physics, pose,
        physx::PxBoxGeometry(body.hx, body.hy, body.hz), *m_material, 1.0f);
    if (!dyn) return;
    physx::PxRigidBodyExt::setMassAndUpdateInertia(*dyn, node.mass > 0.0f ? node.mass : 10.0f);
    dyn->setLinearDamping(0.05f);
    dyn->setAngularDamping(0.5f);
    m_scene->addActor(*dyn);
    body.actor = dyn;
}

void PhysicsWorld::simulate(float dt)
{
    if (!m_scene) return;
    m_scene->simulate(dt);
    m_scene->fetchResults(true);
    removeExpiredFragments(dt);
}

bool PhysicsWorld::transformFor(int nodeId, float* out16) const
{
    if (!out16 || nodeId < 0 || nodeId >= static_cast<int>(m_bodies.size())) return false;
    const Body& body = m_bodies[static_cast<size_t>(nodeId)];
    if (!body.actor) return false;
    const physx::PxTransform pose = body.actor->getGlobalPose();
    physx::PxMat44 matrix(pose.q);
    matrix.column3 = physx::PxVec4(pose.p.x, pose.p.y, pose.p.z, 1.0f);
    writeMat44(matrix, out16);
    return true;
}

bool PhysicsWorld::hasBody(int nodeId) const
{
    return nodeId >= 0 && nodeId < static_cast<int>(m_bodies.size()) &&
           m_bodies[static_cast<size_t>(nodeId)].actor != nullptr;
}

bool PhysicsWorld::isDynamic(int nodeId) const
{
    if (nodeId < 0 || nodeId >= static_cast<int>(m_bodies.size())) return false;
    return m_bodies[static_cast<size_t>(nodeId)].dynamic;
}

void PhysicsWorld::removeBody(int nodeId)
{
    if (nodeId < 0 || nodeId >= static_cast<int>(m_bodies.size())) return;
    Body& body = m_bodies[static_cast<size_t>(nodeId)];
    if (!body.actor) return;
    if (m_scene) m_scene->removeActor(*body.actor);
    body.actor->release();
    body.actor = nullptr;
    body.dynamic = false;
}

void PhysicsWorld::destroyFragment(FragmentBody& fragment)
{
    if (!fragment.actor) return;
    if (m_scene) m_scene->removeActor(*fragment.actor);
    fragment.actor->release();
    fragment.actor = nullptr;
}

void PhysicsWorld::removeExpiredFragments(float dt)
{
    for (size_t index = 0; index < m_fragments.size();)
    {
        FragmentBody& fragment = m_fragments[index];
        if (fragment.actor) fragment.age += dt;
        if (fragment.age >= kFragmentLifetime)
        {
            destroyFragment(fragment);
            m_fragments.erase(m_fragments.begin() + static_cast<std::ptrdiff_t>(index));
        }
        else
        {
            ++index;
        }
    }
}

void PhysicsWorld::spawnFragments(const std::vector<FragmentSpawnInfo>& fragments,
                                  const BlastSupportModel& model)
{
    if (!m_scene) return;
    std::vector<int> replacedMembers;
    for (const FragmentSpawnInfo& spawn : fragments)
    {
        if (spawn.nodeId < 0 || spawn.nodeId >= static_cast<int>(model.nodes().size()))
            continue;
        const NodeState& node = model.nodes()[static_cast<size_t>(spawn.nodeId)];
        const BoxLayout layout = nodeLayout(node, model.activeColumns(), model.activeBlocks(), model.isHouse());
        const int k = spawn.fragmentIndex & 7;
        // Each fragment halves all three axes (2x2x2), so the eight pieces
        // reassemble exactly into the member's original box.
        const float hx = std::max(0.05f, layout.hx * 0.5f);
        const float hy = std::max(0.05f, layout.hy * 0.5f);
        const float hz = std::max(0.05f, layout.hz * 0.5f);
        const float gap = 0.05f;
        const float dx = (k & 1) ? (hx + gap) : -(hx + gap);
        const float dy = (k & 2) ? (hy + gap) : -(hy + gap);
        const float dz = (k & 4) ? (hz + gap) : -(hz + gap);
        const physx::PxTransform pose(physx::PxVec3(layout.cx + dx, layout.cy + dy, layout.cz + dz));
        physx::PxRigidDynamic* dyn = PxCreateDynamic(*m_physics, pose,
            physx::PxBoxGeometry(hx, hy, hz), *m_material, 1.0f);
        if (!dyn) continue;
        physx::PxRigidBodyExt::setMassAndUpdateInertia(*dyn,
            node.mass > 0.0f ? std::max(0.5f, node.mass * 0.125f) : 2.0f);
        dyn->setLinearDamping(0.05f);
        dyn->setAngularDamping(0.5f);
        m_scene->addActor(*dyn);
        FragmentBody fragment;
        fragment.actor = dyn;
        fragment.hx = hx;
        fragment.hy = hy;
        fragment.hz = hz;
        fragment.sourceNodeId = spawn.nodeId;
        m_fragments.push_back(fragment);

        // The member visually shatters into its fragments: replace its own box
        // body with the pieces so the whole member is not also rendered.
        if (std::find(replacedMembers.begin(), replacedMembers.end(), spawn.nodeId) == replacedMembers.end())
        {
            removeBody(spawn.nodeId);
            replacedMembers.push_back(spawn.nodeId);
        }
    }
}

bool PhysicsWorld::fragmentTransformFor(int index, float* out16) const
{
    if (!out16 || index < 0 || index >= static_cast<int>(m_fragments.size())) return false;
    const FragmentBody& fragment = m_fragments[static_cast<size_t>(index)];
    if (!fragment.actor) return false;
    const physx::PxTransform pose = fragment.actor->getGlobalPose();
    physx::PxMat44 matrix(pose.q);
    matrix.column3 = physx::PxVec4(pose.p.x, pose.p.y, pose.p.z, 1.0f);
    writeMat44(matrix, out16);
    return true;
}

void PhysicsWorld::fragmentExtentsFor(int index, float& hx, float& hy, float& hz) const
{
    hx = hy = hz = 0.0f;
    if (index < 0 || index >= static_cast<int>(m_fragments.size())) return;
    const FragmentBody& fragment = m_fragments[static_cast<size_t>(index)];
    hx = fragment.hx;
    hy = fragment.hy;
    hz = fragment.hz;
}

int PhysicsWorld::fragmentNodeIdFor(int index) const
{
    if (index < 0 || index >= static_cast<int>(m_fragments.size())) return -1;
    return m_fragments[static_cast<size_t>(index)].sourceNodeId;
}
}
