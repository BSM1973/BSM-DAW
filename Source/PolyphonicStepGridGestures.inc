// Phase 3: central pointer/keyboard routing for detached and dock editors.
// All state mutations happen on JUCE's message thread, never in processBlock().
#include "PolyphonicStepGridDirectEdit.h"
namespace
{
LibertyPolyGridEditing::Property polyProperty(int tab) noexcept
{
    return (LibertyPolyGridEditing::Property)juce::jlimit(0, 5, tab);
}
int polyPropertyStep(int tab, std::int64_t ticks)
{
    switch (tab)
    {
        case 0: return 5; // Velocity
        case 1: return (int)juce::jmax<std::int64_t>(1, ticks / 16); // Offset
        case 2: return (int)juce::jmax<std::int64_t>(1, ticks / 4); // Length
        case 3: return 1; // Repeat
        case 4: return 1; // Pitch, semitones
        default: return 5; // Chance
    }
}
} // namespace

juce::Rectangle<int> MainComponent::polyGridDockArea() const noexcept
{
    const int y = getMixerTop() + 36;
    return {310, y, juce::jmax(0, getWidth() - 310),
                      juce::jmax(0, getHeight() - y)};
}

LibertyStepSequencer::Pattern* MainComponent::polyGridPattern(bool& midi, int& index,
                                                              int& bankIndex) noexcept
{
    const int midiFirst = getAudioTrackCount();
    const int instrumentFirst = midiFirst + getMidiTrackCount();
    if (selectedTrack >= midiFirst && selectedTrack < instrumentFirst)
    {
        midi = true;
        index = selectedTrack - midiFirst;
        if ((size_t)index >= midiStepSequencers.size()) return nullptr;
        bankIndex = midiStepSequencers[(size_t)index].activePattern;
        return getMidiStepSequencer(index);
    }
    if (selectedTrack >= instrumentFirst &&
        selectedTrack < instrumentFirst + getInstrumentTrackCount())
    {
        midi = false;
        index = selectedTrack - instrumentFirst;
        if ((size_t)index >= instrumentStepSequencers.size()) return nullptr;
        bankIndex = instrumentStepSequencers[(size_t)index].activePattern;
        return getInstrumentStepSequencer(index);
    }
    return nullptr;
}

LibertyPolyGridEditing& MainComponent::polyGridEditor(bool midi, int index, int bank)
{
    const int logical = midi ? getAudioTrackCount() + index
                             : getAudioTrackCount() + getMidiTrackCount() + index;
    return polyGridEditors[std::make_pair(logical, bank)];
}

void MainComponent::publishPolyGrid(bool midi, int index, LibertyStepSequencer::Pattern& pattern)
{
    pattern.enabled = pattern.polyGrid.active && pattern.polyGrid.hasNotes();
    if (!midi) publishInstrumentStepSequencer(index);
    repaint();
}

