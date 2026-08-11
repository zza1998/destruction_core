#pragma once

#include "BlastSupportModel.h"

namespace blast_demo
{
struct BoxLayout
{
    float cx = 0.0f;
    float cy = 0.0f;
    float cz = 0.0f;
    float hx = 1.0f;
    float hy = 1.0f;
    float hz = 1.0f;
};

constexpr float kColumnSpacing = 2.6f;
constexpr float kFloorHeight = 2.2f;

// House footprint: four walls enclose a square plan, and each floor slab is a
// horizontal plate spanning the enclosure interior (slightly smaller than the
// outer walls so it sits inside them).
constexpr float kHouseHalfX = 2.2f;
constexpr float kHouseHalfZ = 2.2f;
constexpr float kHouseWallHalfThickness = 0.18f;
constexpr float kHouseSlabInset = 0.25f;

inline float slotCenterX(int slot, int activeColumns)
{
    return (static_cast<float>(slot) - static_cast<float>(activeColumns - 1) * 0.5f) * kColumnSpacing;
}

// Grid layout helpers. A grid floor tiles its blocks in a kGridSize x kGridSize
// pattern while the four columns stand at the four corners of the grid; every
// column supports the 2x2 quadrant of blocks nearest to it.
constexpr int kGridSize = 4;

inline bool isGridLayout(int activeColumns, int activeBlocks)
{
    return activeBlocks > activeColumns;
}

inline int gridCol(int slot) { return slot % kGridSize; }
inline int gridRow(int slot) { return slot / kGridSize; }

inline int gridColumnCol(int columnSlot) { return columnSlot % 2 == 0 ? 0 : kGridSize - 1; }
inline int gridColumnRow(int columnSlot) { return columnSlot < 2 ? 0 : kGridSize - 1; }

// Column slot (0..3) supporting the block at (col,row): NW, NE, SW, SE corners.
inline int nearestColumnSlot(int col, int row)
{
    return (row < kGridSize / 2 ? 0 : 2) + (col < kGridSize / 2 ? 0 : 1);
}

// Physical adjacency between a grid block and a corner column. A corner column
// stands at the corner of the 4x4 block grid and bonds to the corner block at
// that corner plus the two edge blocks immediately beside it along the slab
// edges (the 2x2 corner quadrant minus the diagonal block, which only touches
// the column at a single point). Interior blocks are never adjacent to a
// column. Column slot order: 0 = NW (0,0), 1 = NE (3,0), 2 = SW (0,3),
// 3 = SE (3,3).
inline bool gridBlockAdjacentToColumn(int col, int row, int columnSlot)
{
    const int cc = (columnSlot % 2 == 0) ? 0 : kGridSize - 1;
    const int cr = (columnSlot < 2) ? 0 : kGridSize - 1;
    const int dc = cc == 0 ? 1 : -1;
    const int dr = cr == 0 ? 1 : -1;
    const int c0 = dc > 0 ? cc : cc + dc;
    const int c1 = dc > 0 ? cc + dc : cc;
    const int r0 = dr > 0 ? cr : cr + dr;
    const int r1 = dr > 0 ? cr + dr : cr;
    if (col < c0 || col > c1 || row < r0 || row > r1) return false;
    if (col == cc + dc && row == cr + dr) return false;
    return true;
}

inline float gridPointX(int col) { return slotCenterX(col, kGridSize); }
inline float gridPointZ(int row) { return slotCenterX(row, kGridSize); }

inline BoxLayout nodeLayout(const NodeState& node, int activeColumns, int activeBlocks, bool houseMode)
{
    BoxLayout layout;
    if (houseMode)
    {
        if (node.type == NodeType::Wall)
        {
            // Wall segment rises through its whole story. Eight segments per
            // floor enclose the house: north/south split in two along X,
            // east/west split in two along Z.
            layout.cy = static_cast<float>(node.floor) * kFloorHeight + kFloorHeight * 0.5f;
            layout.hy = kFloorHeight * 0.5f;
            const float halfLenX = kHouseHalfX * 0.5f;
            const float halfLenZ = kHouseHalfZ * 0.5f;
            switch (node.slot)
            {
            case 0: // North-west wall segment
                layout.cx = -halfLenX; layout.cz = -kHouseHalfZ;
                layout.hx = halfLenX; layout.hz = kHouseWallHalfThickness;
                break;
            case 1: // North-east wall segment
                layout.cx = halfLenX; layout.cz = -kHouseHalfZ;
                layout.hx = halfLenX; layout.hz = kHouseWallHalfThickness;
                break;
            case 2: // South-west wall segment
                layout.cx = -halfLenX; layout.cz = kHouseHalfZ;
                layout.hx = halfLenX; layout.hz = kHouseWallHalfThickness;
                break;
            case 3: // South-east wall segment
                layout.cx = halfLenX; layout.cz = kHouseHalfZ;
                layout.hx = halfLenX; layout.hz = kHouseWallHalfThickness;
                break;
            case 4: // East-north wall segment
                layout.cx = kHouseHalfX; layout.cz = -halfLenZ;
                layout.hx = kHouseWallHalfThickness; layout.hz = halfLenZ;
                break;
            case 5: // East-south wall segment
                layout.cx = kHouseHalfX; layout.cz = halfLenZ;
                layout.hx = kHouseWallHalfThickness; layout.hz = halfLenZ;
                break;
            case 6: // West-north wall segment
                layout.cx = -kHouseHalfX; layout.cz = -halfLenZ;
                layout.hx = kHouseWallHalfThickness; layout.hz = halfLenZ;
                break;
            case 7: // West-south wall segment
                layout.cx = -kHouseHalfX; layout.cz = halfLenZ;
                layout.hx = kHouseWallHalfThickness; layout.hz = halfLenZ;
                break;
            default:
                layout.cx = 0.0f; layout.cz = 0.0f;
                layout.hx = 0.3f; layout.hz = 0.3f;
                break;
            }
        }
        else if (node.type == NodeType::Slab)
        {
            // Floor plate capping the wall enclosure of its story. Four slab
            // nodes per story are tiled 2x2 so they form one continuous floor
            // without overlapping (slot: 0=NW, 1=NE, 2=SW, 3=SE).
            const float half = kHouseHalfX - kHouseSlabInset;
            const bool north = node.slot == 0 || node.slot == 1;
            const bool west = node.slot == 0 || node.slot == 2;
            layout.cx = (west ? -1.0f : 1.0f) * half * 0.5f;
            layout.cz = (north ? -1.0f : 1.0f) * half * 0.5f;
            layout.cy = static_cast<float>(node.floor) * kFloorHeight + kFloorHeight + 0.05f;
            layout.hx = half * 0.5f;
            layout.hy = 0.12f;
            layout.hz = half * 0.5f;
        }
        else
        {
            layout.cy = static_cast<float>(node.floor) * kFloorHeight + kFloorHeight * 0.5f;
            layout.hx = 0.3f;
            layout.hy = kFloorHeight * 0.5f;
            layout.hz = 0.3f;
        }
        return layout;
    }
    else if (isGridLayout(activeColumns, activeBlocks))
    {
        // 4x4 block grid: blocks are laid out on the XZ plane (x: 4 columns,
        // z: 4 rows) and a column stands at each corner of the grid, rising
        // from its story floor. The block sits on top of its supporting column.
        if (node.type == NodeType::Slab)
        {
            layout.cx = gridPointX(gridCol(node.slot));
            layout.cz = gridPointZ(gridRow(node.slot));
            // Floor plate sits on top of its story's columns: its center is at
            // the column-top height, slightly above the column tops.
            layout.cy = static_cast<float>(node.floor) * kFloorHeight + kFloorHeight + 0.05f;
            layout.hx = 1.28f;
            layout.hy = 0.2f;
            layout.hz = 1.28f;
        }
        else if (node.type == NodeType::Wall)
        {
            // Load-bearing walls stand on the outer edges of the floor slab
            // and rise through their whole story like a column. Eight segments
            // per floor: slot 0..3 split the north wall into four pieces (one
            // per grid column, each one floor block wide, thin along Z and
            // sitting along the north edge) and slot 4..7 split the west wall
            // into four pieces (one per grid row, thin along X, sitting along
            // the west edge). The north-west corner segments meet at the
            // corner of the slab.
            const float edge = static_cast<float>(kGridSize - 1) * 0.5f * kColumnSpacing + 1.28f;
            if (node.slot < 4)
            {
                layout.cx = gridPointX(node.slot);
                layout.cz = -edge;                 // north edge of the slab
                layout.hx = 1.28f;                 // one floor block wide along X
                layout.hz = 0.15f;                 // thin along Z
            }
            else
            {
                layout.cx = -edge;                 // west edge of the slab
                layout.cz = gridPointZ(node.slot - 4);
                layout.hx = 0.15f;                 // thin along X
                layout.hz = 1.28f;                 // one floor block wide along Z
            }
            layout.cy = static_cast<float>(node.floor) * kFloorHeight + kFloorHeight * 0.5f;
            layout.hy = kFloorHeight * 0.5f;
        }
        else
        {
            layout.cx = gridPointX(gridColumnCol(node.slot));
            layout.cz = gridPointZ(gridColumnRow(node.slot));
            // Column stands on its story floor: bottom at y = floor*height,
            // so the ground-story column rests exactly on the ground plane.
            layout.cy = static_cast<float>(node.floor) * kFloorHeight + kFloorHeight * 0.5f;
            layout.hx = 0.3f;
            layout.hy = kFloorHeight * 0.5f;
            layout.hz = 0.3f;
        }
        return layout;
    }
    layout.cx = slotCenterX(node.slot, activeColumns);
    layout.cz = 0.0f;
    if (node.type == NodeType::Slab)
    {
        // Floor plate sits on top of its story's columns: center at column-top
        // height, slightly above the column tops.
        layout.cy = static_cast<float>(node.floor) * kFloorHeight + kFloorHeight + 0.05f;
        layout.hx = 1.28f;
        layout.hy = 0.2f;
        layout.hz = 0.9f;
    }
    else
    {
        // Column rises from its story floor: bottom at y = floor*height, so the
        // ground-story column sits exactly on the ground plane.
        layout.cy = static_cast<float>(node.floor) * kFloorHeight + kFloorHeight * 0.5f;
        layout.hx = 0.3f;
        layout.hy = kFloorHeight * 0.5f;
        layout.hz = 0.3f;
    }
    return layout;
}
}
