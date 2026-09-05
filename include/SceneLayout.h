#pragma once

#include "BlastSupportModel.h"
#include "GeometryDerived.h"
#include "NodeTypes.h"

namespace blast_demo
{
// BoxLayout now lives in NodeTypes.h (shared, no dependency on the model).

constexpr float kColumnSpacing = 2.6f;
constexpr float kFloorHeight = 2.2f;
// Floor plate thickness. A column rises from its story floor to the underside
// of the plate above it, so its height is kFloorHeight - kSlabThickness. This
// leaves a gap between a column's top and the column above's bottom: columns
// do NOT touch end-to-end; the floor plate transfers the load between them.
constexpr float kSlabThickness = 0.4f;
constexpr float kColumnHeight = kFloorHeight - kSlabThickness;   // 1.8

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

// Layout category used ONLY to pick a member's geometric shape when building a
// preset. It is not stored on the node and never drives analysis: at runtime
// the role is derived from the box (GeometryDerived::deriveRole). Slab = floor
// plate, Column = square bearing, Wall = thin edge bearing.
enum class LayoutKind { Slab, Column, Wall };

inline BoxLayout nodeLayout(int floor, int slot, LayoutKind kind,
                            int activeColumns, int activeBlocks, bool houseMode)
{
    BoxLayout layout;
    if (houseMode)
    {
        if (kind == LayoutKind::Wall)
        {
            // Wall segment rises through its whole story. Eight segments per
            // floor enclose the house: north/south split in two along X,
            // east/west split in two along Z.
            layout.cy = static_cast<float>(floor) * kFloorHeight + kFloorHeight * 0.5f;
            layout.hy = kFloorHeight * 0.5f;
            const float halfLenX = kHouseHalfX * 0.5f;
            const float halfLenZ = kHouseHalfZ * 0.5f;
            switch (slot)
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
        else if (kind == LayoutKind::Slab)
        {
            // Floor plate capping the wall enclosure of its story. Four slab
            // nodes per story are tiled 2x2 so they form one continuous floor
            // without overlapping (slot: 0=NW, 1=NE, 2=SW, 3=SE).
            const float half = kHouseHalfX - kHouseSlabInset;
            const bool north = slot == 0 || slot == 1;
            const bool west = slot == 0 || slot == 2;
            layout.cx = (west ? -1.0f : 1.0f) * half * 0.5f;
            layout.cz = (north ? -1.0f : 1.0f) * half * 0.5f;
            layout.cy = static_cast<float>(floor) * kFloorHeight + kFloorHeight + 0.05f;
            layout.hx = half * 0.5f;
            layout.hy = 0.12f;
            layout.hz = half * 0.5f;
        }
        else
        {
            layout.cy = static_cast<float>(floor) * kFloorHeight + kFloorHeight * 0.5f;
            layout.hx = 0.3f;
            layout.hy = kFloorHeight * 0.5f;
            layout.hz = 0.3f;
        }
        return layout;
    }
    else if (activeColumns == 2 && activeBlocks == 4 && !houseMode)
    {
        // ShearPair layout: 4 plates in a single row across X, bridging
        // between just two columns at the row's ends (plates 0 and 3 sit on
        // the two end columns; plates 1 and 2 span the middle with no column
        // beneath them). The middle plates are held only by horizontal
        // (lateral-shear) bonds to their neighbours, so a destroyed end column
        // forces the whole row to relay its load sideways — the ideal test bed
        // for detectLateralShear.
        if (kind == LayoutKind::Slab)
        {
            // 4 plates side by side along X, each fully spanning the row width.
            layout.cy = static_cast<float>(floor) * kFloorHeight + kColumnHeight + kSlabThickness * 0.5f;
            layout.cx = slotCenterX(slot, 4);
            layout.cz = 0.0f;
            layout.hx = 1.28f;                 // adjacent plates overlap -> horizontalContact
            layout.hy = kSlabThickness * 0.5f;
            layout.hz = 1.28f;
        }
        else
        {
            // Two end columns directly under the outermost plates 0 and 3. The
            // plate row spans x = +/-(1.5 * kColumnSpacing) = +/-3.9; the end
            // plates sit there, so the columns go at those same ±3.9 positions.
            layout.cy = static_cast<float>(floor) * kFloorHeight + kColumnHeight * 0.5f;
            layout.cx = (slot == 0 ? -1.0f : 1.0f) * (1.5f * kColumnSpacing);
            layout.cz = 0.0f;
            layout.hx = 0.3f;
            layout.hy = kColumnHeight * 0.5f;
            layout.hz = 0.3f;
        }
        return layout;
    }
    else if (isGridLayout(activeColumns, activeBlocks))
    {
        // 4x4 block grid: blocks are laid out on the XZ plane (x: 4 columns,
        // z: 4 rows) and a column stands at each corner of the grid, rising
        // from its story floor. The block sits on top of its supporting column.
        if (kind == LayoutKind::Slab)
        {
            layout.cx = gridPointX(gridCol(slot));
            layout.cz = gridPointZ(gridRow(slot));
            // Floor plate sits on top of its story's columns.
            layout.cy = static_cast<float>(floor) * kFloorHeight + kColumnHeight + kSlabThickness * 0.5f;
            layout.hx = 1.28f;
            layout.hy = kSlabThickness * 0.5f;
            layout.hz = 1.28f;
        }
        else if (kind == LayoutKind::Wall)
        {
            // Load-bearing walls stand on the outer edges of the floor slab
            // and rise from their story floor to the plate underside (like a
            // column), so consecutive walls do not touch end-to-end.
            const float edge = static_cast<float>(kGridSize - 1) * 0.5f * kColumnSpacing + 1.28f;
            if (slot < 4)
            {
                layout.cx = gridPointX(slot);
                layout.cz = -edge;                 // north edge of the slab
                layout.hx = 1.28f;                 // one floor block wide along X
                layout.hz = 0.15f;                 // thin along Z
            }
            else
            {
                layout.cx = -edge;                 // west edge of the slab
                layout.cz = gridPointZ(slot - 4);
                layout.hx = 0.15f;                 // thin along X
                layout.hz = 1.28f;                 // one floor block wide along Z
            }
            layout.cy = static_cast<float>(floor) * kFloorHeight + kColumnHeight * 0.5f;
            layout.hy = kColumnHeight * 0.5f;
        }
        else
        {
            layout.cx = gridPointX(gridColumnCol(slot));
            layout.cz = gridPointZ(gridColumnRow(slot));
            // Column rises from its story floor to the plate underside; the
            // ground-story column rests exactly on the ground plane. The grid
            // column is thicker than a 1:1 column so two destroyed columns
            // release more load than a single wall segment (keeps the
            // "one failure survives, two columns collapse" tuning).
            layout.cy = static_cast<float>(floor) * kFloorHeight + kColumnHeight * 0.5f;
            layout.hx = 0.4f;
            layout.hy = kColumnHeight * 0.5f;
            layout.hz = 0.4f;
        }
        return layout;
    }
    layout.cx = slotCenterX(slot, activeColumns);
    layout.cz = 0.0f;
    if (kind == LayoutKind::Slab)
    {
        // Floor plate sits on top of its story's columns: bottom at the column
        // top, top at the storey ceiling. Its centre is at
        // floor*H + kColumnHeight + kSlabThickness/2.
        layout.cy = static_cast<float>(floor) * kFloorHeight + kColumnHeight + kSlabThickness * 0.5f;
        layout.hx = 1.28f;
        layout.hy = kSlabThickness * 0.5f;
        layout.hz = 0.9f;
    }
    else
    {
        // Column rises from its story floor to the underside of the plate above
        // it (does NOT reach the storey ceiling), so consecutive columns do not
        // touch end-to-end: the floor plate transfers the load between them.
        layout.cy = static_cast<float>(floor) * kFloorHeight + kColumnHeight * 0.5f;
        layout.hx = 0.3f;
        layout.hy = kColumnHeight * 0.5f;
        layout.hz = 0.3f;
    }
    return layout;
}
}