bool MainComponent::beginPolyGridPointer(juce::Point<int> p, juce::Rectangle<int> area,
                                         const juce::ModifierKeys mods, int clicks)
{
    if (!area.contains(p)) return false;
    bool midi = false;
    int track = -1, bank = -1;
    auto* pattern = polyGridPattern(midi, track, bank);
    if (!pattern || !polyShowNewGrid(*pattern)) return false;
    const auto l = LibertyPolyGridUI::Layout::make(area.getWidth(), area.getHeight(), stepSequencerPage);
    if (!l.valid) return true;
    const auto local = p - area.getPosition();
    auto hit = [&](const LibertyPolyGridUI::Rect& r) { return r.contains(local.x, local.y); };
    auto& grid = pattern->polyGrid;
    auto& editor = polyGridEditor(midi, track, bank);
    const auto publish = [&] { publishPolyGrid(midi, track, *pattern); };
    polyGesture = {};
    polyGesture.area = area;
    polyGesture.midi = midi;
    polyGesture.track = track;
    polyGesture.bank = bank;
    polyGesture.down = p;
    polyGesture.current = p;
    polyGesture.original = grid;
    polyGesture.additive = mods.isShiftDown() || mods.isCommandDown() || mods.isCtrlDown();
    grabKeyboardFocus();

    if (hit(l.undoButton) || hit(l.redoButton))
    {
        const bool changed = hit(l.undoButton) ? editor.undo(grid) : editor.redo(grid);
        if (changed) publish();
        else repaint();
        return true;
    }
    if (hit(l.createClip) || hit(l.pianoExport))
    {
        exportPolyGridToPianoRoll(midi, track);
        repaint();
        return true;
    }
    if (hit(l.pianoImport))
    {
        importPianoRollToPolyGrid(midi, track);
        repaint();
        return true;
    }
    if (hit(l.classicMode))
    {
        stepSequencerPage = (stepSequencerPage + 1) % 4;
        repaint();
        return true;
    }
    for (int i = 0; i < 8; ++i)
        if (hit(l.banks[(size_t)i]))
        {
            if (midi) midiStepSequencers[(size_t)track].activePattern = i;
            else instrumentStepSequencers[(size_t)track].activePattern = i;
            stepSequencerPage = 0;
            polyGridSelectedLane = -1;
            polyGridSelectedEvent = 0;
            if (!midi) publishInstrumentStepSequencer(track);
            repaint();
            return true;
        }
    for (int tab = 0; tab < 6; ++tab)
        if (hit(l.editorTabs[(size_t)tab]))
        {
            polyGridEditorTab = tab;
            repaint();
            return true;
        }

    for (int row = 0; row < LibertyPolyphonicGrid::laneCount; ++row)
    {
        auto& lane = grid.lanes[(size_t)row];
        bool rowAction = false;
        if (hit(l.muteButtons[(size_t)row]))
        {
            editor.begin(grid); lane.muted = !lane.muted; rowAction = true;
        }
        else if (hit(l.soloButtons[(size_t)row]))
        {
            editor.begin(grid); lane.solo = !lane.solo; rowAction = true;
        }
        else if (hit(l.rowControls[(size_t)row][0]))
        {
            editor.begin(grid);
            lane.steps = (std::uint8_t)(lane.steps >= grid.stepCount ? 8 :
                std::min(grid.stepCount, (int)lane.steps + 4));
            rowAction = true;
        }
        else if (hit(l.rowControls[(size_t)row][1]))
        {
            editor.begin(grid);
            lane.rate = (std::uint8_t)((lane.rate + 1) % 3);
            rowAction = true;
        }
        else if (hit(l.rowControls[(size_t)row][2]))
        {
            editor.begin(grid);
            lane.reverse = !lane.reverse;
            rowAction = true;
        }
        else if (hit(l.rowControls[(size_t)row][3]))
        {
            editor.begin(grid);
            lane.swing = (std::uint8_t)(lane.swing >= 100 ? 0 : lane.swing + 25);
            rowAction = true;
        }
        if (rowAction)
        {
            editor.commit(grid);
            publish();
            return true;
        }
    }

    if ((hit(l.editMinus) || hit(l.editPlus)) && !editor.selection().empty())
    {
        const int sign = hit(l.editPlus) ? 1 : -1;
        polyGesture.mode = PolyGridGesture::Mode::Parameter;
        polyGesture.active = true;
        polyGesture.parameterSign = sign;
        polyGesture.parameterTab = polyGridEditorTab;
        polyGesture.originalSelection = editor.selection();
        editor.begin(grid);
        editor.applyDelta(grid, polyProperty(polyGridEditorTab),
                          sign * polyPropertyStep(polyGridEditorTab, grid.stepTicks));
        publish();
        return true;
    }

    for (int row = 0; row < LibertyPolyphonicGrid::laneCount; ++row)
    {
        for (int col = 0; col < 16; ++col)
        {
            if (!hit(l.cells[(size_t)row][(size_t)col])) continue;
            const int step = l.firstStep + col;
            if (step >= grid.stepCount) return true;
            const auto& lane = grid.lanes[(size_t)row];
            const auto pixelY = l.cells[(size_t)row][0].y;
            const LibertyPolyphonicGrid::Event* existing = nullptr;
            int rightEdge = -1;
            // Long notes are hit-testable throughout their entire rectangle.
            for (auto it = lane.events.rbegin(); it != lane.events.rend(); ++it)
            {
                const auto from = it->startStep;
                const auto end = (std::int64_t)from +
                    (it->lengthTicks + grid.stepTicks - 1) / grid.stepTicks;
                if (step < from || (std::int64_t)step >= end) continue;
                const auto endClipped = juce::jmin<std::int64_t>(l.firstStep + 16, end);
                const int x = l.gridLeft +
                     (int)(endClipped - l.firstStep) * l.gridWidth / 16 - 3;
                if (local.x <= x && local.y >= pixelY &&
                    local.y < pixelY + l.rowHeight - 1)
                {
                    existing = &*it;
                    rightEdge = x;
                    break;
                }
            }
            polyGesture.startRow = row;
            polyGesture.startStep = step;
            if (existing)
            {
                const LibertyPolyGridEditing::Key key {row, existing->id};
                if (clicks >= 2 || mods.isPopupMenu() || mods.isRightButtonDown())
                {
                    editor.begin(grid);
                    if (!editor.contains(key)) editor.select(key);
                    editor.removeSelected(grid);
                    editor.commit(grid);
                    polyGridSelectedLane = -1;
                    polyGridSelectedEvent = 0;
                    publish();
                    return true;
                }
                if (polyGesture.additive)
                {
                    editor.toggle(key);
                    repaint();
                    return true;
                }
                if (!editor.contains(key)) editor.select(key);
                polyGridSelectedLane = row;
                polyGridSelectedEvent = key.id;
                polyGesture.originalSelection = editor.selection();
                polyGesture.parameterTab = polyGridEditorTab;
                polyGesture.fineParameter = mods.isAltDown();
                // On the body of a note, decide the gesture from its initial
                // direction: vertical adjusts the chosen bottom parameter;
                // horizontal keeps note moving. The 7px right edge resizes.
                polyGesture.mode = rightEdge - local.x <= 7
                    ? PolyGridGesture::Mode::Resize : PolyGridGesture::Mode::PendingNote;
                polyGesture.active = true;
                editor.begin(grid);
                repaint();
                return true;
            }
            if (polyGesture.additive)
            {
                polyGesture.mode = PolyGridGesture::Mode::Marquee;
                polyGesture.active = true;
                polyGesture.originalSelection = editor.selection();
                repaint();
                return true;
            }
            editor.begin(grid);
            grid.active = true;
            const auto id = grid.addNote(row, step, grid.stepTicks);
            if (!id)
            {
                editor.cancel(grid);
                repaint();
                return true;
            }
            editor.select({row, id});
            polyGridSelectedLane = row;
            polyGridSelectedEvent = id;
            polyGesture.mode = PolyGridGesture::Mode::Paint;
            polyGesture.active = true;
            polyGesture.noteId = id;
            publish();
            return true;
        }
    }

    return true; // Empty section of this editor; do not route to mixer.
}

