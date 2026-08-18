#include "BlastRuntime.h"
#include "BlastSupportModel.h"
#include "GeometryDerived.h"
#include "NodeTypes.h"

#include <limits>
#include <set>
#include <utility>

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

bool BlastRuntime::initialize(const std::vector<NodeState>& nodes, const std::vector<EdgeState>& edges,
                              int floors, int columnsPerFloor, int blocksPerFloor, int wallsPerFloor,
                              std::string& error)
{
    (void)floors;
    (void)columnsPerFloor;
    (void)blocksPerFloor;
    (void)wallsPerFloor;
    destroy();
    using namespace Nv::Blast;
    const uint32_t structuralChunkCount = static_cast<uint32_t>(nodes.size() - 1);
    const uint32_t chunkCount = structuralChunkCount * (1 + kFragmentsPerChunk);
    std::vector<NvBlastChunkDesc> chunks(chunkCount);
    for (uint32_t i = 0; i < structuralChunkCount; ++i)
    {
        const NodeState& node = nodes[static_cast<size_t>(i + 1)];
        NvBlastChunkDesc& core = chunks[i];
        core.centroid[0] = node.box.cx;
        core.centroid[1] = node.box.cy;
        core.centroid[2] = node.box.cz;
        core.volume = 1.0f;
        core.parentChunkDescIndex = Invalid;
        core.flags = NvBlastChunkDesc::SupportFlag;
        core.userData = node.id;

        // Pre-fracture pieces. Blast bonds only join support chunks, so every
        // fragment is a root support chunk bonded to its siblings (the seams);
        // the fragment clique forms its own connected component so it stays a
        // single actor while the member is intact. Blast represents a support
        // chunk as a sphere of radius ~0.31 (volume 0.125); the eight fragment
        // centroids must sit far enough apart (>= ~0.62) that they do not
        // overlap, or Blast's exact-support-coverage pass merges them into
        // fewer pieces. A fixed 0.5 offset keeps them 1.0 apart regardless of
        // member aspect ratio (the visual fragment size is computed separately
        // by the physics layer from the member's box).
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

    // Bonds mirror the support edges: every physical contact between two
    // members (or a member and the world) becomes one bond. A Ground edge
    // (to == 0) bonds the member to the world node (Invalid). Bidirectional
    // horizontal edges are deduplicated.
    std::vector<NvBlastBondDesc> bonds;
    std::set<std::pair<uint32_t, uint32_t>> seen;
    for (const EdgeState& edge : edges)
    {
        if (!edge.alive) continue;
        const uint32_t a = edge.from == 0 ? Invalid : static_cast<uint32_t>(edge.from - 1);
        const uint32_t b = edge.to == 0 ? Invalid : static_cast<uint32_t>(edge.to - 1);
        if (a == Invalid && b == Invalid) continue;
        const auto key = a < b ? std::make_pair(a, b) : std::make_pair(b, a);
        if (!seen.insert(key).second) continue;
        NvBlastBondDesc bond = {};
        bond.chunkIndices[0] = a;
        bond.chunkIndices[1] = b;
        bond.bond.area = 1.0f;
        bonds.push_back(bond);
    }

    // Seams: every member's 8 fragments are bonded to each other so the member
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

void BlastRuntime::fractureMember(const NodeState& node, const std::vector<EdgeState>& edges,
                                  int floors, int columnsPerFloor, int blocksPerFloor, int wallsPerFloor,
                                  std::vector<FragmentSpawnInfo>& outFragments)
{
    (void)floors;
    (void)columnsPerFloor;
    (void)blocksPerFloor;
    (void)wallsPerFloor;
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

    // Break the seams of this member's fragment clique so its fragments split
    // into separate actors.
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

    // Break a member's bond to the support below. The support below is whatever
    // member this one rests on (the FROM side of a downward vertical edge), or
    // the world for the ground story. This keeps the original "destroyed member
    // detaches from the structure" behaviour, and it now also applies to floor
    // plates: a plate that fails (e.g. laterally) genuinely separates from the
    // support beneath it instead of shattering into cosmetic fragments only.
    if (deriveRole(node.box) == MemberRole::VerticalBearing ||
        deriveRole(node.box) == MemberRole::HorizontalPlate)
    {
        const uint32_t core = coreChunkIndex(nodeId);
        uint32_t lowerChunk = Invalid;
        for (const EdgeState& edge : edges)
        {
            if (!edge.alive || edge.from != node.id) continue;
            // Downward edge: this member is above its support.
            lowerChunk = edge.to == 0 ? Invalid : static_cast<uint32_t>(edge.to - 1);
            break;
        }
        const uint32_t graphCore = nodeOfChunk[core];
        uint32_t bondIndex = Invalid, fractureNode0 = Invalid, fractureNode1 = Invalid;
        for (uint32_t graphNode = 0; graphNode < graph.nodeCount; ++graphNode)
        {
            if (graphNode != graphCore) continue;
            for (uint32_t i = graph.adjacencyPartition[graphNode]; i < graph.adjacencyPartition[graphNode + 1]; ++i)
            {
                const uint32_t adjacentNode = graph.adjacentNodeIndices[i];
                const uint32_t other = adjacentNode < graph.nodeCount ? graph.chunkIndices[adjacentNode] : Invalid;
                if (other == lowerChunk)
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
