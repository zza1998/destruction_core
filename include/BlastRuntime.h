#pragma once

#include <string>
#include <vector>

#include "NvBlastTkActor.h"
#include "NvBlastTkAsset.h"
#include "NvBlastTkFramework.h"
#include "NvBlastTkGroup.h"

namespace blast_demo
{
struct NodeState;

// A visual debris piece split off a structural member. Each member is
// pre-fractured into 8 fragments (2x2x2); fragmentIndex is 0..7.
struct FragmentSpawnInfo
{
    int nodeId = 0;
    int fragmentIndex = 0;
};

class BlastRuntime
{
public:
    ~BlastRuntime();

    bool initialize(const std::vector<NodeState>& nodes, int floors, int columnsPerFloor,
                    int blocksPerFloor, int wallsPerFloor, std::string& error);
    void destroy();

    // Break the member's fragment seams (and, for columns/walls, its bond to
    // the support below), process the group, and report the visible fragments
    // that split off as a result.
    void fractureMember(const NodeState& node, int floors, int columnsPerFloor,
                        int blocksPerFloor, int wallsPerFloor, std::vector<FragmentSpawnInfo>& outFragments);

private:
    Nv::Blast::TkActor* findActorForChunk(uint32_t chunkIndex);

    Nv::Blast::TkFramework* m_framework = nullptr;
    Nv::Blast::TkAsset* m_asset = nullptr;
    Nv::Blast::TkActor* m_actor = nullptr;
    Nv::Blast::TkGroup* m_group = nullptr;
    uint32_t m_structuralChunkCount = 0;
};
}