bool MainComponent::dragPolyGridPointer(juce::Point<int> p)
{
    if (!polyGesture.active) return false;
    polyGesture.current = p;
    const auto gesture = polyGesture;
    auto* pattern = gesture.midi ? getMidiStepSequencer(gesture.track)
                                  : getInstrumentStepSequencer(gesture.track);
    // Reject a stale pointer gesture if its pattern bank was switched.
    const int bank = gesture.midi ? midiStepSequencers[(size_t)gesture.track].activePattern
                                  : instrumentStepSequencers[(size_t)gesture.track].activePattern;
    if (!pattern || bank != gesture.bank) { polyGesture.active = false; return true; }
    auto& editor = polyGridEditor(gesture.midi, gesture.track, gesture.bank);
    auto& grid = pattern->polyGrid;
    const auto l = LibertyPolyGridUI::Layout::make(gesture.area.getWidth(), gesture.area.getHeight(), stepSequencerPage);
    if (!l.valid) return true;
    const auto local = p - gesture.area.getPosition();
    const int stepWidth = juce::jmax(1, l.gridWidth / 16);
    const int deltaStep = (int)std::lround((double)(p.x - gesture.down.x) / stepWidth);
    const int deltaRow = (int)std::lround((double)(p.y - gesture.down.y) / juce::jmax(1, l.rowHeight));
    if (gesture.mode == PolyGridGesture::Mode::PendingNote)
    {
        const auto intent = LibertyPolyGridDirect::chooseIntent(
            p.x - gesture.down.x, p.y - gesture.down.y);
        if (intent == LibertyPolyGridDirect::DragIntent::Pending) return true;
        polyGesture.mode = intent == LibertyPolyGridDirect::DragIntent::Parameter
            ? PolyGridGesture::Mode::NoteParameter : PolyGridGesture::Mode::Move;
    }
    switch (polyGesture.mode)
    {
        case PolyGridGesture::Mode::Move:
        {
            editor.clearSelection();
            for (const auto& k : gesture.originalSelection) editor.select(k, true);
            if (!editor.moveFrom(gesture.original, grid, deltaRow, deltaStep)) return true;
            editor.clearSelection();
            for (auto k : gesture.originalSelection)
            {
                k.row += deltaRow;
                editor.select(k, true);
            }
            if (!editor.selection().empty())
            {
                polyGridSelectedLane = editor.selection().front().row;
                polyGridSelectedEvent = editor.selection().front().id;
            }
            publishPolyGrid(gesture.midi, gesture.track, *pattern);
            return true;
        }
        case PolyGridGesture::Mode::NoteParameter:
        {
            // Relative to mouseDown on every frame: reversing the drag
            // restores the original value, without accumulating rounding.
            grid = gesture.original;
            editor.clearSelection();
            for (const auto& key : gesture.originalSelection)
                editor.select(key, true);
            const int pixelsUp = gesture.down.y - p.y;
            const int delta = LibertyPolyGridDirect::deltaForVerticalDrag(
                polyProperty(gesture.parameterTab), pixelsUp,
                gesture.original.stepTicks, gesture.fineParameter);
            editor.applyDelta(grid, polyProperty(gesture.parameterTab), delta);
            publishPolyGrid(gesture.midi, gesture.track, *pattern);
            return true;
        }
        case PolyGridGesture::Mode::Resize:
            editor.clearSelection();
            for (const auto& k : gesture.originalSelection) editor.select(k, true);
            if (editor.resizeFrom(gesture.original, grid, deltaStep))
                publishPolyGrid(gesture.midi, gesture.track, *pattern);
            return true;
        case PolyGridGesture::Mode::Paint:
        {
            const auto length = juce::jmax<std::int64_t>(1, (std::int64_t)(1 + deltaStep) * grid.stepTicks);
            if (grid.changeLength(gesture.startRow, gesture.noteId, length))
                publishPolyGrid(gesture.midi, gesture.track, *pattern);
            return true;
        }
        case PolyGridGesture::Mode::Marquee:
        {
            editor.clearSelection();
            for (const auto& k : gesture.originalSelection) editor.select(k, true);
            const auto clampedRow = juce::jlimit(0, LibertyPolyphonicGrid::laneCount - 1,
                gesture.startRow + deltaRow);
            const auto clampedStep = juce::jlimit(0, grid.stepCount - 1,
                gesture.startStep + deltaStep);
            editor.selectRectangle(grid, gesture.startRow, gesture.startStep,
                                   clampedRow, clampedStep, true);
            repaint();
            return true;
        }
        case PolyGridGesture::Mode::Parameter:
        {
            grid = gesture.original;
            editor.clearSelection();
            for (const auto& k : gesture.originalSelection) editor.select(k, true);
            const int units = (int)std::lround((double)(p.x - gesture.down.x) / 12.0);
            const int delta = gesture.parameterSign *
                              polyPropertyStep(gesture.parameterTab, grid.stepTicks) * (1 + units);
            editor.applyDelta(grid, polyProperty(gesture.parameterTab), delta);
            publishPolyGrid(gesture.midi, gesture.track, *pattern);
            return true;
        }
        default: break;
    }
    return true;
}

