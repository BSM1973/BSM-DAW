#include "PolyphonicStepGridLayout.h"
#include <cassert>
#include <iostream>
using LibertyPolyGridUI::Layout;
int main()
{
    for (const auto w : {510, 650, 800, 1150, 1440})
        for (const auto h : {230, 300, 380, 520})
        {
            const auto l = Layout::make(w, h);
            assert(l.valid);
            assert(l.gridWidth >= 220 && l.rowHeight >= 12);
            assert(l.clipPanel.x >= 0 && l.clipPanel.y >= 0);
            assert(l.editorArea.y + l.editorArea.h <= h);
            assert(!l.clipPanel.intersects(l.parameterHeader));
            assert(!l.clipPanel.intersects(l.gridHeader));
            assert(!l.editorArea.intersects(l.cells[7][15]));
            for (int row = 0; row < 8; ++row)
            {
                assert(l.pitchLabels[row].w > 0);
                assert(l.muteButtons[row].w > 0 && l.soloButtons[row].w > 0);
                for (int step = 0; step < 16; ++step)
                {
                    const auto r = l.cells[row][step];
                    assert(r.w > 0 && r.h > 0 && r.x + r.w <= w);
                    assert(r.y + r.h <= h);
                    if (step) assert(!r.intersects(l.cells[row][step - 1]));
                    if (row) assert(!r.intersects(l.cells[row - 1][step]));
                }
                for (int c = 0; c < 4; ++c)
                {
                    assert(l.rowControls[row][c].w > 0);
                    assert(!l.rowControls[row][c].intersects(l.cells[row][0]));
                    if (c) assert(!l.rowControls[row][c].intersects(l.rowControls[row][c - 1]));
                }
            }
            for (int i = 0; i < 6; ++i)
            {
                const auto r = l.editorTabs[i];
                assert(r.w > 0 && r.h > 0);
                if (i) assert(!r.intersects(l.editorTabs[i - 1]));
            }
        }
    assert(!Layout::make(400, 200).valid);
    const auto page3 = Layout::make(1150, 510, 3);
    assert(page3.firstStep == 48);
    std::cout << "Liberty polyphonic layout: 8 rows, 16 cols, no overlap OK\n";
}
