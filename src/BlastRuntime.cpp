#include "BlastRuntime.h"
#include "BlastSupportModel.h"

#include <limits>

namespace blast_demo
{
namespace
{
const uint32_t Invalid = std::numeric_limits<uint32_t>::max();

// Each structural member is pre-fractured into 8 visual fragments (2x2x2).
// A fragment chunk's userData packs the parent node id and the fragment index
// under a marker bit so it can be told apart from the member's core chunk.
constexpr uint32_t kFragmentUserDataFlag = 0x80000000u;
constexpr uint32_t kFragmentsPerChunk = 8;

int blockId(int floor, int slot, int blocksPerFloor)
{
    return 1 + floor * blocksPerFloor + slot;
}

int columnId(int floor, int slot, int floors, int columnsPerFloor, int blocksPerFloor)
{
    return 1 + floors * blocksPerFloor + floor * columnsPerFloor + slot;
}

int wallId(int floor, int slot, int floors, int columnsPerFloor, int blocksPerFloor, int wallsPerFloor)
{
    return 1 + floors * blocksPerFloor + floors * columnsPerFloor + floor * wallsPerFloor + slot;
}

// Core chunk index for a structural member (node ids are 1-based, Ground is 0).
uint32_t coreChunkIndex(uint32_t nodeId)
{
    return nodeId - 1;
}

uint32_t fragmentChunkIndex(uint32_t nodeId, uint32_t fragmentK, uint32_t structuralChunkCount)
{
    return structuralChunkCount + (nodeId - 1) * kFragmentsPerChunk + fragmentK;
}

bool isFragmentChunk(uint32_t userData)
{
    return (userData & kFragmentUserDataFlag) != 0;
}

uint32_t fragmentNodeId(uint32_t userData)
{
    return (userData & ~kFragmentUserDataFlag) >> 4;
}

uint32_t fragmentIndex(uint32_t userData)
{
    return userData & (kFragmentsPerChunk - 1);
}
}

BlastRuntime::~BlastRuntime()
{
    destroy();
}

void BlastRuntime::destroy()
{
    m_group = nullptr;
    m_actor = nullptr;
    m_asset = nullptr;
    m_structuralChunkCount = 0;
    if (m_framework)
    {
        m_framework->release();
        m_framework = nullptr;
    }
}

bool BlastRuntime::initialize(const std::vector<NodeState>& nodes, int floors, int columnsPerFloor,
                              int blocksPerFloor, int wallsPerFloor, std::string& error)
{
    destroy();
    using namespace Nv::Blast;
    const uint32_t structuralChunkCount = static_cast<uint32_t>(nodes.size() - 1);
    const uint32_t chunkCount = structuralChunkCount * (1 + kFragmentsPerChunk);
    std::vector<NvBlastChunkDesc> chunks(chunkCount);
    for (uint32_t i = 0; i < structuralChunkCount; ++i)
    {
        const NodeState& node = nodes[static_cast<size_t>(i + 1)];
        NvBlastChunkDesc& core = chunks[i];
        core.centroid[0] = static_cast<float>(node.slot) * 2.0f;
        core.centroid[1] = static_cast<float>(node.floor) * 3.0f;
        core.centroid[2] = node.type == NodeType::Slab ? 0.0f : 1.0f;
        core.volume = 1.0f;
        core.parentChunkDescIndex = Invalid;
        core.flags = NvBlastChunkDesc::SupportFlag;
        core.userData = node.id;

        // Pre-fracture pieces. Blast bonds only join support chunks, so every
        // fragment is a root support chunk bonded to its siblings (the seams);
        // the fragment clique forms its own connected component so it stays a
        // single actor while the member is intact.
        for (uint32_t k = 0; k < kFragmentsPerChunk; ++k)
        {
            NvBlastChunkDesc& frag = chunks[structuralChunkCount + i * kFragmentsPerChunk + k];
            frag.centroid[0] = core.centroid[0] + ((k & 1) != 0 ? 0.5f : -0.5f);
            frag.centroid[1] = core.centroid[1] + ((k & 2) != 0 ? 0.5f : -0.5f);
            frag.centroid[2] = core.centroid[2] + ((k & 4) != 0 ? 0.5f : -0.5f);
            frag.volume = 0.125f;
            frag.parentChunkDescIndex = Invalid;
            frag.flags = NvBlastChunkDesc::SupportFlag;
            frag.userData = kFragmentUserDataFlag | (static_cast<uint32_t>(node.id) << 4) | k;
        }
    }

    std::vector<NvBlastBondDesc> bonds;
    for (int floor = 0; floor < floors; ++floor)
    {
        for (int slot = 0; slot < columnsPerFloor; ++slot)
        {
            NvBlastBondDesc bond = {};
            // Vertical column/wall chain: upper column/wall rests on the one
            // below (or on Ground for the first floor).
            if (floor == 0)
            {
                bond.chunkIndices[0] = static_cast<uint32_t>(columnId(floor, slot, floors, columnsPerFloor, blocksPerFloor) - 1);
                bond.chunkIndices[1] = Invalid;
            }
            else
            {
                bond.chunkIndices[0] = static_cast<uint32_t>(columnId(floor - 1, slot, floors, columnsPerFloor, blocksPerFloor) - 1);
                bond.chunkIndices[1] = static_cast<uint32_t>(columnId(floor, slot, floors, columnsPerFloor, blocksPerFloor) - 1);
            }
            bond.bond.area = 1.0f;
            bonds.push_back(bond);
            // A slab bonds to its column/wall only when it exists (slot in
            // block range).
            if (slot < blocksPerFloor)
            {
                NvBlastBondDesc slabBond = {};
                slabBond.chunkIndices[0] = static_cast<uint32_t>(blockId(floor, slot, blocksPerFloor) - 1);
                slabBond.chunkIndices[1] = static_cast<uint32_t>(columnId(floor, slot, floors, columnsPerFloor, blocksPerFloor) - 1);
                slabBond.bond.area = 1.0f;
                bonds.push_back(slabBond);
            }
            if (slot > 0 && slot - 1 < blocksPerFloor && slot < blocksPerFloor)
            {
                NvBlastBondDesc slabSlab = {};
                slabSlab.chunkIndices[0] = static_cast<uint32_t>(blockId(floor, slot - 1, blocksPerFloor) - 1);
                slabSlab.chunkIndices[1] = static_cast<uint32_t>(blockId(floor, slot, blocksPerFloor) - 1);
                slabSlab.bond.area = 1.0f;
                bonds.push_back(slabSlab);
            }
        }
    }

    // Grid walls are extra load bearers appended after the columns. Each wall
    // is chained vertically to the wall below it (or to the world on the
    // ground story), mirroring the column chain so a destroyed wall's support
    // bond below can be fractured.
    for (int floor = 0; floor < floors; ++floor)
    {
        for (int slot = 0; slot < wallsPerFloor; ++slot)
        {
            NvBlastBondDesc bond = {};
            if (floor == 0)
            {
                bond.chunkIndices[0] = static_cast<uint32_t>(wallId(floor, slot, floors, columnsPerFloor, blocksPerFloor, wallsPerFloor) - 1);
                bond.chunkIndices[1] = Invalid;
            }
            else
            {
                bond.chunkIndices[0] = static_cast<uint32_t>(wallId(floor - 1, slot, floors, columnsPerFloor, blocksPerFloor, wallsPerFloor) - 1);
                bond.chunkIndices[1] = static_cast<uint32_t>(wallId(floor, slot, floors, columnsPerFloor, blocksPerFloor, wallsPerFloor) - 1);
            }
            bond.bond.area = 1.0f;
            bonds.push_back(bond);
        }
    }

    // Seams: every member's 4 fragments are bonded to each other so the member
    // behaves as one piece until the seams are fractured.
    for (uint32_t i = 0; i < structuralChunkCount; ++i)
    {
        const uint32_t base = structuralChunkCount + i * kFragmentsPerChunk;
        for (uint32_t a = 0; a < kFragmentsPerChunk; ++a)
        {
            for (uint32_t b = a + 1; b < kFragmentsPerChunk; ++b)
            {
                NvBlastBondDesc seam = {};
                seam.chunkIndices[0] = base + a;
                seam.chunkIndices[1] = base + b;
                seam.bond.area = 1.0f;
                bonds.push_back(seam);
            }
        }
    }

    m_framework = NvBlastTkFrameworkCreate();
    if (!m_framework) { error = "Blast init failed: no framework."; return false; }
    TkAssetDesc desc;
    desc.chunkCount = chunkCount;
    desc.chunkDescs = chunks.data();
    desc.bondCount = static_cast<uint32_t>(bonds.size());
    desc.bondDescs = bonds.data();
    m_framework->ensureAssetExactSupportCoverage(chunks.data(), chunkCount);
    m_asset = m_framework->createAsset(desc);
    if (!m_asset) { error = "Blast init failed: asset creation."; return false; }
    TkActorDesc actorDesc(m_asset);
    m_actor = m_framework->createActor(actorDesc);
    m_group = m_framework->createGroup({1});
    if (!m_actor || !m_group || !m_group->addActor(*m_actor))
    {
        error = "Blast init failed: actor/group.";
        return false;
    }
    m_structuralChunkCount = structuralChunkCount;
    return true;
}

Nv::Blast::TkActor* BlastRuntime::findActorForChunk(uint32_t chunkIndex)
{
    if (!m_asset || !m_group) return nullptr;
    const auto graph = m_asset->getGraph();
    const uint32_t actorCount = m_group->getActorCount();
    if (actorCount == 0) return nullptr;
    std::vector<Nv::Blast::TkActor*> actors(actorCount);
    m_group->getActors(actors.data(), actorCount);
    for (Nv::Blast::TkActor* actor : actors)
    {
        if (!actor) continue;
        const uint32_t nodeCount = actor->getGraphNodeCount();
        if (nodeCount == 0) continue;
        std::vector<uint32_t> nodeIndices(nodeCount);
        actor->getGraphNodeIndices(nodeIndices.data(), nodeCount);
        for (uint32_t graphNode : nodeIndices)
        {
            if (graphNode < graph.nodeCount && graph.chunkIndices[graphNode] == chunkIndex)
                return actor;
        }
    }
    return nullptr;
}

void BlastRuntime::fractureMember(const NodeState& node, int floors, int columnsPerFloor,
                                  int blocksPerFloor, int wallsPerFloor, std::vector<FragmentSpawnInfo>& outFragments)
{
    outFragments.clear();
    if (!m_asset || !m_group || m_structuralChunkCount == 0) return;
    using namespace Nv::Blast;

    const uint32_t nodeId = static_cast<uint32_t>(node.id);
    const auto graph = m_asset->getGraph();

    // Map chunk index -> graph node index once.
    std::vector<uint32_t> nodeOfChunk(m_asset->getChunkCount(), Invalid);
    for (uint32_t graphNode = 0; graphNode < graph.nodeCount; ++graphNode)
    {
        const uint32_t chunk = graph.chunkIndices[graphNode];
        if (chunk != Invalid && chunk < nodeOfChunk.size())
            nodeOfChunk[chunk] = graphNode;
    }

    std::vector<NvBlastBondFractureData> commands;

    // Break the seams of this member's fragment clique so its 4 fragments
    // split into separate actors.
    const uint32_t fragBase = fragmentChunkIndex(nodeId, 0, m_structuralChunkCount);
    for (uint32_t a = 0; a < kFragmentsPerChunk; ++a)
    {
        for (uint32_t b = a + 1; b < kFragmentsPerChunk; ++b)
        {
            const uint32_t chunkA = fragmentChunkIndex(nodeId, a, m_structuralChunkCount);
            const uint32_t chunkB = fragmentChunkIndex(nodeId, b, m_structuralChunkCount);
            const uint32_t graphA = nodeOfChunk[chunkA];
            const uint32_t graphB = nodeOfChunk[chunkB];
            if (graphA == Invalid || graphB == Invalid) continue;
            NvBlastBondFractureData cmd = {};
            cmd.nodeIndex0 = graphA;
            cmd.nodeIndex1 = graphB;
            cmd.health = 1.0f;
            commands.push_back(cmd);
        }
    }

    TkActor* seamActor = findActorForChunk(fragBase);
    if (seamActor && !commands.empty())
    {
        const NvBlastFractureBuffers seamCommands = {
            static_cast<uint32_t>(commands.size()), 0, commands.data(), nullptr};
        seamActor->applyFracture(nullptr, &seamCommands);
    }

    // Break the member's bond to the support below (columns/walls only). This
    // keeps the original "destroyed member detaches from the structure" Blast
    // behaviour for the load-bearing members.
    if (node.type == NodeType::Column || node.type == NodeType::Wall)
    {
        const uint32_t core = coreChunkIndex(nodeId);
        // Grid walls live in their own id range after the columns; house walls
        // share the column range. The support below is the same-kind member on
        // the floor below (or the world node for the ground story).
        uint32_t lowerChunk = Invalid;
        if (node.floor > 0)
        {
            lowerChunk = node.type == NodeType::Wall && wallsPerFloor > 0
                ? coreChunkIndex(static_cast<uint32_t>(
                    wallId(node.floor - 1, node.slot, floors, columnsPerFloor, blocksPerFloor, wallsPerFloor)))
                : coreChunkIndex(static_cast<uint32_t>(
                    columnId(node.floor - 1, node.slot, floors, columnsPerFloor, blocksPerFloor)));
        }
        const uint32_t graphCore = nodeOfChunk[core];
        // Find the bond from the core to the lower chunk (or to the world node
        // for the ground story) through the graph adjacency.
        uint32_t bondIndex = Invalid, fractureNode0 = Invalid, fractureNode1 = Invalid;
        for (uint32_t graphNode = 0; graphNode < graph.nodeCount; ++graphNode)
        {
            if (graphNode != graphCore) continue;
            for (uint32_t i = graph.adjacencyPartition[graphNode]; i < graph.adjacencyPartition[graphNode + 1]; ++i)
            {
                const uint32_t adjacentNode = graph.adjacentNodeIndices[i];
                const uint32_t other = adjacentNode < graph.nodeCount ? graph.chunkIndices[adjacentNode] : Invalid;
                if ((node.floor == 0 && other == Invalid) || (node.floor > 0 && other == lowerChunk))
                {
                    bondIndex = graph.adjacentBondIndices[i];
                    fractureNode0 = graphNode;
                    fractureNode1 = adjacentNode;
                    break;
                }
            }
        }
        TkActor* structureActor = graphCore < graph.nodeCount ? findActorForChunk(core) : nullptr;
        if (structureActor && bondIndex != Invalid)
        {
            NvBlastBondFractureData cmd = {};
            cmd.nodeIndex0 = fractureNode0;
            cmd.nodeIndex1 = fractureNode1;
            cmd.health = 1.0f;
            const NvBlastFractureBuffers structuralCommands = {1, 0, &cmd, nullptr};
            structureActor->applyFracture(nullptr, &structuralCommands);
        }
    }

    m_group->process();

    // Collect the visible fragments that split off. Only fragments belonging
    // to this member are reported; the rest of the structure is untouched.
    const uint32_t actorCount = m_group->getActorCount();
    if (actorCount == 0) return;
    std::vector<TkActor*> actors(actorCount);
    m_group->getActors(actors.data(), actorCount);
    for (TkActor* actor : actors)
    {
        if (!actor) continue;
        const uint32_t visibleCount = actor->getVisibleChunkCount();
        if (visibleCount == 0) continue;
        std::vector<uint32_t> visible(visibleCount);
        actor->getVisibleChunkIndices(visible.data(), visibleCount);
        for (uint32_t chunkIndex : visible)
        {
            if (chunkIndex >= m_asset->getChunkCount()) continue;
            const uint32_t userData = m_asset->getChunks()[chunkIndex].userData;
            if (isFragmentChunk(userData) && fragmentNodeId(userData) == nodeId)
            {
                FragmentSpawnInfo info;
                info.nodeId = node.id;
                info.fragmentIndex = static_cast<int>(fragmentIndex(userData));
                outFragments.push_back(info);
            }
        }
    }
}
}
