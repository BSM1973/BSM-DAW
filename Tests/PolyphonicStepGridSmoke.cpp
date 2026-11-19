#include "PolyphonicStepGrid.h"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <limits>

int main()
{
    LibertyPolyphonicGrid grid;
    assert(!grid.active && grid.stepCount == 16 && grid.stepTicks == 240);
    const int pitches[8] = {72, 71, 69, 67, 65, 64, 62, 60};
    for (int i = 0; i < 8; ++i) assert(grid.laneAt(i)->pitch == pitches[i]);
    assert(grid.laneAt(-1) == nullptr && grid.laneAt(8) == nullptr);
    assert(grid.cycleTicks() == 3840 && grid.render().empty());

    // Same position in two distinct pitch lanes must produce two MIDI notes.
    const auto c4 = grid.addNote(0, 0, 240, 100);
    const auto b3 = grid.addNote(1, 0, 480, 87);
    const auto e3 = grid.addNote(5, 2, 720, 96);
    assert(c4 != 0 && b3 != 0 && e3 != 0 && c4 != b3 && b3 != e3);
    assert(grid.noteAt(0, 0)->id == c4 && grid.noteAt(1, 0)->id == b3);
    assert(grid.noteAt(5, 2)->lengthTicks == 720);
    assert(grid.addNote(0, 0, 240) == 0);
    assert(grid.addNote(8, 0, 240) == 0);
    assert(grid.addNote(0, 16, 240) == 0);
    assert(grid.addNote(0, 3, 0) == 0);
    assert(grid.addNote(0, 3, 240, 0) == 0);

    grid.active = true;
    const auto notes = grid.render(1000);
    assert(notes.size() == 3);
    assert(notes[0].startTick == 1000 && notes[1].startTick == 1000);
    assert(notes[0].pitch == 71 && notes[0].lengthTicks == 480);
    assert(notes[1].pitch == 72 && notes[1].lengthTicks == 240);
    assert(notes[2].startTick == 1480 && notes[2].pitch == 64);
    assert(notes[2].lengthTicks == 720);
    assert(grid.render(std::numeric_limits<std::int64_t>::max()).empty());

    // A note lasting three steps is separate from neighbouring short notes.
    assert(grid.changeLength(5, e3, 960));
    assert(grid.noteAt(5, 2)->lengthTicks == 960);
    assert(grid.noteAt(0, 0)->lengthTicks == 240);
    const auto neighbour = grid.addNote(5, 6, 240);
    assert(neighbour > 0 && grid.render().size() == 4);

    // Individual notes and entire rows can be muted / soloed without losing data.
    grid.laneAt(0)->muted = true;
    assert(grid.render().size() == 3);
    grid.laneAt(1)->solo = true;
    assert(grid.render().size() == 1 && grid.render()[0].pitch == 71);
    grid.laneAt(1)->solo = false;
    grid.laneAt(0)->muted = false;
    grid.noteAt(0, 0); // Non-owning lookup for editor hit testing.
    assert(grid.removeNote(1, b3));
    assert(!grid.removeNote(1, b3));
    assert(grid.render().size() == 3);

    // Duration is capped to the end of the musical loop, not the next note.
    const auto final = grid.addNote(7, 15, 1000);
    assert(final != 0 && grid.noteAt(7, 15)->lengthTicks == 240);
    assert(grid.changeLength(7, final, 10000));
    assert(grid.noteAt(7, 15)->lengthTicks == 240);
    assert(!grid.changeLength(7, final, 0));
    assert(grid.render().size() == 4);

    // Saved note identifiers can be restored safely, without duplicate IDs.
    LibertyPolyphonicGrid restored;
    restored.active = true;
    assert(restored.importNote(0, {123, 1, 480, 110, 1, 72, false}));
    assert(!restored.importNote(1, {123, 2, 480, 110, 1, 72, false}));
    assert(!restored.importNote(0, {124, 1, 480, 110, 1, 72, false}));
    assert(!restored.importNote(0, {125, 17, 480, 110, 1, 72, false}));
    const auto next = restored.addNote(1, 3, 960);
    assert(next > 123 && restored.render().size() == 2);
    // Live row controls affect actual exported MIDI timing, not just labels.
    restored.laneAt(1)->rate = 1; // Eighth-note row.
    restored.laneAt(1)->reverse = true;
    restored.laneAt(1)->steps = 8;
    restored.laneAt(1)->swing = 50;
    auto reversed = restored.render();
    assert(reversed.size() == 2);
    assert(reversed[1].pitch == 71);
    assert(reversed[1].startTick == 4 * 480);
    restored.laneAt(1)->reverse = false;
    reversed = restored.render();
    assert(reversed[1].startTick == 3 * 480 + 120);
    restored.laneAt(1)->muted = true;
    assert(restored.render().size() == 1);
    restored.laneAt(1)->muted = false;
    restored.stepTicks = -1;
    assert(restored.cycleTicks() == 0 && restored.render().empty());
    restored.stepTicks = 240;
    restored.active = false;
    assert(restored.render().empty() && restored.hasNotes());

    std::cout << "Liberty polyphonic Step Grid: 8 lanes / simultaneous notes / independent lengths OK\n";
}