bool MainComponent::endPolyGridPointer()
{
    if (!polyGesture.active) return false;
    const auto gesture = polyGesture;
    polyGesture = {};
    auto* pattern = gesture.midi ? getMidiStepSequencer(gesture.track)
                                  : getInstrumentStepSequencer(gesture.track);
    if (!pattern) return true;
    auto& editor = polyGridEditor(gesture.midi, gesture.track, gesture.bank);
    editor.commit(pattern->polyGrid);
    publishPolyGrid(gesture.midi, gesture.track, *pattern);
    return true;
}

bool MainComponent::polyGridKeyAction(const juce::KeyPress& key, bool detached)
{
    const int sourceTrack = selectedTrack;
    if (detached && floatingStepSequencerTrack >= getAudioTrackCount())
        selectedTrack = floatingStepSequencerTrack;
    bool midi = false;
    int track = -1, bank = -1;
    auto* pattern = polyGridPattern(midi, track, bank);
    if (!pattern || !polyShowNewGrid(*pattern))
    {
        selectedTrack = sourceTrack;
        return false;
    }
    const auto mods = key.getModifiers();
    const bool command = mods.isCommandDown() || mods.isCtrlDown();
    const int ch = key.getKeyCode();
    auto& editor = polyGridEditor(midi, track, bank);
    auto& grid = pattern->polyGrid;
    bool handled = false, changed = false;
    if (command && (ch == 'z' || ch == 'Z' || ch == 'y' || ch == 'Y'))
    {
        handled = true;
        changed = (ch == 'y' || ch == 'Y' || mods.isShiftDown()) ?
            editor.redo(grid) : editor.undo(grid);
    }
    else if (command && (ch == 'a' || ch == 'A'))
    {
        handled = true;
        editor.clearSelection();
        for (int row = 0; row < LibertyPolyphonicGrid::laneCount; ++row)
            for (const auto& e : grid.lanes[(size_t)row].events)
                editor.select({row, e.id}, true);
    }
    else if (ch == juce::KeyPress::deleteKey || ch == juce::KeyPress::backspaceKey)
    {
        handled = true;
        if (!editor.selection().empty())
        {
            editor.begin(grid);
            changed = editor.removeSelected(grid);
            editor.commit(grid);
        }
    }
    if (handled)
    {
        if (changed) publishPolyGrid(midi, track, *pattern);
        else repaint();
    }
    selectedTrack = sourceTrack;
    return handled;
}

bool MainComponent::beginFloatingPolyGrid(const juce::MouseEvent& e,
                                          juce::Rectangle<int> area)
{
    const int sourceTrack = selectedTrack;
    const int firstInstrument = getAudioTrackCount() + getMidiTrackCount();
    if (floatingStepSequencerTrack >= getAudioTrackCount() &&
        floatingStepSequencerTrack < firstInstrument + getInstrumentTrackCount())
        selectedTrack = floatingStepSequencerTrack;
    const bool handled = beginPolyGridPointer(e.getPosition(), area, e.mods, e.getNumberOfClicks());
    selectedTrack = sourceTrack;
    return handled;
}
