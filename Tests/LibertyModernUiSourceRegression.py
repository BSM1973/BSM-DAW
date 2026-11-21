#!/usr/bin/env python3
"""Liberty Obsidian 2.0: source and geometry regression for the three main pages."""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
read=lambda p:(root/p).read_text(encoding="utf-8")
theme=read("Source/LibertyDesignSystem.h")
main=read("Source/Main.cpp")
arrange=read("Source/MainComponent.cpp")
performance=read("Source/PerformController.cpp")
mixer=read("Source/MixConsoleController.cpp")
pages=read("Source/PageModeCoordinator.cpp")
toolbar=read("Source/DynamicTrackController.cpp")
geometry=read("Source/NoOverlapLayout.h")

for token in ("canvasDeep","surfaceRaised","accentBlue","accentPurple",
              "accentLight","textPrimary","fillPanel","accentRule"):
    assert token in theme, "Missing visual design token: "+token
assert "juce::ColourGradient" in theme
assert "drawButtonBackground(juce::Graphics& g,juce::Button& button" in main
assert "drawButtonText(juce::Graphics& g,juce::TextButton& b" in main
assert "void drawLinearSlider(juce::Graphics& g,int x,int y,int width,int height," in main
assert "editor->setLookAndFeel(&menuLookAndFeel);" in main
assert "if(editor)editor->setLookAndFeel(nullptr);" in main
assert "performButton.setLookAndFeel(&arrangementLook);" in pages
assert "performButton.setToggleState(perform,juce::dontSendNotification);" in pages
assert "LibertyUI::Colour::accentPurple" in pages
assert "LibertyUI::fillPanel(g,header.toFloat(),false,7.f);" in performance
assert "LibertyUI::fillPanel(g,live.toFloat(),false,8.f);" in performance
assert "LibertyUI::fillPanel(g,area.toFloat(),true,10.f);" in performance
assert "const int columnW = visibleTracks>0?" in performance
assert "juce::jmin(440" in performance
assert "jlimit(110, 220" not in performance
assert "LibertyUI::Colour::canvasDeep" in performance
assert "LibertyUI::Colour::canvas" in mixer
assert "LibertyUI::Colour::accent" in mixer
assert "LibertyUI::fillPanel(g,c.toFloat(),master,7.f);" in arrange
assert "LibertyUI::Colour::canvasDeep" in arrange
assert "LibertyUI::fillPanel(g,juce::Rectangle<float>(" in toolbar
assert "LibertyNoOverlap::audioButton" in toolbar
assert "LibertyNoOverlap::instrumentButton" in toolbar
assert "creationControlsHaveSafeGaps()" in geometry
assert 'g.drawText("FOLLOW ACTIONS' not in performance
assert 'juce::String::fromUTF8("NON ASSIGNÉ")' in performance

# For desktop-supported sizes the four-track PERFORMANCE grid must fill
# almost the entire canvas, never cover the scene launch column, and each
# track must retain a positive, non-overlapping hit rectangle.
for width in (1280,1440,1659,1920,2560):
    left=18
    scene_launch=92
    grid_right=width-scene_launch-28
    total=grid_right-left
    for tracks in range(1,33):
        gap=8
        usable=max(1,total-max(0,tracks-1)*gap)
        col=min(440,max(1,usable//tracks))
        last=left+tracks*col+(tracks-1)*gap
        assert col>0 and last<=grid_right, (width,tracks,col,last,grid_right)
        if tracks==4:
            assert tracks*col>=total*0.72, (width,col,total)

# Signature rendering protects original control geometry.
browser=read("Source/BrowserController.cpp")
assert "LibertyUI::fillPanel(g,brandPanel,false,10.f);" in arrange
assert "LibertyUI::fillPanel(g,commandDeck,false,11.f);" in arrange
assert "mark.closeSubPath();" in arrange
assert 'g.drawFittedText(playing?"STOP":"PLAY",textArea,' in arrange
assert "215 + i * 62, 38, 56, 28" in arrange
assert "LibertyUI::fillPanel(g,s.bounds.toFloat().reduced(0.5f),chosen,6.f);" in mixer
assert "juce::Colour(0xffe7f0f7),thumb.getX(),thumb.getY()" in mixer
assert 'label="BAL 0.0 dB";' in mixer
assert "setDoubleClickReturnValue(true,0.0)" in mixer
assert 'g.drawFittedText("NAVIGATEUR",14,10,juce::jmax(0,getWidth()-96),24,' in browser
assert "closeButton.setBounds(getWidth() - 72, 8, 62, 26);" in browser
assert "if(!button.isVisible()||cell.getWidth()<80||cell.getHeight()<24)" in performance
assert "if(!queued&&!active)continue;" in performance
assert "g.fillRoundedRectangle(light,1.5f);" in performance
assert "queued?LibertyUI::Colour::accentAmber" in performance
print("PASS: Obsidian Signature rendering, typography and preserved control geometry")
