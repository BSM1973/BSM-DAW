#define private public
#include "MainComponent.h"
#undef private
#include "PluginHost.h"
#if JUCE_MAC
#include "NativePluginCapture.h"
#endif
#include "OneKnobEffects.h"
#include "LibertyDesignSystem.h"
#include "PageNavigationLayout.h"
void refreshLibertyMixConsole(MainComponent*);
void resizeLibertyMixConsole(MainComponent*);

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <map>
#include <memory>
#include <functional>
#include <initializer_list>
#include <thread>
#include <vector>

int getLibertyPreferredBrowserWidth(MainComponent* owner);
void resizeLibertyBrowserResizeController(MainComponent* owner);

namespace
{
constexpr int topBarHeight = 76;

// File filtering is performed by the directory listing itself, so category
// changes and search results agree with the visible file tree.
class BrowserFileFilter final : public juce::FileFilter
{
public:
    BrowserFileFilter() : juce::FileFilter("Liberty browser files") {}
    int category = 0;
    juce::String query;
    bool isDirectorySuitable(const juce::File&) const override { return true; }
    bool isFileSuitable(const juce::File& file) const override
    {
        if (query.isNotEmpty() && !file.getFileName().containsIgnoreCase(query)) return false;
        const auto ext = file.getFileExtension().toLowerCase();
        if (category == 4) return ext == ".bsmproj";
        if (category == 1) return ext == ".wav" || ext == ".aif" || ext == ".aiff" || ext == ".mp3" || ext == ".flac";
        if (category == 2) return ext == ".mid" || ext == ".midi";
        if (category == 3) return ext == ".xml" || ext == ".fxp" || ext == ".vstpreset" || ext == ".aupreset";
        return true;
    }
};


// Reject unreadable, oversized and visually uniform screenshots before replacing artwork.
static bool libertyValidArtworkPNG(const juce::File& source, juce::String& reason)
{
    if (!source.existsAsFile() || !source.hasFileExtension("png")
        || source.getSize() < 128 || source.getSize() > 32LL * 1024 * 1024)
    {
        reason = "Fichier PNG absent, invalide ou trop volumineux.";
        return false;
    }
    juce::PNGImageFormat png;
    std::unique_ptr<juce::FileInputStream> stream(source.createInputStream());
    if (!stream || !stream->openedOk() || !png.canUnderstand(*stream))
    {
        reason = "Le fichier n'est pas un véritable PNG.";
        return false;
    }
    stream->setPosition(0);
    const auto image = png.decodeImage(*stream);
    if (!image.isValid() || image.getWidth() < 80 || image.getHeight() < 60
        || (juce::int64) image.getWidth() * image.getHeight() > 24000000)
    {
        reason = "Dimensions de la miniature invalides.";
        return false;
    }
    const auto first = image.getPixelAt(0, 0);
    bool varied = false;
    for (int y = 0; y < 9 && !varied; ++y)
        for (int x = 0; x < 9 && !varied; ++x)
        {
            const auto pixel = image.getPixelAt((image.getWidth() - 1) * x / 8,
                                                 (image.getHeight() - 1) * y / 8);
            varied = std::abs((int) pixel.getRed() - (int) first.getRed()) > 8
                  || std::abs((int) pixel.getGreen() - (int) first.getGreen()) > 8
                  || std::abs((int) pixel.getBlue() - (int) first.getBlue()) > 8;
        }
    if (!varied)
    {
        reason = "Image blanche ou uniforme refusée : ancienne miniature conservée.";
        return false;
    }
    return true;
}

static bool libertySavePluginArtwork(const juce::File& source,
                                    const juce::String& identifier,
                                    juce::String& reason)
{
    if (!libertyValidArtworkPNG(source, reason)) return false;
    const auto folder = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Liberty").getChildFile("PluginThumbnails");
    if (!folder.createDirectory())
    {
        reason = "Impossible de créer le dossier des miniatures.";
        return false;
    }
    const auto destination = folder.getChildFile(juce::String::toHexString(
        (juce::int64) identifier.hashCode64()) + ".png");
    const auto staged = folder.getNonexistentChildFile("thumbnail-import", ".png", false);
    if (!source.copyFileTo(staged) || !staged.replaceFileIn(destination))
    {
        staged.deleteFile();
        reason = "Impossible d'enregistrer la miniature.";
        return false;
    }
    return true;
}

class PluginTreeItem final : public juce::TreeViewItem
{
public:
    enum class Kind { root, manufacturer, plugin };

    PluginTreeItem(Kind kindIn,
                   juce::String labelIn,
                   const juce::PluginDescription* descriptionIn = nullptr,
                   std::function<void(const juce::PluginDescription&)> activateIn = {},
                   std::function<bool(const juce::PluginDescription&)> isFavouriteIn = {},
                   std::function<void(const juce::PluginDescription&)> toggleFavouriteIn = {},
                   bool thumbnailsIn = false,
                   std::function<void(const juce::File&, const juce::PluginDescription&)> importArtworkIn = {},
                   std::function<void(const juce::PluginDescription&, juce::Point<int>)> editFamilyIn = {},
                   bool manuallyClassifiedIn = false)
        : kind(kindIn), thumbnails(thumbnailsIn), manuallyClassified(manuallyClassifiedIn),
          label(std::move(labelIn)),
          activate(std::move(activateIn)), isFavourite(std::move(isFavouriteIn)),
          toggleFavourite(std::move(toggleFavouriteIn)), importArtwork(std::move(importArtworkIn)),
          editFamily(std::move(editFamilyIn))
    {
        if (descriptionIn != nullptr) description = *descriptionIn;
        // Artwork is decoded lazily on first visibility, not when JUCE builds
        // hundreds of collapsed folders or filters thousands of plugins.
        // The thumbnail is still owned by each TreeViewItem and released with it.
    }

    bool mightContainSubItems() override { return kind != Kind::plugin; }
    bool canBeSelected() const override { return kind == Kind::plugin; }
    int getItemHeight() const override { return kind == Kind::manufacturer ? 28 : (kind == Kind::plugin ? (thumbnails ? 138 : 38) : 20); }
    juce::String getUniqueName() const override { return kind == Kind::plugin ? description.createIdentifierString() : label; }

    void paintItem(juce::Graphics& g, int width, int height) override
    {
        if (kind == Kind::root) return;
        if (kind == Kind::manufacturer)
        {
            const bool fav = label == "FAVORIS";
            // Obsidian graphite manufacturer headers; preserve the gold-toned favourites surface.
            g.setColour(fav ? juce::Colour(0xff2a2415) : juce::Colour(0xff1b2833));
            g.fillRect(0, 0, width, height);
            // A one-pixel graphite rule separates manufacturer groups without changing layout.
            g.setColour(fav ? juce::Colour(0xff655331) : juce::Colour(0xff40515f));
            g.fillRect(0, height - 1, width, 1);
            // Obsidian manufacturer headings use muted turquoise; favourites keep gold.
            g.setColour(fav ? juce::Colour(0xffffc857) : juce::Colour(0xffb8e8ec));
            g.setFont(juce::Font(11.0f, juce::Font::bold));
            // Keep a dedicated disclosure region and clip long group names.
            const int headingWidth = juce::jmax(0, width - 20);
            if (headingWidth > 0)
                g.drawFittedText(label, 4, 0, headingWidth, height,
                                 juce::Justification::centredLeft, 1);
            return;
        }

        if (isSelected())
        {
            // Obsidian selection uses the same deep turquoise as selected arranger rows.
            g.setColour(juce::Colour(0xff23434c));
            g.fillRect(0, 0, width, height);
            // Obsidian selection marker stays inside the existing plugin row.
            g.setColour(juce::Colour(0xff54d6dc));
            g.fillRect(0, 0, 2, height);
        }

        int textLeft = 4;
        if (thumbnails && width >= 110)
        {
            if (!thumbnailReadAttempted)
            {
                thumbnailReadAttempted = true;
                const auto folder = juce::File::getSpecialLocation(
                    juce::File::userApplicationDataDirectory)
                    .getChildFile("Liberty").getChildFile("PluginThumbnails");
                const auto fileName = juce::String::toHexString(
                    (juce::int64) description.createIdentifierString().hashCode64()) + ".png";
                const auto localFile = folder.getChildFile(fileName);
                if (localFile.existsAsFile())
                    thumbnailImage = juce::ImageFileFormat::loadFrom(localFile);
                if (!thumbnailImage.isValid())
                {
                    const auto exactCatalogue = folder.getSiblingFile("PluginArtworkCatalogue")
                        .getChildFile(fileName);
                    if (exactCatalogue.existsAsFile())
                        thumbnailImage = juce::ImageFileFormat::loadFrom(exactCatalogue);
                }
            }
            const int imageWidth = juce::jlimit(92, 180, (width * 48) / 100);
            const juce::Rectangle<int> imageArea(4, 5, imageWidth, height - 10);
            g.setColour(juce::Colour(0xff1b2833));
            g.fillRoundedRectangle(imageArea.toFloat(), 4.0f);
            if (thumbnailImage.isValid()) g.drawImageWithin(thumbnailImage, imageArea.getX(), imageArea.getY(), imageArea.getWidth(), imageArea.getHeight(), juce::RectanglePlacement::centred);
            else
            {
                // Always provide a deterministic, locally rendered cover without network requests.
                const auto manufacturer = description.manufacturerName.trim();
                const auto title = description.name.trim();
                const auto seed = (juce::uint32) (manufacturer + "|" + title).hashCode();
                const auto accent = juce::Colour::fromHSV((float)(seed % 360) / 360.0f, 0.50f, 0.83f, 1.0f);
                g.setGradientFill(juce::ColourGradient(juce::Colour(0xff132638),
                    (float)imageArea.getX(), (float)imageArea.getY(),
                    juce::Colour(0xff243c51), (float)imageArea.getRight(), (float)imageArea.getBottom(), false));
                g.fillRoundedRectangle(imageArea.toFloat(), 5.0f);
                g.setColour(accent);
                g.fillRoundedRectangle((float)imageArea.getX() + 5.0f, (float)imageArea.getY() + 6.0f,
                    3.0f, (float)imageArea.getHeight() - 12.0f, 1.5f);
                g.setColour(juce::Colour(0xffeffcfd));
                g.setFont(juce::Font(13.0f, juce::Font::bold));
                g.drawFittedText(title, imageArea.reduced(12, 9).removeFromTop(36),
                    juce::Justification::centred, 2);
                g.setColour(juce::Colour(0xffb9ccd6));
                g.setFont(juce::Font(8.0f));
                g.drawFittedText(manufacturer.isNotEmpty() ? manufacturer : "LIBERTY PLUGINS",
                    imageArea.withTrimmedTop(53).reduced(7, 2), juce::Justification::centred, 1);
            }
            textLeft = imageArea.getRight() + 8;
        }
        // User-assigned families receive a small turquoise indicator.
        // Reserve the space in the name line so there is no text overlap.
        const bool showBadge = manuallyClassified && width > textLeft + 114;
        const int markerWidth = showBadge ? 44 : (manuallyClassified ? 8 : 0);
        if (manuallyClassified && width > 16)
        {
            if (showBadge)
            {
                const auto tag = juce::Rectangle<float>((float)width - 49.0f, 4.0f, 43.0f, 14.0f);
                g.setColour(juce::Colour(0xff23515b));
                g.fillRoundedRectangle(tag, 3.0f);
                g.setColour(juce::Colour(0xff54d6dc));
                g.setFont(juce::Font(8.0f, juce::Font::bold));
                g.drawFittedText("PERSO", tag.toNearestInt(),
                                 juce::Justification::centred, 1);
            }
            else
            {
                g.setColour(juce::Colour(0xff54d6dc));
                g.fillEllipse((float)width - 12.0f, 7.0f, 6.0f, 6.0f);
            }
        }
        g.setColour(juce::Colour(0xffeffcfd));
        g.setFont(juce::Font(11.0f, juce::Font::bold));
        const int nameWidth = width - textLeft - 8 - (markerWidth == 44 ? 49 : (markerWidth ? 10 : 0));
        if (nameWidth > 8)
            g.drawFittedText(description.name, textLeft, 3, nameWidth, 17,
                             juce::Justification::centredLeft, 1);
        // Obsidian secondary metadata stays readable without competing with plugin names.
        g.setColour(juce::Colour(0xffb9ccd6));
        g.setFont(juce::Font(9.0f));
        if (width > textLeft + 8)
            g.drawFittedText(description.pluginFormatName + "  "
                             + (description.isInstrument ? "INSTRUMENT" : "EFFET"),
                             textLeft, 20, width - textLeft - 8, 14,
                             juce::Justification::centredLeft, 1);
    }

    void itemClicked(const juce::MouseEvent& event) override
    {
        if (kind != Kind::plugin || !event.mods.isRightButtonDown()) return;
        // Preserve favourite management in the context menu rather than
        // silently toggling it when the user intends to change a category.
        if (editFamily) editFamily(description, event.getScreenPosition());
        else if (toggleFavourite) toggleFavourite(description);
    }

    void itemDoubleClicked(const juce::MouseEvent&) override
    {
        if (kind == Kind::manufacturer) { setOpen(!isOpen()); return; }
        if (kind == Kind::plugin && activate) activate(description);
    }

    const juce::PluginDescription* getPluginDescription() const noexcept
    {
        return kind == Kind::plugin ? &description : nullptr;
    }

    // Let JUCE's TreeView delegate Finder drops to the actual plugin row.
    // This avoids guessing the row from scroll/viewport coordinates.
    bool isInterestedInFileDrag(const juce::StringArray& files) override
    {
        return kind == Kind::plugin && files.size() == 1
            && juce::File(files[0]).hasFileExtension("png");
    }

    void filesDropped(const juce::StringArray& files, int) override
    {
        if (isInterestedInFileDrag(files) && importArtwork)
            importArtwork(juce::File(files[0]), description);
    }

private:
    Kind kind;
    bool thumbnails = false;
    bool manuallyClassified = false;
    bool thumbnailReadAttempted = false;
    juce::Image thumbnailImage;
    juce::String label;
    juce::PluginDescription description;
    std::function<void(const juce::PluginDescription&)> activate;
    std::function<bool(const juce::PluginDescription&)> isFavourite;
    std::function<void(const juce::PluginDescription&)> toggleFavourite;
    std::function<void(const juce::File&, const juce::PluginDescription&)> importArtwork;
    std::function<void(const juce::PluginDescription&, juce::Point<int>)> editFamily;
};

// One Knob controls reserve separate rectangles for their icon and label.
// Never paint icons over JUCE's centred default TextButton text.
class OneKnobButton final : public juce::TextButton
{
public:
    void paintButton(juce::Graphics& g, bool isOver, bool isDown) override
    {
        const auto area = getLocalBounds().toFloat().reduced(0.8f);
        const auto background = getToggleState() ? juce::Colour(0xff286674)
                                                  : juce::Colour(0xff1b2833);
        g.setColour(background.brighter(isDown ? 0.12f : (isOver ? 0.06f : 0.0f)));
        g.fillRoundedRectangle(area, 5.0f);
        g.setColour(juce::Colour(0xff40515f));
        g.drawRoundedRectangle(area, 5.0f, 1.0f);

        const int width = getWidth();
        const int iconSpace = width >= 112 ? 25 : (width >= 82 ? 20 : 0);
        if (iconSpace > 0)
        {
            const float cx = (float)iconSpace * 0.48f;
            const float cy = (float)getHeight() * 0.5f;
            g.setColour(juce::Colour(0xff54d6dc));
            g.drawEllipse(cx - 5.0f, cy - 5.0f, 10.0f, 10.0f, 1.0f);
            juce::Path wave;
            wave.startNewSubPath(cx - 3.5f, cy);
            wave.lineTo(cx - 1.5f, cy - 2.0f);
            wave.lineTo(cx + 0.5f, cy + 2.0f);
            wave.lineTo(cx + 3.5f, cy);
            g.strokePath(wave, juce::PathStrokeType(1.0f));
        }

        // The label is clipped to its own region; the icon cannot overlap it.
        auto textArea = getLocalBounds().withTrimmedLeft(iconSpace + 2).reduced(3, 2);
        g.setColour(juce::Colour(0xffb8e8ec));
        g.setFont(juce::Font(juce::jlimit(9.0f, 12.0f, (float)width / 11.0f), juce::Font::bold));
        g.drawFittedText(getButtonText(), textArea, juce::Justification::centred, 1, 0.75f);
    }
};


// Compact midnight-blue action dashboard, matching Liberty's welcome cards.
// It is an anchored, dismissible CallOutBox (not a new full-size window).
// The content owns all its buttons; no action touches its lifetime after close.
class ArtworkMenuAction final : public juce::TextButton
{
public:
    ArtworkMenuAction(const juce::String& label, bool highlighted, bool selectedIn = false)
        : juce::TextButton(label), emphasis(highlighted), selected(selectedIn) {}

    void paintButton(juce::Graphics& g, bool hovered, bool pressed) override
    {
        auto surface = getLocalBounds().toFloat().reduced(0.7f);
        auto base = selected ? juce::Colour(0xff286674)
                            : emphasis ? juce::Colour(0xff294d5d)
                                       : juce::Colour(0xff203746);
        if (hovered && isEnabled()) base = base.brighter(0.12f);
        if (pressed && isEnabled()) base = base.brighter(0.08f);
        const float opacity = isEnabled() ? 1.0f : 0.35f;
        g.setColour(base.withMultipliedAlpha(opacity));
        g.fillRoundedRectangle(surface, 5.0f);
        g.setColour(juce::Colour(selected ? 0xff54d6dc : 0xff405a6b).withMultipliedAlpha(opacity));
        g.drawRoundedRectangle(surface, 5.0f, 1.0f);
        g.setColour(juce::Colour(0xffeffcfd).withMultipliedAlpha(opacity));
        g.setFont(juce::Font(10.6f, juce::Font::bold));
        g.drawFittedText(getButtonText(), getLocalBounds().reduced(7, 2),
                         juce::Justification::centred, 1, 0.74f);
    }

private:
    bool emphasis = false;
    bool selected = false;
};

class ArtworkMenuPanel final : public juce::Component
{
public:
    ArtworkMenuPanel(bool batchRunning, int pngCount, int totalCount, int view,
                     std::function<void(int)> onAction)
        : running(batchRunning), currentView(view), present(pngCount), total(totalCount),
          action(std::move(onAction))
    {
        setSize(552, 462);
        // Every card and button has its own fixed rectangle, including at
        // full French label lengths; no overlay and no auto-laid-out text.
        add(5,  juce::String::fromUTF8("CAPTURE ISOLÉE (MAC)"),       28, 116, 229, 30, !running);
        add(12, "TESTER 1 PLUGIN",            28, 152, 229, 30, !running);
        add(6,  "CAPTURER 10 PLUGINS",        28, 188, 229, 30, !running);
        add(7,  "CAPTURER 50 PLUGINS",        28, 224, 229, 30, !running);
        add(8,  "CAPTURER TOUS",              28, 260, 229, 30, !running, true);
        add(9,  juce::String::fromUTF8("ARRÊTER LA CAPTURE"),         28, 296, 229, 30, running);

        add(13, juce::String::fromUTF8("RÉESSAYER 1 ÉCHEC"),         297, 116, 227, 29, !running);
        add(10, juce::String::fromUTF8("RÉESSAYER 10 ÉCHECS"),       297, 150, 227, 29, !running);
        add(14, juce::String::fromUTF8("RÉESSAYER 100 ÉCHECS"),      297, 184, 227, 29, !running, true);

        add(1,  "IMPORTER UN PNG",            297, 267, 227, 28, true, true);
        add(2,  "IMAGES OFFICIELLES",         297, 302, 227, 28, !running);

        add(20, "TOUS",                       28, 385, 111, 26, true,  false, currentView == 0);
        add(21, "AVEC IMAGE",                 146, 385, 111, 26, true, false, currentView == 1);
        add(22, "SANS IMAGE",                  28, 417, 111, 26, true, false, currentView == 2);
        add(23, juce::String::fromUTF8("ÉCHECS"),                     146, 417, 111, 26, true, false, currentView == 3);

        add(11, "JOURNAL DES CAPTURES",      297, 384, 227, 27, true);
        add(3,  "DOSSIER DES IMAGES",        297, 416, 227, 27, true);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff101e2b));
        g.setColour(juce::Colour(0xff405a6b));
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.7f), 12.0f, 1.2f);

        g.setColour(juce::Colour(0xff54d6dc));
        g.fillRoundedRectangle(15.0f, 17.0f, 3.5f, 39.0f, 1.5f);
        g.setColour(juce::Colour(0xffeffcfd));
        g.setFont(juce::Font(18.0f, juce::Font::bold));
        g.drawText("LIBERTY  /  MINIATURES", 29, 12, 365, 27,
                   juce::Justification::centredLeft);
        g.setColour(juce::Colour(0xff82bdc9));
        g.setFont(juce::Font(10.0f, juce::Font::bold));
        g.drawText(juce::String::fromUTF8("CAPTURES AUTOMATIQUES ET BIBLIOTHÈQUE"), 29, 40, 376, 18,
                   juce::Justification::centredLeft);
        g.setColour(juce::Colour(0xffb8e8ec));
        g.setFont(juce::Font(10.0f, juce::Font::bold));
        g.drawFittedText("PNG  " + juce::String(present) + "/" + juce::String(total),
                         405, 22, 127, 30, juce::Justification::centredRight, 1);
        g.setColour(juce::Colour(0xff365366));
        g.fillRect(15, 67, 522, 1);

        drawCard(g, {14, 78, 257, 262}, "CAPTURES");
        drawCard(g, {283, 78, 255, 144}, juce::String::fromUTF8("RÉESSAYER LES ÉCHECS"));
        drawCard(g, {283, 232, 255, 108}, "IMPORTATION");
        drawCard(g, {14, 350, 257, 98}, "FILTRES");
        drawCard(g, {283, 350, 255, 98}, "OUTILS");
    }

private:
    static void drawCard(juce::Graphics& g, const juce::Rectangle<int>& area,
                         const juce::String& title)
    {
        auto r = area.toFloat();
        g.setColour(juce::Colour(0xff1a3040));
        g.fillRoundedRectangle(r, 8.0f);
        g.setColour(juce::Colour(0xff36566a));
        g.drawRoundedRectangle(r.reduced(0.5f), 8.0f, 1.0f);
        g.setColour(juce::Colour(0xff54d6dc));
        g.fillRoundedRectangle((float)area.getX() + 12.0f, (float)area.getY() + 11.0f,
                               23.0f, 3.0f, 1.5f);
        g.setColour(juce::Colour(0xffeffcfd));
        g.setFont(juce::Font(11.0f, juce::Font::bold));
        g.drawFittedText(title, area.getX() + 12, area.getY() + 17,
                         area.getWidth() - 24, 18,
                         juce::Justification::centredLeft, 1);
    }

    void add(int id, const juce::String& label, int x, int y, int w, int h,
             bool enabled, bool emphasis = false, bool selected = false)
    {
        auto button = std::make_unique<ArtworkMenuAction>(label, emphasis, selected);
        button->setBounds(x, y, w, h);
        button->setEnabled(enabled);
        button->setMouseCursor(juce::MouseCursor::PointingHandCursor);
        button->onClick = [this, id]
        {
            // Defer browser changes (and file chooser creation) until after
            // the callout dismisses; its children may then be safely destroyed.
            const auto callback = action;
            if (auto* box = findParentComponentOfClass<juce::CallOutBox>())
                box->dismiss();
            juce::MessageManager::callAsync([callback, id]
            {
                if (callback) callback(id);
            });
        };
        addAndMakeVisible(*button);
        buttons.push_back(std::move(button));
    }

    bool running = false;
    int currentView = 0, present = 0, total = 0;
    std::function<void(int)> action;
    std::vector<std::unique_ptr<ArtworkMenuAction>> buttons;
};

// Search box keeps focus while Up/Down navigates the filtered plugin tree.
// Ordinary text editing and all other keys still use JUCE's TextEditor.
class LibertyPluginSearchEditor final : public juce::TextEditor
{
public:
    std::function<void(int)> onNavigateResults;

    bool keyPressed(const juce::KeyPress& key) override
    {
        if (!key.getModifiers().isAnyModifierKeyDown())
        {
            const int delta = key.isKeyCode(juce::KeyPress::downKey) ? 1
                            : key.isKeyCode(juce::KeyPress::upKey) ? -1 : 0;
            if (delta != 0 && onNavigateResults)
            {
                onNavigateResults(delta);
                return true;
            }
        }
        return juce::TextEditor::keyPressed(key);
    }
};

class BrowserPanel final : public juce::Component,
                           private juce::Timer,
                           private juce::FileBrowserListener
{
public:
    void ensureToggleVisible()
    {
        toggleButton.setVisible(true);
        toggleButton.toFront(false);
    }
    bool isOpenForMixer() const noexcept {return browserOpen;}

    void openForEffectSlot(bool instrument,int lane,int slot)
    {
        if(slot<0||slot>=LibertyPluginHost::effectSlotsPerTrack ||
           lane<0||(instrument?lane>=owner.getInstrumentTrackCount()
                                :lane>=owner.getAudioTrackCount()))
            return;
        targetIsInstrument=instrument;
        targetEffectLane=lane;
        targetEffectSlot=slot;
        owner.selectedTrack=instrument
            ?owner.getAudioTrackCount()+owner.getMidiTrackCount()+lane:lane;
        setCategory(Category::plugins);
        toggleButton.setToggleState(true,juce::dontSendNotification);
        setBrowserOpen(true);
    }

    explicit BrowserPanel(MainComponent& ownerIn)
        : owner(ownerIn), directoryList(&filter, thread), fileTree(directoryList)
    {
        setAlwaysOnTop(true);
        thread.startThread();
        previewFormats.registerBasicFormats();
        previewPlayer.setSource(&previewTransport);
        owner.audioEngine.getDeviceManager().addAudioCallback(&previewPlayer);
        loadFavourites();
        loadRecentPlugins();
        loadPluginFamilyOverrides();
        loadPluginCollections();

        toggleButton.setButtonText("NAVIGATEUR");
        toggleButton.setAlwaysOnTop(true);
        // Obsidian browser toggle matches category controls in both states.
        toggleButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1b2833));
        toggleButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff286674));
        toggleButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffb9ccd6));
        toggleButton.setColour(juce::TextButton::textColourOnId, juce::Colour(0xffeffcfd));
        toggleButton.setClickingTogglesState(true);
        toggleButton.setToggleState(false, juce::dontSendNotification);
        toggleButton.onClick = [this] { setBrowserOpen(toggleButton.getToggleState()); };
        owner.addAndMakeVisible(toggleButton);

        closeButton.setButtonText("FERMER");
        // Obsidian close control uses the same graphite surface and soft-white labels.
        closeButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1b2833));
        closeButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffeffcfd));
        closeButton.onClick = [this]
        {
            toggleButton.setToggleState(false, juce::dontSendNotification);
            setBrowserOpen(false);
        };
        addAndMakeVisible(closeButton);

        // Obsidian: distinguish selected files without altering file interactions.
        fileTree.setColour(juce::DirectoryContentsDisplayComponent::highlightColourId, juce::Colour(0xff23434c));
        fileTree.setColour(juce::DirectoryContentsDisplayComponent::textColourId, juce::Colour(0xffeffcfd));
        fileTree.setRootItemVisible(false);
        // Obsidian file tree uses the same deep graphite as the plugin browser.
        fileTree.setColour(juce::TreeView::backgroundColourId, juce::Colour(0xff101d2b));
        // Match the plugin tree graphite guides and turquoise drop target.
        fileTree.setColour(juce::TreeView::linesColourId, juce::Colour(0xff40515f));
        fileTree.setColour(juce::TreeView::dragAndDropIndicatorColourId, juce::Colour(0xff54d6dc));
        fileTree.addListener(this);
        addAndMakeVisible(fileTree);

        pluginTree.setRootItemVisible(false);
        pluginTree.setMultiSelectEnabled(false);
        // Keep the plugin list surface continuous with the Obsidian browser panel.
        pluginTree.setColour(juce::TreeView::backgroundColourId, juce::Colour(0xff101821));
        // Obsidian graphite tree guides align with the browser panel dividers.
        pluginTree.setColour(juce::TreeView::linesColourId, juce::Colour(0xff40515f));
        // Obsidian turquoise highlights plugin drag-and-drop targets consistently.
        pluginTree.setColour(juce::TreeView::dragAndDropIndicatorColourId, juce::Colour(0xff54d6dc));
        addAndMakeVisible(pluginTree);
        thumbnailModeButton.setButtonText("MINIATURES");
        thumbnailModeButton.setTooltip("Affiche les captures authentiques du catalogue local quand elles existent; sinon une couverture provisoire.");
        thumbnailModeButton.setClickingTogglesState(true);
        thumbnailModeButton.onClick = [this]
        {
            favouritesFile().getSiblingFile("PluginThumbnailMode.txt").replaceWithText(
                thumbnailModeButton.getToggleState() ? "1" : "0");
            rebuildPluginTree();
        };
        thumbnailModeButton.setToggleState(
            favouritesFile().getSiblingFile("PluginThumbnailMode.txt").loadFileAsString().trim() == "1",
            juce::dontSendNotification);
        thumbnailModeButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1b2833));
        thumbnailModeButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff286674));
        thumbnailModeButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffeffcfd));
        addAndMakeVisible(thumbnailModeButton);
        importThumbnailButton.setButtonText("IMPORTER IMAGE");
        importThumbnailButton.setTooltip("Importer une capture reelle du plugin selectionne. Les images sont associees a son identifiant exact.");
        importThumbnailButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1b2833));
        importThumbnailButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffeffcfd));
        importThumbnailButton.onClick = [this] { showArtworkMenu(); };
        addAndMakeVisible(importThumbnailButton);

        spliceTitle.setText("SPLICE", juce::dontSendNotification);
        // Obsidian primary text keeps the Splice heading consistent with Browser tabs.
        spliceTitle.setColour(juce::Label::textColourId, juce::Colour(0xffeffcfd));
        spliceTitle.setFont(juce::Font(14.0f, juce::Font::bold));
        addAndMakeVisible(spliceTitle);
        spliceInfo.setText("Splice Sounds - charge le plugin officiel AU/VST3 dans Liberty pour l'interface complète.", juce::dontSendNotification);
        // Match Splice help text to Obsidian secondary typography.
        spliceInfo.setColour(juce::Label::textColourId, juce::Colour(0xffb9ccd6));
        spliceInfo.setFont(juce::Font(10.0f));
        spliceInfo.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(spliceInfo);
        // Obsidian primary action for opening the official Splice plugin.
        spliceOpenPluginButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff286674));
        spliceOpenPluginButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffeffcfd));
        spliceOpenPluginButton.setButtonText("OUVRIR SPLICE SOUNDS");
        spliceOpenPluginButton.onClick = [this] { openSpliceSounds(); };
        addAndMakeVisible(spliceOpenPluginButton);
        // Obsidian secondary Splice action: subdued graphite, readable blue-grey text.
        spliceScanButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1b2833));
        spliceScanButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffb9ccd6));
        spliceScanButton.setButtonText("SCAN SPLICE");
        spliceScanButton.onClick = [this] { scanPlugins(); };
        addAndMakeVisible(spliceScanButton);

        filesButton.setButtonText("PROJETS");
        audioButton.setButtonText("SAMPLES");
        midiButton.setButtonText("MIDI");
        presetsButton.setButtonText("PRESETS");
        pluginsButton.setButtonText("PLUGINS");
        pluginSearch.setTextToShowWhenEmpty(juce::String::fromUTF8("Rechercher un plugin ou fabricant..."), juce::Colour(0xff8da6b1));
        pluginSearch.setTooltip(juce::String::fromUTF8(
            "Rechercher un plugin. Flèches haut/bas : parcourir les résultats ; Entrée : charger le plugin."));
        pluginSearch.onNavigateResults = [this](int step)
        {
            if (category == Category::plugins) navigatePluginResults(step);
        };
        pluginSearch.onReturnKey = [this]
        {
            if (category != Category::plugins) return;
            if (pluginSearchPending) rebuildPluginTree();
            if (auto* selected = selectedPluginDescription())
            {
                const juce::PluginDescription description = *selected;
                loadPluginDescription(description);
            }
            else if (!keyboardResults.empty())
            {
                auto* first = keyboardResults.front();
                first->setSelected(true, true);
                pluginTree.scrollToKeepItemVisible(first);
                if (const auto* description = first->getPluginDescription())
                {
                    const juce::PluginDescription copy = *description;
                    loadPluginDescription(copy);
                }
            }
        };
        pluginSearch.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff101821));
        pluginSearch.setColour(juce::TextEditor::textColourId, juce::Colour(0xffeffcfd));
        pluginSearch.setColour(juce::TextEditor::outlineColourId, juce::Colour(0xff40515f));
        pluginSearch.onTextChange = [this]
        {
            if (category == Category::plugins)
            {
                // For 1,800+ plugins, avoid destroying every TreeViewItem and
                // decoding images for each keystroke. Empty search clears at
                // once; other queries settle briefly on the 10 Hz UI timer.
                pluginSearchPending = true;
                pluginSearchChangedAt = juce::Time::getMillisecondCounter();
                if (pluginSearch.getText().trim().isEmpty())
                    rebuildPluginTree();
            }
            else if (category != Category::splice)
            {
                filter.query = pluginSearch.getText().trim();
                directoryList.refresh();
            }
        };
        pluginSearch.onEscapeKey = [this]
        {
            pluginSearch.clear();
            pluginTree.grabKeyboardFocus();
        };
        addAndMakeVisible(pluginSearch);
        // One compact control next to search; no additional toolbar rows.
        pluginFiltersButton.setButtonText("FILTRES");
        pluginFiltersButton.setTooltip(juce::String::fromUTF8(
            "Combiner fabricant, format AU/VST3 et instrument/effet."));
        pluginFiltersButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff203746));
        pluginFiltersButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff286674));
        pluginFiltersButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffeffcfd));
        pluginFiltersButton.setColour(juce::TextButton::textColourOnId, juce::Colour(0xffeffcfd));
        pluginFiltersButton.onClick = [this] { showPluginFiltersMenu(); };
        addAndMakeVisible(pluginFiltersButton);
        pluginFiltersButton.setVisible(false);
        spliceButton.setButtonText("SPLICE");
        for (auto* b : { &filesButton, &audioButton, &midiButton, &presetsButton, &pluginsButton, &spliceButton })
        {
            // Obsidian inactive category surface matches browser manufacturer headers.
            b->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1b2833));
            // Obsidian active category surface matches selected step-sequencer accents.
            b->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff286674));
            // Obsidian muted blue-grey keeps inactive category labels readable.
            b->setColour(juce::TextButton::textColourOffId, juce::Colour(0xffb9ccd6));
            // Obsidian soft white completes the active category typography.
            b->setColour(juce::TextButton::textColourOnId, juce::Colour(0xffeffcfd));
            b->setClickingTogglesState(false);
            addAndMakeVisible(*b);
        }

        filesButton.onClick = [this] { setCategory(Category::files); };
        audioButton.onClick = [this] { setCategory(Category::audio); };
        midiButton.onClick = [this] { setCategory(Category::midi); };
        presetsButton.onClick = [this] { setCategory(Category::presets); };
        pluginsButton.onClick = [this] { setCategory(Category::plugins); };
        spliceButton.onClick = [this] { setCategory(Category::splice); };

        homeButton.setButtonText("HOME");
        // Obsidian HOME control matches the graphite category surfaces.
        homeButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1b2833));
        homeButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffeffcfd));
        homeButton.onClick = [this]
        {
            if (category == Category::audio)
            {
                const auto library = findInstalledSampleLibrary();
                if (library.isDirectory()) { setRoot(library); return; }
            }
            setRoot(juce::File::getSpecialLocation(juce::File::userHomeDirectory));
        };
        addAndMakeVisible(homeButton);
        chooseLibraryButton.setButtonText("CHOISIR");
        chooseLibraryButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1b2833));
        chooseLibraryButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffeffcfd));
        chooseLibraryButton.onClick = [this] { chooseSampleLibrary(); };
        addAndMakeVisible(chooseLibraryButton);
        saveFolderButton.setButtonText("AJOUTER FAVORI");
        openFavouriteButton.setButtonText("OUVRIR FAVORI");
        for (auto* button : { &saveFolderButton, &openFavouriteButton })
        {
            button->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1b2833));
            button->setColour(juce::TextButton::textColourOffId, juce::Colour(0xffeffcfd));
            addAndMakeVisible(*button);
        }
        saveFolderButton.onClick = [this]
        {
            if (!rootDirectory.isDirectory()) return;
            auto favourites = sampleFolderFavourites();
            const auto path = rootDirectory.getFullPathName();
            const int index = favourites.indexOf(path);
            if (index >= 0) favourites.remove(index);
            else favourites.add(path);
            sampleFolderFavouritesFile().replaceWithText(favourites.joinIntoString("\n"));
            updateSampleFolderControls();
        };
        openFavouriteButton.onClick = [this]
        {
            juce::PopupMenu menu;
            const auto favourites = sampleFolderFavourites();
            for (int i = 0; i < favourites.size(); ++i)
            {
                const juce::File folder(favourites[i]);
                menu.addItem(i + 1, folder.getFileName() + (folder.isDirectory() ? "" : " (indisponible)"), folder.isDirectory());
            }
            if (favourites.isEmpty()) menu.addItem(1, "Aucun dossier favori", false);
            juce::Component::SafePointer<BrowserPanel> safe(this);
            menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&openFavouriteButton),
                [safe, favourites](int result)
                {
                    if (safe == nullptr || result < 1 || result > favourites.size()) return;
                    const juce::File folder(favourites[result - 1]);
                    if (folder.isDirectory()) safe->setRoot(folder);
                });
        };
        parentFolderButton.setButtonText("DOSSIER PARENT");
        parentFolderButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1b2833));
        parentFolderButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffeffcfd));
        parentFolderButton.onClick = [this]
        {
            const auto parent = rootDirectory.getParentDirectory();
            if (parent.isDirectory() && parent != rootDirectory) setRoot(parent);
        };
        addAndMakeVisible(parentFolderButton);
        previewPlay.setButtonText(juce::String::fromUTF8("ÉCOUTER"));
        previewStop.setButtonText("STOP");
        for (auto* b : { &previewPlay, &previewStop })
        {
            b->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1b2833));
            b->setColour(juce::TextButton::textColourOffId, juce::Colour(0xffeffcfd));
            addAndMakeVisible(*b);
        }
        previewPlay.onClick = [this]
        {
            if (previewReader == nullptr) return;
            if (previewTransport.hasStreamFinished()) previewTransport.setPosition(0.0);
            previewTransport.start();
        };
        previewStop.onClick = [this] { previewTransport.stop(); previewTransport.setPosition(0.0); };
        previewVolume.setSliderStyle(juce::Slider::LinearHorizontal);
        previewVolume.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        previewVolume.setRange(0.0, 1.0, 0.01);
        previewVolume.setValue(0.7, juce::dontSendNotification);
        previewVolume.onValueChange = [this] { previewTransport.setGain((float)previewVolume.getValue()); };
        addAndMakeVisible(previewVolume);
        previewAuto.setButtonText("AUTO");
        previewAuto.setColour(juce::ToggleButton::textColourId, juce::Colour(0xffeffcfd));
        addAndMakeVisible(previewAuto);
        previewInfo.setText("Aucun sample sélectionné", juce::dontSendNotification);
        previewInfo.setColour(juce::Label::textColourId, juce::Colour(0xffeffcfd));
        previewInfo.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(previewInfo);
        samplePackInfo.setColour(juce::Label::textColourId, juce::Colour(0xffb8e8ec));
        samplePackInfo.setFont(juce::Font(10.0f));
        samplePackInfo.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(samplePackInfo);
        browsePacksButton.setButtonText("PACKS");
        browsePacksButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1b2833));
        browsePacksButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffeffcfd));
        browsePacksButton.onClick = [this] { showInstalledPacks(); };
        addAndMakeVisible(browsePacksButton);
        musicFilterButton.setButtonText("FILTRES");
        musicFilterButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1b2833));
        musicFilterButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffeffcfd));
        musicFilterButton.onClick = [this] { showMusicalFilters(); };
        addAndMakeVisible(musicFilterButton);
        sampleNameSearch.setTextToShowWhenEmpty("RECHERCHER UN SAMPLE...", juce::Colour(0xff8095a8));
        sampleNameSearch.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff101d2b));
        sampleNameSearch.setColour(juce::TextEditor::textColourId, juce::Colour(0xffeff7ff));
        sampleNameSearch.setColour(juce::TextEditor::outlineColourId, juce::Colour(0xff38536a));
        sampleNameSearch.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colour(0xff54d6dc));
        sampleNameSearch.setColour(juce::TextEditor::highlightColourId, juce::Colour(0xff286674));
        sampleNameSearch.setFont(juce::Font(12.0f));
        sampleNameSearch.onTextChange = [this]
        {
            sampleNameQuery = sampleNameSearch.getText();
            saveSampleFilterPreferences();
            sampleSearchPending = true;
            sampleSearchChangedAt = juce::Time::getMillisecondCounter();
        };
        addAndMakeVisible(sampleNameSearch);
        const auto savedSampleFilters = juce::StringArray::fromLines(sampleFilterPreferenceFile().loadFileAsString());
        if (savedSampleFilters.size() > 0) sampleFilterMask = savedSampleFilters[0].getIntValue() & 31;
        if (savedSampleFilters.size() > 1)
        {
            sampleNameQuery = savedSampleFilters[1];
            sampleNameSearch.setText(sampleNameQuery, juce::dontSendNotification);
        }

        scanPluginsButton.setButtonText("SCAN AU + VST3");
        blacklistButton.setButtonText("BLACKLIST");
        clearBlacklistButton.setButtonText("CLEAR BL");
        favouritePluginButton.setButtonText("FAV");
        loadPluginButton.setButtonText("LOAD");
        openPluginButton.setButtonText("OPEN UI");
        unloadPluginButton.setButtonText("UNLOAD");
        for (auto* b : { &scanPluginsButton, &blacklistButton, &clearBlacklistButton, &favouritePluginButton,
                         &loadPluginButton, &openPluginButton, &unloadPluginButton })
        {
            // Obsidian plugin actions share the graphite surface and soft-white typography.
            b->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1b2833));
            b->setColour(juce::TextButton::textColourOffId, juce::Colour(0xffeffcfd));
            addAndMakeVisible(*b);
        }

        scanPluginsButton.onClick = [this] { scanPlugins(); };
        blacklistButton.onClick = [this] { showBlacklist(); };
        clearBlacklistButton.onClick = [this] { clearBlacklist(); };
        favouritePluginButton.onClick = [this] { toggleSelectedFavourite(); };
        loadPluginButton.onClick = [this] { loadSelectedPlugin(); };
        openPluginButton.onClick = [this] { openLoadedPluginEditor(); };
        unloadPluginButton.onClick = [this] { unloadPlugin(); };

        chorusButton.setButtonText("1K CHORUS");
        flangerButton.setButtonText("1K FLANGER");
        phaserButton.setButtonText("1K PHASER");
        tremoloButton.setButtonText("1K TREMOLO");
        reverbButton.setButtonText("1K REVERB");
        delayButton.setButtonText("1K DELAY");
        driveButton.setButtonText("1K DRIVE");
        compressorButton.setButtonText("1K COMP");
        saturationButton.setButtonText("1K SAT");
        widthButton.setButtonText("1K WIDTH");
        filterButton.setButtonText("1K FILTER");
        doublerButton.setButtonText("1K DOUBLER");
        exciterButton.setButtonText("1K EXCITER"); deEsserButton.setButtonText("1K DE-ESS");
        gateButton.setButtonText("1K GATE"); bassBoostButton.setButtonText("1K BASS");
        airButton.setButtonText("1K AIR"); punchButton.setButtonText("1K PUNCH"); softClipButton.setButtonText("1K CLIP");
        clearOneKnobButton.setButtonText("1K OFF");
        for (auto* b : { &chorusButton, &flangerButton, &phaserButton, &tremoloButton, &reverbButton, &delayButton, &driveButton, &compressorButton, &saturationButton, &widthButton, &filterButton, &doublerButton, &exciterButton, &deEsserButton, &gateButton, &bassBoostButton, &airButton, &punchButton, &softClipButton, &clearOneKnobButton })
        {
            // Obsidian One Knob action buttons use the shared graphite and cyan palette.
            b->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1b2833));
            b->setColour(juce::TextButton::textColourOffId, juce::Colour(0xffb8e8ec));
            b->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff286674));
            b->setColour(juce::TextButton::textColourOnId, juce::Colour(0xffeffcfd));
            addAndMakeVisible(*b);
        }
        chorusButton.onClick = [this] { loadOneKnob(LibertyOneKnobRack::Type::chorus); };
        flangerButton.onClick = [this] { loadOneKnob(LibertyOneKnobRack::Type::flanger); };
        phaserButton.onClick = [this] { loadOneKnob(LibertyOneKnobRack::Type::phaser); };
        tremoloButton.onClick = [this] { loadOneKnob(LibertyOneKnobRack::Type::tremolo); };
        reverbButton.onClick = [this] { loadOneKnob(LibertyOneKnobRack::Type::reverb); };
        delayButton.onClick = [this] { loadOneKnob(LibertyOneKnobRack::Type::delay); };
        driveButton.onClick = [this] { loadOneKnob(LibertyOneKnobRack::Type::drive); };
        compressorButton.onClick = [this] { loadOneKnob(LibertyOneKnobRack::Type::compressor); };
        saturationButton.onClick = [this] { loadOneKnob(LibertyOneKnobRack::Type::saturation); };
        widthButton.onClick = [this] { loadOneKnob(LibertyOneKnobRack::Type::stereoWidth); };
        filterButton.onClick = [this] { loadOneKnob(LibertyOneKnobRack::Type::filter); };
        doublerButton.onClick = [this] { loadOneKnob(LibertyOneKnobRack::Type::doubler); };
        exciterButton.onClick = [this] { loadOneKnob(LibertyOneKnobRack::Type::exciter); };
        deEsserButton.onClick = [this] { loadOneKnob(LibertyOneKnobRack::Type::deEsser); };
        gateButton.onClick = [this] { loadOneKnob(LibertyOneKnobRack::Type::gate); };
        bassBoostButton.onClick = [this] { loadOneKnob(LibertyOneKnobRack::Type::bassBoost); };
        airButton.onClick = [this] { loadOneKnob(LibertyOneKnobRack::Type::air); };
        punchButton.onClick = [this] { loadOneKnob(LibertyOneKnobRack::Type::punch); };
        softClipButton.onClick = [this] { loadOneKnob(LibertyOneKnobRack::Type::softClip); };
        clearOneKnobButton.onClick = [this] { clearOneKnob(); };

        // Obsidian secondary text aligns plugin status with browser hints.
        pluginStatus.setColour(juce::Label::textColourId, juce::Colour(0xffb9ccd6));
        pluginStatus.setFont(juce::Font(10.0f));
        pluginStatus.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(pluginStatus);

        // Compact PNG coverage dashboard: separate from the capture progress
        // label so the two messages never overwrite one another.
        artworkStatsLabel.setColour(juce::Label::backgroundColourId, juce::Colour(0xff142638));
        artworkStatsLabel.setColour(juce::Label::textColourId, juce::Colour(0xffb8e8ec));
        artworkStatsLabel.setColour(juce::Label::outlineColourId, juce::Colour(0xff40515f));
        artworkStatsLabel.setFont(juce::Font(9.0f, juce::Font::bold));
        artworkStatsLabel.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(artworkStatsLabel);
        artworkStatsLabel.setVisible(false);

        setVisible(false);
        owner.addAndMakeVisible(this);
        setRoot(juce::File::getSpecialLocation(juce::File::userHomeDirectory));
        refreshPlugins();
        setCategory(Category::files);
        startTimerHz(10);
    }

    ~BrowserPanel() override { shutdown(); }

    void shutdown()
    {
        if (stopped.exchange(true)) return;
        batchArtworkCancel.store(true);
        stopTimer();
        previewTransport.stop();
        owner.audioEngine.getDeviceManager().removeAudioCallback(&previewPlayer);
        previewPlayer.setSource(nullptr);
        previewTransport.setSource(nullptr);
        previewReader.reset();
        if (artworkThread.joinable()) artworkThread.join();
        if (batchArtworkThread.joinable()) batchArtworkThread.join();
        if (captureThread.joinable()) captureThread.join();
        if (sampleSearchThread.joinable()) sampleSearchThread.join();
        if (scanThread.joinable()) scanThread.join();
        pluginTree.setRootItem(nullptr);
        pluginRoot.reset();
        fileTree.removeListener(this);
        thread.stopThread(1500);
        setVisible(false);
        toggleButton.setVisible(false);
    }

    void paint(juce::Graphics& g) override
    {
        // Obsidian deep graphite aligns the browser with mixer and editor panels.
        g.fillAll(LibertyUI::Colour::panel);
        // Obsidian browser edge matches the graphite dividers throughout Liberty.
        g.setColour(juce::Colour(0xff40515f));
        g.drawVerticalLine(0, 0.0f, (float)getHeight());
        // Obsidian soft-white title matches primary labels across Liberty Pro.
        g.setColour(LibertyUI::Colour::textPrimary);
        g.setFont(juce::Font(15.0f, juce::Font::bold));
        g.drawText("LIBERTY  /  NAVIGATEUR", 14, 10, juce::jmax(0, getWidth() - 96), 24, juce::Justification::centredLeft, true);
        g.setColour(juce::Colour(0xff54d6dc));
        g.fillRoundedRectangle(12.0f, 35.0f, juce::jmax(0.0f, (float)getWidth() - 24.0f), 2.0f, 1.0f);
        // Obsidian separator clearly distinguishes browser controls from the plugin list.
        g.setColour(juce::Colour(0xff40515f));
        g.fillRect(0, 100, getWidth(), 1);
        if (category == Category::audio)
        {
            g.setColour(juce::Colour(0xff89a6b9));
            g.setFont(juce::Font(10.0f, juce::Font::bold));
            g.drawText(juce::String::fromUTF8("BIBLIOTHÈQUE DE SAMPLES"), 14, 219, juce::jmax(0, getWidth() - 28), 11, juce::Justification::centredLeft, true);
        }
        if (category == Category::audio)
        {
            g.setColour(juce::Colour(0xff142638));
            g.fillRoundedRectangle(7.0f, 102.0f, (float)juce::jmax(0, getWidth() - 14), 116.0f, 8.0f);
            g.setColour(juce::Colour(0xff324d63));
            g.drawRoundedRectangle(7.0f, 102.0f, (float)juce::jmax(0, getWidth() - 14), 116.0f, 8.0f, 1.0f);
            const int previewTop = juce::jmax(136, getHeight() - 144);
            g.setColour(juce::Colour(0xff142638));
            g.fillRoundedRectangle(7.0f, (float)previewTop - 5.0f, (float)juce::jmax(0, getWidth() - 14), 134.0f, 8.0f);
            g.setColour(juce::Colour(0xff324d63));
            g.drawRoundedRectangle(7.0f, (float)previewTop - 5.0f, (float)juce::jmax(0, getWidth() - 14), 134.0f, 8.0f, 1.0f);
            const auto wave = juce::Rectangle<int>(14, previewTop + 24, juce::jmax(0, getWidth() - 28), 36);
            g.setColour(juce::Colour(0xff0c1723));
            g.fillRoundedRectangle(wave.toFloat(), 4.0f);
            if (previewThumbnail.getTotalLength() > 0.0)
            {
                g.setColour(juce::Colour(0xff54d6dc));
                previewThumbnail.drawChannels(g, wave, 0.0, previewThumbnail.getTotalLength(), 0.85f);
                const auto fraction = juce::jlimit(0.0, 1.0, previewTransport.getCurrentPosition() / previewThumbnail.getTotalLength());
                g.setColour(juce::Colour(0xfff0c879));
                g.drawVerticalLine(wave.getX() + (int)(fraction * wave.getWidth()), (float)wave.getY(), (float)wave.getBottom());
            }
        }
        if (category == Category::plugins)
        {
            // Actual in-app One Knob section header, rather than a mockup-only label.
            g.setColour(juce::Colour(0xff54d6dc));
            g.fillRoundedRectangle(12.0f, 177.0f, 3.0f, 10.0f, 1.5f);
            g.setColour(juce::Colour(0xffb8e8ec));
            g.setFont(juce::Font(11.0f, juce::Font::bold));
            g.drawText("ONE KNOB FX", 22, 172, getWidth() - 34, 20, juce::Justification::centredLeft);
        }
        // The readout occupies y=374..392, the bar y=394..397 and the
        // list starts at y=400. All three surfaces remain non-overlapping.
        if (category == Category::plugins && getHeight() >= 540)
        {
            const float barWidth = (float) juce::jmax(0, getWidth() - 20);
            g.setColour(juce::Colour(0xff243d50));
            g.fillRoundedRectangle(10.0f, 394.0f, barWidth, 3.0f, 1.5f);
            if (artworkTotalCount > 0 && artworkPresentCount > 0)
            {
                const float fraction = (float) artworkPresentCount / (float) artworkTotalCount;
                g.setColour(juce::Colour(0xff54d6dc));
                g.fillRoundedRectangle(10.0f, 394.0f, barWidth * fraction, 3.0f, 1.5f);
            }
        }
        // Obsidian secondary label improves category hint legibility on deep graphite.
        g.setColour(LibertyUI::Colour::textMuted);
        g.setFont(juce::Font(9.0f));
        g.drawText(categoryHint(), 14, getHeight() - 29, getWidth() - 28, 18, juce::Justification::centredLeft, true);
    }

    void resized() override
    {
        closeButton.setBounds(getWidth() - 72, 8, 62, 26);
        const int tabGap = 3;
        const int tabWidth = juce::jmax(0, (juce::jmax(0, getWidth() - 16) - tabGap * 5) / 6);
        int tabX = 8;
        for (auto* button : { &filesButton, &audioButton, &midiButton, &presetsButton, &pluginsButton, &spliceButton })
        {
            button->setBounds(tabX, 42, tabWidth, 26);
            tabX += tabWidth + tabGap;
        }

        homeButton.setBounds(12, 105, 70, 24);
        chooseLibraryButton.setBounds(86, 105, 78, 24);
        parentFolderButton.setBounds(category == Category::audio ? 168 : 86, 105,
            juce::jmax(0, getWidth() - (category == Category::audio ? 180 : 98)), 24);
        const int folderFavWidth = juce::jmax(0, (getWidth() - 24) / 2);
        saveFolderButton.setBounds(10, 136, folderFavWidth, 25);
        openFavouriteButton.setBounds(14 + folderFavWidth, 136, folderFavWidth, 25);
        samplePackInfo.setBounds(14, 165, juce::jmax(0, getWidth() - 190), 22);
        browsePacksButton.setBounds(juce::jmax(10, getWidth() - 160), 165, 70, 22);
        musicFilterButton.setBounds(juce::jmax(10, getWidth() - 86), 165, 76, 22);
        sampleNameSearch.setBounds(14, 190, juce::jmax(0, getWidth() - 28), 24);
        samplePackInfo.setFont(juce::Font(10.0f));
        previewInfo.setFont(juce::Font(11.0f, juce::Font::bold));
        fileTree.setBounds(10, category == Category::audio ? 230 : 136,
            juce::jmax(0, getWidth() - 20),
            juce::jmax(0, getHeight() - (category == Category::audio ? 382 : 248)));
        const int previewY = juce::jmax(136, getHeight() - 144);
        const int previewW = juce::jmax(0, (getWidth() - 24) / 2);
        previewInfo.setBounds(14, previewY, juce::jmax(0, getWidth() - 28), 22);
        previewPlay.setBounds(10, previewY + 65, previewW, 26);
        previewStop.setBounds(14 + previewW, previewY + 65, previewW, 26);
        previewVolume.setBounds(10, previewY + 97, juce::jmax(0, getWidth() - 96), 24);
        previewAuto.setBounds(juce::jmax(10, getWidth() - 78), previewY + 97, 68, 24);

        scanPluginsButton.setBounds(10, 108, getWidth() - 20, 28);
        const int blacklistW = juce::jmax(0, (getWidth() - 24) / 2);
        blacklistButton.setBounds(10, 140, blacklistW, 26);
        clearBlacklistButton.setBounds(14 + blacklistW, 140, blacklistW, 26);
        const int oneKnobY = 192;
        const int oneKnobW = juce::jmax(0, (getWidth() - 26) / 4);
        juce::TextButton* oneKnobButtons[] = { &chorusButton, &flangerButton, &phaserButton, &tremoloButton,
            &reverbButton, &delayButton, &driveButton, &compressorButton, &saturationButton, &widthButton,
            &filterButton, &doublerButton, &exciterButton, &deEsserButton, &gateButton, &bassBoostButton, &airButton, &punchButton, &softClipButton, &clearOneKnobButton };
        for (int i = 0; i < 20; ++i)
            oneKnobButtons[i]->setBounds(10 + (i % 4) * (oneKnobW + 2), oneKnobY + (i / 4) * 30, oneKnobW, 28);
        // Plugin search and filters share the same horizontal band without
        // reducing the thumbnail/list vertical space.
        if (category == Category::plugins)
        {
            constexpr int filterWidth = 87;
            constexpr int gap = 7;
            pluginSearch.setBounds(10, 76, juce::jmax(0, getWidth() - 20 - filterWidth - gap), 23);
            pluginFiltersButton.setBounds(juce::jmax(10, getWidth() - 10 - filterWidth),
                                          76, filterWidth, 23);
        }
        else
        {
            pluginSearch.setBounds(10, 76, juce::jmax(0, getWidth() - 20), 23);
            pluginFiltersButton.setBounds(0, 0, 0, 0);
        }
        const int thumbnailW = juce::jmax(0, (getWidth() - 24) / 2);
        thumbnailModeButton.setBounds(10, 343, thumbnailW, 26);
        importThumbnailButton.setBounds(14 + thumbnailW, 343, thumbnailW, 26);
        const bool showArtworkDashboard = category == Category::plugins && getHeight() >= 540;
        artworkStatsLabel.setBounds(10, 374, juce::jmax(0, getWidth() - 20), 18);
        artworkStatsLabel.setVisible(showArtworkDashboard);
        const int treeTop = showArtworkDashboard ? 400 : 374;
        const int treeBottomMargin = showArtworkDashboard ? 502 : 476;
        pluginTree.setBounds(10, treeTop, juce::jmax(0, getWidth() - 20),
                             juce::jmax(0, getHeight() - treeBottomMargin));
        const int controlsY = getHeight() - 94;
        const int w = juce::jmax(0, (getWidth() - 28) / 4);
        favouritePluginButton.setBounds(10, controlsY, w, 28);
        loadPluginButton.setBounds(14 + w, controlsY, w, 28);
        openPluginButton.setBounds(18 + w * 2, controlsY, w, 28);
        unloadPluginButton.setBounds(22 + w * 3, controlsY, w, 28);
        pluginStatus.setBounds(10, controlsY + 31, getWidth() - 20, 28);
        spliceTitle.setBounds(12, 100, getWidth() - 24, 28);
        spliceInfo.setBounds(12, 128, getWidth() - 24, 52);
        const int spliceButtonW = juce::jmax(0, (getWidth() - 28) / 2);
        spliceOpenPluginButton.setBounds(12, 188, spliceButtonW, 32);
        spliceScanButton.setBounds(16 + spliceButtonW, 188, juce::jmax(0, getWidth() - 28 - spliceButtonW), 32);
    }

private:
    enum class Category { files, audio, midi, presets, plugins, splice };
    enum class ArtworkView { all, withImage, withoutImage, failed };
    enum class PluginFormatFilter { all, au, vst3 };
    enum class PluginTypeFilter { all, instrument, effect };
    enum class QuickView { all, favourites, recent };
    enum class PluginUsage {
        all, synthesizer, keyboards, drums, bass, guitar, orchestral, sampler,
        reverb, delay, compressor, equalizer, distortion, modulation,
        dynamics, utility, unclassified
    };

    static juce::String usageLabel(PluginUsage usage)
    {
        switch (usage)
        {
            case PluginUsage::synthesizer: return juce::String::fromUTF8("Synthétiseurs");
            case PluginUsage::keyboards:   return "Pianos / Claviers";
            case PluginUsage::drums:       return "Batteries / Percussions";
            case PluginUsage::bass:        return "Basses";
            case PluginUsage::guitar:      return "Guitares";
            case PluginUsage::orchestral:  return "Orchestral / Voix";
            case PluginUsage::sampler:     return "Samplers / ROMplers";
            case PluginUsage::reverb:      return juce::String::fromUTF8("Réverbérations");
            case PluginUsage::delay:       return juce::String::fromUTF8("Delays / Échos");
            case PluginUsage::compressor:  return "Compresseurs";
            case PluginUsage::equalizer:   return juce::String::fromUTF8("Égaliseurs");
            case PluginUsage::distortion:  return "Distorsion / Saturation";
            case PluginUsage::modulation:  return "Chorus / Modulation";
            case PluginUsage::dynamics:    return "Limiteurs / Gates";
            case PluginUsage::utility:     return "Utilitaires / Analyse";
            case PluginUsage::unclassified:return juce::String::fromUTF8("Non classés");
            case PluginUsage::all:         return "Toutes les familles";
        }
        return "Toutes les familles";
    }

    // The plugin exposes only descriptive metadata, not its audio algorithms.
    // Classify conservatively: exact plugin type first, then verified strings
    // supplied by the plugin (name and category). Unknowns remain available.
    static PluginUsage classifyPluginUsage(const juce::PluginDescription& d)
    {
        const auto name = d.name + " " + d.category;
        const auto containsAny = [&name](std::initializer_list<const char*> words)
        {
            for (auto* word : words)
                if (name.containsIgnoreCase(word)) return true;
            return false;
        };

        if (d.isInstrument)
        {
            if (containsAny({ "sampler", "sampling", "rompler", "sampletank", "kontakt" }))
                return PluginUsage::sampler;
            if (containsAny({ "bass", "basse" }))
                return PluginUsage::bass;
            if (containsAny({ "guitar", "guitare", "strum" }))
                return PluginUsage::guitar;
            if (containsAny({ "drum", "percussion", "drums", "battery", "batterie", "rhythm machine" }))
                return PluginUsage::drums;
            if (containsAny({ "piano", "rhodes", "wurl", "clavinet", "harpsichord",
                              "keyboard", "electric keys", "organ", "hammond" }))
                return PluginUsage::keyboards;
            if (containsAny({ "orchestral", "string", "violin", "viola", "cello",
                              "brass", "woodwind", "flute", "choir", "vocal", "voice" }))
                return PluginUsage::orchestral;
            if (containsAny({ "synth", "wavetable", "oscillator", "modular",
                              "subtractive", "analog", "analogue", "virtual analog" }))
                return PluginUsage::synthesizer;
        }
        else
        {
            if (containsAny({ "reverb", "reverber", "convolution", "convolver" }))
                return PluginUsage::reverb;
            if (containsAny({ "delay", "echo", "repeater" }))
                return PluginUsage::delay;
            if (containsAny({ "compress", "compressor" })
                || name.containsWholeWordIgnoreCase("comp"))
                return PluginUsage::compressor;
            if (containsAny({ "equaliz", "equalis", "equaliz", "parametric eq" })
                || name.containsWholeWordIgnoreCase("eq"))
                return PluginUsage::equalizer;
            if (containsAny({ "distort", "saturat", "overdrive", "fuzz",
                              "bitcrush", "bit crush", "amp sim", "drive" }))
                return PluginUsage::distortion;
            if (containsAny({ "chorus", "flang", "phas", "tremolo", "vibrato",
                              "rotary", "modulation" }))
                return PluginUsage::modulation;
            if (containsAny({ "limiter", "expander", "transient", "noise gate",
                              "de-esser", "deesser", "dynamics" })
                || name.containsWholeWordIgnoreCase("gate"))
                return PluginUsage::dynamics;
            if (containsAny({ "analy", "analys", "meter", "loudness", "tuner",
                              "utility", "stereo", "correlation", "panorama" })
                || name.containsWholeWordIgnoreCase("gain"))
                return PluginUsage::utility;
        }
        return PluginUsage::unclassified;
    }

    static const std::array<PluginUsage, 7>& instrumentUsages()
    {
        static const std::array<PluginUsage, 7> values = {
            PluginUsage::synthesizer, PluginUsage::keyboards, PluginUsage::drums,
            PluginUsage::bass, PluginUsage::guitar, PluginUsage::orchestral,
            PluginUsage::sampler
        };
        return values;
    }

    static const std::array<PluginUsage, 8>& effectUsages()
    {
        static const std::array<PluginUsage, 8> values = {
            PluginUsage::reverb, PluginUsage::delay, PluginUsage::compressor,
            PluginUsage::equalizer, PluginUsage::distortion, PluginUsage::modulation,
            PluginUsage::dynamics, PluginUsage::utility
        };
        return values;
    }



    static juce::File pluginCollectionsFile()
    {
        // Never mix collections with the JUCE plugin scan cache, favourites,
        // screenshot captures, or music-family corrections.
        return favouritesFile().getSiblingFile("PluginCollections.xml");
    }

    static juce::File pluginCollectionsBackupFile()
    {
        return pluginCollectionsFile().getSiblingFile("PluginCollections.backup.xml");
    }

    static bool validCollectionName(const juce::String& name)
    {
        return name.isNotEmpty() && name.length() <= 48
            && !name.containsAnyOf("\r\n\t");
    }

    juce::String existingCollectionName(const juce::String& candidate) const
    {
        for (const auto& entry : pluginCollections)
            if (entry.first.equalsIgnoreCase(candidate))
                return entry.first;
        return {};
    }

    static bool readPluginCollectionsFile(
        const juce::File& source,
        std::map<juce::String, juce::StringArray>& recovered)
    {
        recovered.clear();
        if (!source.existsAsFile() || source.getSize() <= 0
            || source.getSize() > 5 * 1024 * 1024)
            return false;

        auto document = juce::XmlDocument::parse(source);
        if (document == nullptr || !document->hasTagName("LibertyPluginCollections")
            || document->getIntAttribute("version", 1) != 1)
            return false;

        juce::StringArray seenNames;
        int entries = 0;
        for (auto* group = document->getFirstChildElement(); group != nullptr;
             group = group->getNextElement())
        {
            const auto name = group->getStringAttribute("name").trim();
            if (!group->hasTagName("collection") || !validCollectionName(name)
                || seenNames.contains(name, true) || recovered.size() >= 64)
                return false;

            seenNames.add(name);
            juce::StringArray members;
            for (auto* plugin = group->getFirstChildElement(); plugin != nullptr;
                 plugin = plugin->getNextElement())
            {
                const auto key = plugin->getStringAttribute("id");
                if (!plugin->hasTagName("plugin") || key.isEmpty()
                    || key.length() > 2048 || ++entries > 20000
                    || members.contains(key))
                    return false;
                members.add(key);
            }
            recovered.emplace(name, std::move(members));
        }
        return true; // An empty but valid collection database is allowed.
    }

    bool hasRecoverableCollectionsBackup() const
    {
        std::map<juce::String, juce::StringArray> saved;
        return readPluginCollectionsFile(pluginCollectionsBackupFile(), saved);
    }

    void loadPluginCollections()
    {
        pluginCollections.clear();
        collectionsReadOnly = false;
        const auto file = pluginCollectionsFile();
        if (!file.existsAsFile()) return;

        std::map<juce::String, juce::StringArray> recovered;
        if (!readPluginCollectionsFile(file, recovered))
        {
            // Keep the damaged main file untouched until explicit recovery.
            collectionsReadOnly = true;
            return;
        }
        pluginCollections = std::move(recovered);
    }

    bool savePluginCollections()
    {
        if (collectionsReadOnly) return false;
        juce::XmlElement root("LibertyPluginCollections");
        root.setAttribute("version", 1);
        for (const auto& group : pluginCollections)
        {
            auto* collection = root.createNewChildElement("collection");
            collection->setAttribute("name", group.first);
            for (const auto& key : group.second)
            {
                auto* item = collection->createNewChildElement("plugin");
                item->setAttribute("id", key);
            }
        }
        const auto target = pluginCollectionsFile();
        if (target.existsAsFile())
        {
            // First atomically preserve the previous *validated* version.
            // If backup fails, abort before touching the current database.
            std::map<juce::String, juce::StringArray> verifiedPrevious;
            if (!readPluginCollectionsFile(target, verifiedPrevious)) return false;
            const auto backup = pluginCollectionsBackupFile();
            juce::TemporaryFile backupStage(backup);
            if (!backupStage.getFile().replaceWithText(target.loadFileAsString())
                || !backupStage.overwriteTargetFileWithTemporary())
                return false;
        }
        juce::TemporaryFile staged(target);
        return staged.getFile().replaceWithText(root.toString())
            && staged.overwriteTargetFileWithTemporary();
    }

    void restorePluginCollectionsBackup()
    {
        const auto backup = pluginCollectionsBackupFile();
        std::map<juce::String, juce::StringArray> recovered;
        if (!readPluginCollectionsFile(backup, recovered))
        {
            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::WarningIcon, "Liberty - Collections",
                juce::String::fromUTF8("Aucune sauvegarde locale valide à restaurer."),
                "OK");
            return;
        }

        juce::Component::SafePointer<BrowserPanel> safe(this);
        juce::AlertWindow::showOkCancelBox(
            juce::AlertWindow::WarningIcon,
            juce::String::fromUTF8("Restaurer les collections ?"),
            juce::String::fromUTF8(
                "Restaurer la version précédente ? Le fichier actuel sera conservé séparément avant restauration."),
            "RESTAURER", "ANNULER", nullptr,
            juce::ModalCallbackFunction::create([safe](int result)
            {
                if (safe == nullptr || safe->stopped.load() || result != 1) return;
                const auto backupFile = pluginCollectionsBackupFile();
                std::map<juce::String, juce::StringArray> snapshot;
                if (!readPluginCollectionsFile(backupFile, snapshot))
                {
                    safe->showCollectionsSaveError();
                    return;
                }
                const auto target = pluginCollectionsFile();
                if (target.existsAsFile())
                {
                    // Preserve the old XML, including a damaged one, for
                    // inspection. Never overwrite a previously preserved copy.
                    const auto tag = juce::Time::getCurrentTime()
                        .formatted("%Y%m%d-%H%M%S");
                    const auto prefix = "PluginCollections-avant-restauration-" + tag;
                    auto savedOld = target.getSiblingFile(prefix + ".xml");
                    for (int n = 1; savedOld.exists(); ++n)
                        savedOld = target.getSiblingFile(prefix + "-" + juce::String(n) + ".xml");
                    if (!target.copyFileTo(savedOld))
                    {
                        safe->showCollectionsSaveError();
                        return;
                    }
                }
                juce::TemporaryFile stage(target);
                if (!stage.getFile().replaceWithText(backupFile.loadFileAsString())
                    || !stage.overwriteTargetFileWithTemporary())
                {
                    safe->showCollectionsSaveError();
                    return;
                }
                safe->loadPluginCollections();
                if (safe->collectionsReadOnly)
                {
                    safe->showCollectionsSaveError();
                    return;
                }
                if (safe->selectedCollection.isNotEmpty()
                    && safe->pluginCollections.find(safe->selectedCollection)
                        == safe->pluginCollections.end())
                    safe->selectedCollection.clear();
                safe->rebuildPluginTree();
                safe->pluginStatus.setText(juce::String::fromUTF8(
                    "Collections restaurées depuis la sauvegarde locale."),
                    juce::dontSendNotification);
            }));
    }

    void showCollectionsSaveError()
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon, "Liberty - Collections",
            juce::String::fromUTF8(
                "Impossible d'enregistrer les collections. Le fichier existant est conservé."),
            "OK");
    }

    bool commitPluginCollections(
        const std::map<juce::String, juce::StringArray>& oldState,
        const juce::String& message)
    {
        if (!savePluginCollections())
        {
            pluginCollections = oldState;
            showCollectionsSaveError();
            return false;
        }
        rebuildPluginTree();
        pluginStatus.setText(message, juce::dontSendNotification);
        return true;
    }

    void togglePluginCollection(const juce::PluginDescription& description,
                                const juce::String& collectionName)
    {
        auto it = pluginCollections.find(collectionName);
        if (it == pluginCollections.end() || collectionsReadOnly) return;
        const auto key = favouriteKey(description);
        if (key.isEmpty()) return;
        const auto old = pluginCollections;
        const int index = it->second.indexOf(key);
        if (index >= 0) it->second.remove(index);
        else it->second.add(key);
        const auto message = description.name
            + (index >= 0 ? juce::String::fromUTF8(" retiré de « ")
                          : juce::String::fromUTF8(" ajouté à « "))
            + collectionName + juce::String::fromUTF8(" »");
        commitPluginCollections(old, message);
    }

    // A single dialog supports creating a collection and renaming one.
    // Optional pluginKey allows "New collection..." from a plugin's right-click
    // menu to immediately place that plugin in the new group.
    void promptCollectionName(const juce::String& renameFrom = {},
                              const juce::String& pluginKey = {})
    {
        if (collectionsReadOnly)
        {
            showCollectionsSaveError();
            return;
        }
        const bool rename = renameFrom.isNotEmpty();
        if (!rename && pluginCollections.size() >= 64)
        {
            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::WarningIcon, "Liberty - Collections",
                juce::String::fromUTF8("Limite de 64 collections atteinte."), "OK");
            return;
        }
        auto dialog = std::make_shared<juce::AlertWindow>(
            rename ? juce::String::fromUTF8("Renommer la collection")
                   : juce::String::fromUTF8("Nouvelle collection Liberty"),
            juce::String::fromUTF8(
                "Saisis un nom court pour ta collection (48 caractères maximum)."),
            juce::AlertWindow::NoIcon);
        dialog->addTextEditor("collectionName", renameFrom,
                              juce::String::fromUTF8("NOM DE LA COLLECTION"));
        dialog->addButton(rename ? "RENOMMER" : juce::String::fromUTF8("CRÉER"), 1);
        dialog->addButton("ANNULER", 0);
        dialog->centreAroundComponent(&pluginFiltersButton, 420, 195);
        juce::Component::SafePointer<BrowserPanel> safe(this);
        dialog->enterModalState(true, juce::ModalCallbackFunction::create(
            [safe, dialog, renameFrom, pluginKey, rename](int result)
            {
                if (safe == nullptr || safe->stopped.load() || result != 1) return;
                const auto name = dialog->getTextEditorContents("collectionName").trim();
                const auto duplicate = safe->existingCollectionName(name);
                if (!validCollectionName(name)
                    || (duplicate.isNotEmpty()
                        && (!renameFrom.equalsIgnoreCase(duplicate) || !rename)))
                {
                    juce::AlertWindow::showMessageBoxAsync(
                        juce::AlertWindow::WarningIcon, "Liberty - Collections",
                        juce::String::fromUTF8(
                            "Nom invalide ou déjà utilisé. Choisis un autre nom (48 caractères max)."),
                        "OK");
                    return;
                }

                const auto old = safe->pluginCollections;
                const auto oldSelection = safe->selectedCollection;
                if (rename)
                {
                    auto oldGroup = safe->pluginCollections.find(renameFrom);
                    if (oldGroup == safe->pluginCollections.end()) return;
                    auto members = oldGroup->second;
                    safe->pluginCollections.erase(oldGroup);
                    safe->pluginCollections.emplace(name, std::move(members));
                    if (safe->selectedCollection.equalsIgnoreCase(renameFrom))
                        safe->selectedCollection = name;
                }
                else
                {
                    juce::StringArray members;
                    if (pluginKey.isNotEmpty()) members.add(pluginKey);
                    safe->pluginCollections.emplace(name, std::move(members));
                }
                if (!safe->commitPluginCollections(old,
                    (rename ? juce::String::fromUTF8("Collection renommée : ")
                            : juce::String::fromUTF8("Collection créée : ")) + name))
                    safe->selectedCollection = oldSelection;
            }), false);
    }

    void confirmDeleteCollection(const juce::String& name)
    {
        if (collectionsReadOnly || pluginCollections.find(name) == pluginCollections.end())
            return;
        juce::Component::SafePointer<BrowserPanel> safe(this);
        juce::AlertWindow::showOkCancelBox(
            juce::AlertWindow::WarningIcon,
            juce::String::fromUTF8("Supprimer la collection ?"),
            juce::String::fromUTF8(
                "Supprimer la collection « ") + name
                + juce::String::fromUTF8(
                    " » et ses associations ? Les plugins installés ne seront pas supprimés."),
            "SUPPRIMER", "ANNULER", nullptr,
            juce::ModalCallbackFunction::create([safe, name](int result)
            {
                if (safe == nullptr || safe->stopped.load() || result != 1) return;
                const auto old = safe->pluginCollections;
                const auto oldSelection = safe->selectedCollection;
                safe->pluginCollections.erase(name);
                if (safe->selectedCollection == name)
                    safe->selectedCollection.clear();
                if (!safe->commitPluginCollections(old,
                    juce::String::fromUTF8("Collection supprimée : ") + name))
                    safe->selectedCollection = oldSelection;
            }));
    }


    void exportPluginCollections()
    {
        if (pluginCollections.empty())
        {
            pluginStatus.setText(juce::String::fromUTF8(
                "Aucune collection à exporter."), juce::dontSendNotification);
            return;
        }
        // Export all known identifiers, even if those plugins are temporarily
        // missing. They can reconnect after reinstallation on another Mac.
        juce::XmlElement root("LibertyPluginCollections");
        root.setAttribute("version", 1);
        for (const auto& group : pluginCollections)
        {
            auto* collection = root.createNewChildElement("collection");
            collection->setAttribute("name", group.first);
            for (const auto& key : group.second)
            {
                auto* plugin = collection->createNewChildElement("plugin");
                plugin->setAttribute("id", key);
            }
        }
        auto chooser = std::make_shared<juce::FileChooser>(
            juce::String::fromUTF8("Exporter les collections Liberty"),
            juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                .getChildFile("Liberty-Collections-Plugins.xml"),
            "*.xml");
        juce::Component::SafePointer<BrowserPanel> safe(this);
        chooser->launchAsync(juce::FileBrowserComponent::saveMode
                | juce::FileBrowserComponent::canSelectFiles
                | juce::FileBrowserComponent::warnAboutOverwriting,
            [safe, chooser, xml = root.toString()](const juce::FileChooser& fc)
            {
                if (safe == nullptr || safe->stopped.load()) return;
                auto file = fc.getResult();
                if (file == juce::File()) return;
                if (!file.hasFileExtension("xml")) file = file.withFileExtension("xml");
                if (file.replaceWithText(xml))
                    safe->pluginStatus.setText(
                        juce::String::fromUTF8("Collections exportées : ")
                            + file.getFileName(), juce::dontSendNotification);
                else
                    juce::AlertWindow::showMessageBoxAsync(
                        juce::AlertWindow::WarningIcon, "Liberty - Collections",
                        juce::String::fromUTF8("Exportation impossible. Vérifie le dossier choisi."),
                        "OK");
            });
    }

    void importPluginCollections()
    {
        if (collectionsReadOnly)
        {
            showCollectionsSaveError();
            return;
        }
        auto chooser = std::make_shared<juce::FileChooser>(
            juce::String::fromUTF8("Importer des collections Liberty"),
            juce::File::getSpecialLocation(juce::File::userDocumentsDirectory), "*.xml");
        juce::Component::SafePointer<BrowserPanel> safe(this);
        chooser->launchAsync(juce::FileBrowserComponent::openMode
                | juce::FileBrowserComponent::canSelectFiles,
            [safe, chooser](const juce::FileChooser& fc)
            {
                if (safe == nullptr || safe->stopped.load()) return;
                const auto source = fc.getResult();
                if (!source.existsAsFile()) return;
                if (source.getSize() <= 0 || source.getSize() > 5 * 1024 * 1024)
                {
                    juce::AlertWindow::showMessageBoxAsync(
                        juce::AlertWindow::WarningIcon, "Liberty - Collections",
                        juce::String::fromUTF8("Fichier vide ou trop volumineux (5 Mo maximum)."),
                        "OK");
                    return;
                }
                auto xml = juce::XmlDocument::parse(source);
                if (xml == nullptr || !xml->hasTagName("LibertyPluginCollections")
                    || xml->getIntAttribute("version", 1) != 1)
                {
                    juce::AlertWindow::showMessageBoxAsync(
                        juce::AlertWindow::WarningIcon, "Liberty - Collections",
                        juce::String::fromUTF8("Ce fichier n'est pas une sauvegarde de collections Liberty."),
                        "OK");
                    return;
                }
                // Validate the whole document before any mutation. A malformed
                // or oversized backup never modifies existing collections.
                std::map<juce::String, juce::StringArray> incoming;
                juce::StringArray importedNames;
                int memberships = 0;
                bool valid = true;
                for (auto* collection = xml->getFirstChildElement(); collection != nullptr;
                     collection = collection->getNextElement())
                {
                    const auto name = collection->getStringAttribute("name").trim();
                    if (!collection->hasTagName("collection")
                        || !validCollectionName(name) || importedNames.contains(name, true)
                        || incoming.size() >= 64)
                    {
                        valid = false;
                        break;
                    }
                    importedNames.add(name);
                    juce::StringArray members;
                    for (auto* plugin = collection->getFirstChildElement(); plugin != nullptr;
                         plugin = plugin->getNextElement())
                    {
                        const auto key = plugin->getStringAttribute("id");
                        if (!plugin->hasTagName("plugin") || key.isEmpty()
                            || key.length() > 2048 || ++memberships > 20000
                            || members.contains(key))
                        {
                            valid = false;
                            break;
                        }
                        members.add(key);
                    }
                    if (!valid) break;
                    incoming.emplace(name, std::move(members));
                }
                if (!valid || incoming.empty())
                {
                    juce::AlertWindow::showMessageBoxAsync(
                        juce::AlertWindow::WarningIcon, "Liberty - Collections",
                        juce::String::fromUTF8("Sauvegarde XML invalide. Aucune collection modifiée."),
                        "OK");
                    return;
                }

                const auto before = safe->pluginCollections;
                int created = 0, added = 0;
                for (const auto& group : incoming)
                {
                    const auto existing = safe->existingCollectionName(group.first);
                    const auto dest = existing.isEmpty() ? group.first : existing;
                    if (existing.isEmpty())
                    {
                        if (safe->pluginCollections.size() >= 64)
                        {
                            valid = false;
                            break;
                        }
                        safe->pluginCollections.emplace(dest, juce::StringArray());
                        ++created;
                    }
                    auto& members = safe->pluginCollections[dest];
                    for (const auto& key : group.second)
                        if (!members.contains(key))
                        {
                            members.add(key);
                            ++added;
                        }
                }
                int total = 0;
                for (const auto& group : safe->pluginCollections)
                    total += group.second.size();
                if (!valid || total > 20000)
                {
                    safe->pluginCollections = before;
                    juce::AlertWindow::showMessageBoxAsync(
                        juce::AlertWindow::WarningIcon, "Liberty - Collections",
                        juce::String::fromUTF8(
                            "Import annulé : limite de 64 collections ou de 20 000 associations."),
                        "OK");
                    return;
                }
                if (!safe->commitPluginCollections(before,
                    juce::String::fromUTF8("Import : ")
                    + juce::String(created)
                    + juce::String::fromUTF8(" collections créées, ")
                    + juce::String(added)
                    + juce::String::fromUTF8(" associations ajoutées.")))
                    return;
            });
    }

    static juce::File pluginFamilyOverridesFile()
    {
        // Same persistent, user-specific Library directory as plugin favorites.
        // A rescan rewrites JUCE's known-plugin cache, not this user file.
        return favouritesFile().getSiblingFile("PluginFamilyOverrides.xml");
    }

    static bool pluginUsageCompatible(const juce::PluginDescription& description,
                                      PluginUsage usage)
    {
        if (usage == PluginUsage::unclassified) return true;
        return description.isInstrument
            ? (usage >= PluginUsage::synthesizer && usage <= PluginUsage::sampler)
            : (usage >= PluginUsage::reverb && usage <= PluginUsage::utility);
    }

    void loadPluginFamilyOverrides()
    {
        pluginFamilyOverrides.clear();
        const auto file = pluginFamilyOverridesFile();
        if (!file.existsAsFile()) return;
        auto document = juce::XmlDocument::parse(file);
        if (document == nullptr || !document->hasTagName("LibertyPluginFamilies"))
        {
            // Keep the bad source file intact; never silently overwrite it.
            familyOverridesReadOnly = true;
            return;
        }
        for (auto* item = document->getFirstChildElement(); item != nullptr;
             item = item->getNextElement())
        {
            if (!item->hasTagName("plugin")) continue;
            const auto id = item->getStringAttribute("id");
            const int value = item->getIntAttribute("family", 0);
            if (id.isNotEmpty() && value > (int)PluginUsage::all
                && value <= (int)PluginUsage::unclassified)
                pluginFamilyOverrides[id] = (PluginUsage)value;
        }
    }

    bool savePluginFamilyOverrides()
    {
        if (familyOverridesReadOnly) return false;
        juce::XmlElement root("LibertyPluginFamilies");
        root.setAttribute("version", 1);
        for (const auto& entry : pluginFamilyOverrides)
        {
            auto* item = root.createNewChildElement("plugin");
            item->setAttribute("id", entry.first);
            item->setAttribute("family", (int)entry.second);
        }
        const auto target = pluginFamilyOverridesFile();
        juce::TemporaryFile staged(target);
        return staged.getFile().replaceWithText(root.toString())
            && staged.overwriteTargetFileWithTemporary();
    }


    bool isPluginManuallyClassified(const juce::PluginDescription& d) const
    {
        const auto found = pluginFamilyOverrides.find(favouriteKey(d));
        return found != pluginFamilyOverrides.end()
            && pluginUsageCompatible(d, found->second);
    }

    void exportPluginFamilyOverrides()
    {
        if (pluginFamilyOverrides.empty())
        {
            pluginStatus.setText(juce::String::fromUTF8(
                "Aucune catégorie personnalisée à exporter."), juce::dontSendNotification);
            return;
        }
        // Export the current in-memory classifications. Do not assume that
        // users have installed exactly the same AU/VST3 identifiers elsewhere.
        juce::XmlElement root("LibertyPluginFamilies");
        root.setAttribute("version", 1);
        for (const auto& entry : pluginFamilyOverrides)
        {
            auto* item = root.createNewChildElement("plugin");
            item->setAttribute("id", entry.first);
            item->setAttribute("family", (int)entry.second);
        }
        auto chooser = std::make_shared<juce::FileChooser>(
            juce::String::fromUTF8("Exporter les classements Liberty"),
            juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                .getChildFile("Liberty-Classements-Plugins.xml"),
            "*.xml");
        juce::Component::SafePointer<BrowserPanel> safe(this);
        chooser->launchAsync(juce::FileBrowserComponent::saveMode
                             | juce::FileBrowserComponent::canSelectFiles
                             | juce::FileBrowserComponent::warnAboutOverwriting,
            [safe, chooser, xml = root.toString()](const juce::FileChooser& fc)
            {
                if (safe == nullptr || safe->stopped.load()) return;
                auto destination = fc.getResult();
                if (destination == juce::File()) return;
                if (!destination.hasFileExtension("xml"))
                    destination = destination.withFileExtension("xml");
                if (destination.replaceWithText(xml))
                    safe->pluginStatus.setText(juce::String::fromUTF8(
                        "Classements exportés : ") + destination.getFileName(),
                        juce::dontSendNotification);
                else
                    juce::AlertWindow::showMessageBoxAsync(
                        juce::AlertWindow::WarningIcon, "Liberty - Navigateur",
                        juce::String::fromUTF8("Impossible d'exporter les classements."), "OK");
            });
    }

    void importPluginFamilyOverrides()
    {
        if (familyOverridesReadOnly)
        {
            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::WarningIcon, "Liberty - Navigateur",
                juce::String::fromUTF8(
                    "Les classements existants sont illisibles. Corrige d'abord PluginFamilyOverrides.xml."),
                "OK");
            return;
        }
        auto chooser = std::make_shared<juce::FileChooser>(
            juce::String::fromUTF8("Importer des classements Liberty"),
            juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
            "*.xml");
        juce::Component::SafePointer<BrowserPanel> safe(this);
        chooser->launchAsync(juce::FileBrowserComponent::openMode
                             | juce::FileBrowserComponent::canSelectFiles,
            [safe, chooser](const juce::FileChooser& fc)
            {
                if (safe == nullptr || safe->stopped.load()) return;
                const auto source = fc.getResult();
                if (!source.existsAsFile()) return;
                // Protect against unexpectedly huge or malformed external
                // XML files before touching the user's existing categories.
                if (source.getSize() <= 0 || source.getSize() > 5 * 1024 * 1024)
                {
                    juce::AlertWindow::showMessageBoxAsync(
                        juce::AlertWindow::WarningIcon, "Liberty - Navigateur",
                        juce::String::fromUTF8("Fichier XML vide ou trop volumineux (maximum 5 Mo)."),
                        "OK");
                    return;
                }
                auto doc = juce::XmlDocument::parse(source);
                if (doc == nullptr || !doc->hasTagName("LibertyPluginFamilies")
                    || doc->getIntAttribute("version", 1) != 1)
                {
                    juce::AlertWindow::showMessageBoxAsync(
                        juce::AlertWindow::WarningIcon, "Liberty - Navigateur",
                        juce::String::fromUTF8("Format de classements Liberty non reconnu."),
                        "OK");
                    return;
                }
                std::map<juce::String, PluginUsage> imported;
                int elements = 0;
                for (auto* node = doc->getFirstChildElement(); node != nullptr;
                     node = node->getNextElement())
                {
                    ++elements;
                    if (elements > 10000 || !node->hasTagName("plugin"))
                        break;
                    const auto identifier = node->getStringAttribute("id").trim();
                    const int value = node->getIntAttribute("family", 0);
                    if (identifier.isEmpty() || identifier.length() > 2048
                        || value <= (int)PluginUsage::all
                        || value > (int)PluginUsage::unclassified
                        || imported.find(identifier) != imported.end())
                        break;
                    imported.emplace(identifier, (PluginUsage)value);
                }
                if (elements == 0 || elements > 10000 || elements != (int)imported.size())
                {
                    juce::AlertWindow::showMessageBoxAsync(
                        juce::AlertWindow::WarningIcon, "Liberty - Navigateur",
                        juce::String::fromUTF8("Fichier XML invalide : aucun classement n'a été importé."),
                        "OK");
                    return;
                }

                // Only merge entries for installed plugins with compatible
                // instrument/effect kinds; unmatched entries are left out.
                const auto old = safe->pluginFamilyOverrides;
                int applied = 0, skipped = 0;
                for (const auto& entry : imported)
                {
                    const juce::PluginDescription* match = nullptr;
                    for (const auto& description : safe->pluginDescriptions)
                        if (favouriteKey(description) == entry.first)
                        {
                            match = &description;
                            break;
                        }
                    if (match == nullptr || !pluginUsageCompatible(*match, entry.second))
                    {
                        ++skipped;
                        continue;
                    }
                    safe->pluginFamilyOverrides[entry.first] = entry.second;
                    ++applied;
                }
                if (applied == 0)
                {
                    safe->pluginStatus.setText(juce::String::fromUTF8(
                        "Aucun plugin correspondant dans cette installation.")
                        , juce::dontSendNotification);
                    return;
                }
                if (!safe->savePluginFamilyOverrides())
                {
                    safe->pluginFamilyOverrides = old;
                    juce::AlertWindow::showMessageBoxAsync(
                        juce::AlertWindow::WarningIcon, "Liberty - Navigateur",
                        juce::String::fromUTF8("Enregistrement impossible : classements conservés."),
                        "OK");
                    return;
                }
                // Only rebuild when the data has been fully validated and saved.
                safe->rebuildPluginTree();
                safe->pluginStatus.setText(
                    juce::String::fromUTF8("Classements importés : ")
                        + juce::String(applied)
                        + juce::String::fromUTF8(" | Ignorés : ")
                        + juce::String(skipped), juce::dontSendNotification);
            });
    }

    PluginUsage effectivePluginUsage(const juce::PluginDescription& d) const
    {
        const auto key = favouriteKey(d);
        auto found = pluginFamilyOverrides.find(key);
        if (found != pluginFamilyOverrides.end()
            && pluginUsageCompatible(d, found->second))
            return found->second;
        return classifyPluginUsage(d);
    }

    void showPluginFamilyEditor(const juce::PluginDescription& description,
                                juce::Point<int> screenPosition)
    {
        // Identify real AU/VST3 siblings conservatively: exact plugin name,
        // exact manufacturer, same instrument/effect type, distinct JUCE IDs.
        // Never apply corrections to every plugin from a manufacturer.
        const auto formatIsAU = [](const juce::String& value)
        {
            return value.equalsIgnoreCase("AudioUnit")
                || value.equalsIgnoreCase("Audio Unit")
                || value.equalsIgnoreCase("AU");
        };
        const auto formatIsVST3 = [](const juce::String& value)
        {
            return value.equalsIgnoreCase("VST3");
        };
        const auto key = favouriteKey(description);
        juce::StringArray variantKeys;
        bool hasAU = false, hasVST3 = false;
        if (formatIsAU(description.pluginFormatName)
            || formatIsVST3(description.pluginFormatName))
        {
            for (const auto& candidate : pluginDescriptions)
            {
                if (candidate.isInstrument != description.isInstrument
                    || !candidate.name.trim().equalsIgnoreCase(description.name.trim())
                    || !candidate.manufacturerName.trim().equalsIgnoreCase(
                        description.manufacturerName.trim()))
                    continue;
                const bool au = formatIsAU(candidate.pluginFormatName);
                const bool vst3 = formatIsVST3(candidate.pluginFormatName);
                if (!au && !vst3) continue;
                const auto variantKey = favouriteKey(candidate);
                if (variantKey.isEmpty()) continue;
                variantKeys.addIfNotAlreadyThere(variantKey);
                hasAU = hasAU || au;
                hasVST3 = hasVST3 || vst3;
            }
        }
        // Cross-format changes require both types, not just multiple VST3
        // versions with the same title.
        const bool canApplyToVariants = hasAU && hasVST3 && variantKeys.size() >= 2;
        const auto it = pluginFamilyOverrides.find(key);
        const bool custom = it != pluginFamilyOverrides.end();
        const auto effective = effectivePluginUsage(description);

        juce::PopupMenu menu;
        menu.addSectionHeader(description.name);
        menu.addItem(3000,
            isFavouritePlugin(description)
                ? juce::String::fromUTF8("RETIRER DES FAVORIS")
                : juce::String::fromUTF8("AJOUTER AUX FAVORIS"));
        menu.addSeparator();

        juce::PopupMenu families;
        const auto addCategory = [&](juce::PopupMenu& target, int base,
                                     PluginUsage usage, bool selected)
        {
            target.addItem(base + (int)usage, usageLabel(usage), true, selected);
        };
        const auto populate = [&](juce::PopupMenu& target, int base, bool grouped)
        {
            const auto add = [&](PluginUsage usage)
            {
                bool selected = effective == usage;
                if (grouped)
                {
                    selected = true;
                    for (const auto& variantKey : variantKeys)
                    {
                        auto found = pluginFamilyOverrides.find(variantKey);
                        if (found == pluginFamilyOverrides.end() || found->second != usage)
                        {
                            selected = false;
                            break;
                        }
                    }
                }
                addCategory(target, base, usage, selected);
            };
            if (description.isInstrument)
                for (auto usage : instrumentUsages()) add(usage);
            else
                for (auto usage : effectUsages()) add(usage);
            target.addSeparator();
            add(PluginUsage::unclassified);
        };
        populate(families, 3100, false);
        menu.addSubMenu(juce::String::fromUTF8("CLASSER CE PLUGIN"), families);
        menu.addItem(3001, juce::String::fromUTF8("REVENIR AU CLASSEMENT AUTOMATIQUE"), custom);

        juce::StringArray collectionNames;
        juce::PopupMenu memberships;
        for (const auto& group : pluginCollections)
        {
            collectionNames.add(group.first);
            memberships.addItem(6000 + collectionNames.size() - 1, group.first, true,
                group.second.contains(key));
        }
        if (!collectionNames.isEmpty()) memberships.addSeparator();
        memberships.addItem(6100, juce::String::fromUTF8("NOUVELLE COLLECTION..."),
                            !collectionsReadOnly && pluginCollections.size() < 64);
        menu.addSeparator();
        menu.addSubMenu("COLLECTIONS", memberships);

        if (canApplyToVariants)
        {
            menu.addSeparator();
            juce::PopupMenu variants;
            populate(variants, 3300, true);
            menu.addSubMenu(
                juce::String::fromUTF8("CLASSER AU + VST3 (")
                    + juce::String(variantKeys.size()) + ")",
                variants);
            bool customVariant = false;
            for (const auto& variantKey : variantKeys)
                customVariant = customVariant
                    || pluginFamilyOverrides.find(variantKey) != pluginFamilyOverrides.end();
            menu.addItem(3200,
                juce::String::fromUTF8("AUTO POUR TOUTES LES VARIANTES"),
                customVariant);
        }

        juce::Component::SafePointer<BrowserPanel> safe(this);
        menu.showMenuAsync(juce::PopupMenu::Options()
            .withTargetScreenArea(juce::Rectangle<int>(screenPosition.x, screenPosition.y, 1, 1))
            .withMaximumNumColumns(1),
            [safe, description, variantKeys, canApplyToVariants, collectionNames](int chosen)
            {
                if (safe == nullptr || safe->stopped.load() || chosen == 0) return;
                if (chosen == 6100)
                {
                    safe->promptCollectionName({}, favouriteKey(description));
                    return;
                }
                if (chosen >= 6000 && chosen < 6000 + collectionNames.size())
                {
                    safe->togglePluginCollection(description, collectionNames[chosen - 6000]);
                    return;
                }
                if (chosen == 3000)
                {
                    safe->toggleFavourite(description);
                    return;
                }
                const bool groupAction = chosen == 3200
                    || (chosen > 3300 && chosen <= 3300 + (int)PluginUsage::unclassified);
                const bool singleAction = chosen == 3001
                    || (chosen > 3100 && chosen <= 3100 + (int)PluginUsage::unclassified);
                if ((!groupAction && !singleAction)
                    || (groupAction && !canApplyToVariants)) return;
                const bool reset = chosen == 3001 || chosen == 3200;
                const auto usage = (PluginUsage)(chosen - (groupAction ? 3300 : 3100));
                if (!reset && !pluginUsageCompatible(description, usage)) return;
                const auto key = favouriteKey(description);
                if (key.isEmpty()) return;
                const auto before = safe->pluginFamilyOverrides;
                auto keysToModify = groupAction ? variantKeys : juce::StringArray(key);
                for (const auto& target : keysToModify)
                {
                    if (reset) safe->pluginFamilyOverrides.erase(target);
                    else safe->pluginFamilyOverrides[target] = usage;
                }
                if (!safe->savePluginFamilyOverrides())
                {
                    safe->pluginFamilyOverrides = before;
                    juce::AlertWindow::showMessageBoxAsync(
                        juce::AlertWindow::WarningIcon, "Liberty - Navigateur",
                        juce::String::fromUTF8(
                            "Enregistrement des familles impossible. Vérifie le fichier PluginFamilyOverrides.xml."),
                        "OK");
                    return;
                }

                // Defer the TreeView rebuild beyond the popup callback. All
                // changes are persisted atomically in one XML file write.
                const auto resultText = groupAction
                    ? description.name + juce::String::fromUTF8(" : ")
                        + juce::String(keysToModify.size())
                        + juce::String::fromUTF8(" variantes AU/VST3 mises à jour.")
                    : description.name + juce::String::fromUTF8(" : famille enregistrée.");
                juce::MessageManager::callAsync([safe, resultText]
                {
                    if (safe == nullptr || safe->stopped.load()) return;
                    safe->rebuildPluginTree();
                    safe->pluginStatus.setText(resultText, juce::dontSendNotification);
                });
            });
    }

    static juce::File favouritesFile()
    {
        auto folder = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                          .getChildFile("BSM").getChildFile("Liberty");
        folder.createDirectory();
        return folder.getChildFile("PluginFavorites.txt");
    }

    static juce::String favouriteKey(const juce::PluginDescription& d)
    {
        auto id = d.createIdentifierString();
        return id.isNotEmpty() ? id : d.pluginFormatName + "|" + d.manufacturerName + "|" + d.name;
    }

    void loadFavourites()
    {
        favouriteKeys.clear();
        if (auto file = favouritesFile(); file.existsAsFile()) file.readLines(favouriteKeys);
        favouriteKeys.trim(); favouriteKeys.removeEmptyStrings(); favouriteKeys.removeDuplicates(false);
    }

    void saveFavourites() { favouritesFile().replaceWithText(favouriteKeys.joinIntoString("\n")); }
    bool isFavouritePlugin(const juce::PluginDescription& d) const { return favouriteKeys.contains(favouriteKey(d)); }

    static juce::File recentPluginsFile()
    {
        return favouritesFile().getSiblingFile("PluginRecent.txt");
    }

    void loadRecentPlugins()
    {
        recentKeys.clear();
        const auto file = recentPluginsFile();
        if (!file.existsAsFile()) return;
        for (const auto& raw : juce::StringArray::fromLines(file.loadFileAsString()))
        {
            const auto key = raw.trim();
            if (key.isEmpty() || recentKeys.contains(key)) continue;
            recentKeys.add(key);
            if (recentKeys.size() >= 20) break;
        }
    }

    void rememberRecentPlugin(const juce::PluginDescription& d)
    {
        // Call only on confirmed successful loads, never on failed attempts,
        // scanning or artwork capture. Store the exact AU/VST3 identifier.
        const auto key = favouriteKey(d);
        if (key.isEmpty()) return;
        for (int i = recentKeys.size() - 1; i >= 0; --i)
            if (recentKeys[i] == key) recentKeys.remove(i);
        recentKeys.insert(0, key);
        while (recentKeys.size() > 20) recentKeys.remove(recentKeys.size() - 1);
        recentPluginsFile().replaceWithText(recentKeys.joinIntoString("\n"));

        // The load can originate from TreeViewItem::itemDoubleClicked.
        // Rebuild later, never destroy that tree item during its callback.
        juce::Component::SafePointer<BrowserPanel> safe(this);
        juce::MessageManager::callAsync([safe]
        {
            if (safe != nullptr && !safe->stopped.load())
                safe->rebuildPluginTree();
        });
    }

    void toggleFavourite(const juce::PluginDescription& d)
    {
        const auto key = favouriteKey(d);
        const int index = favouriteKeys.indexOf(key);
        if (index >= 0) favouriteKeys.remove(index); else favouriteKeys.add(key);
        saveFavourites();
        // The favourite star can be toggled from TreeViewItem::itemClicked.
        // Defer reconstruction so JUCE finishes dispatching that mouse event
        // before the originating item is destroyed.
        juce::Component::SafePointer<BrowserPanel> safe(this);
        juce::MessageManager::callAsync([safe]
        {
            if (safe != nullptr && !safe->stopped.load()) safe->rebuildPluginTree();
        });
        pluginStatus.setText(index >= 0 ? "Retiré des favoris" : "Ajouté aux favoris", juce::dontSendNotification);
    }

    void toggleSelectedFavourite() { if (const auto* d = selectedPluginDescription()) toggleFavourite(*d); }

    void selectionChanged() override {}
    void fileClicked(const juce::File& file, const juce::MouseEvent&) override
    {
        if (category != Category::audio || !file.existsAsFile()) return;
        updateSamplePackInfo(file);
        previewTransport.stop();
        previewTransport.setSource(nullptr);
        previewReader.reset();
        previewThumbnail.clear();
        previewInfo.setText("Aucun sample sélectionné", juce::dontSendNotification);
        auto reader = std::unique_ptr<juce::AudioFormatReader>(previewFormats.createReaderFor(file));
        if (reader == nullptr) return;
        const double rate = reader->sampleRate;
        const double seconds = rate > 0.0 ? (double)reader->lengthInSamples / rate : 0.0;
        const int totalSeconds = juce::jmax(0, (int)seconds);
        previewThumbnail.setSource(new juce::FileInputSource(file));
        previewInfo.setText(file.getFileNameWithoutExtension() + "  |  " + file.getFileExtension().trimCharactersAtStart(".").toUpperCase() + "  |  " + juce::String(totalSeconds / 60)
                            + ":" + juce::String(totalSeconds % 60).paddedLeft('0', 2),
                            juce::dontSendNotification);
        previewInfo.setTooltip(file.getFullPathName() + "  |  " + juce::String(seconds, 2) + " s");
        previewReader = std::make_unique<juce::AudioFormatReaderSource>(reader.release(), true);
        previewTransport.setSource(previewReader.get(), 0, nullptr, rate);
        previewTransport.setGain((float)previewVolume.getValue());
        if (previewAuto.getToggleState()) previewTransport.start();
    }
    void fileDoubleClicked(const juce::File& file) override { openFileOrDirectory(file); }
    void browserRootChanged(const juce::File&) override {}

    void setBrowserOpen(bool shouldOpen)
    {
        if(!shouldOpen)
        {
            previewTransport.stop();
            targetEffectSlot=-1;
            targetEffectLane=-1;
        }
        browserOpen = shouldOpen;
        setVisible(browserOpen);
        if (browserOpen) toFront(false);
        adjustHostWindow(browserOpen);
        // The mixer claims only the left viewport, so the right plugin
        // browser never covers channel strips or floating fader controls.
        resizeLibertyMixConsole(&owner);
        owner.repaint();
    }

    void adjustHostWindow(bool opening)
    {
        auto* window = owner.findParentComponentOfClass<juce::DocumentWindow>();
        if (window == nullptr) return;
        auto bounds = window->getBounds();
        const int width = getLibertyPreferredBrowserWidth(&owner);
        if (opening)
        {
            if (hostExpanded) return;
            originalWindowBounds = bounds;
            auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect(bounds);
            if (display != nullptr)
            {
                const auto usable = display->userArea;
                int wantedRight = bounds.getRight() + width;
                int newX = bounds.getX();
                if (wantedRight > usable.getRight()) newX = juce::jmax(usable.getX(), bounds.getX() - (wantedRight - usable.getRight()));
                window->setBounds(newX, bounds.getY(), juce::jmin(usable.getWidth(), bounds.getWidth() + width), bounds.getHeight());
            }
            else window->setSize(bounds.getWidth() + width, bounds.getHeight());
            hostExpanded = true;
        }
        else if (hostExpanded)
        {
            if (!originalWindowBounds.isEmpty()) window->setBounds(originalWindowBounds);
            hostExpanded = false;
        }
    }

    static juce::File sampleFolderFavouritesFile()
    {
        return favouritesFile().getSiblingFile("SampleFolderFavourites.txt");
    }

    static juce::StringArray sampleFolderFavourites()
    {
        juce::StringArray paths;
        sampleFolderFavouritesFile().readLines(paths);
        paths.trim(); paths.removeEmptyStrings(); paths.removeDuplicates(false);
        return paths;
    }

    static juce::File sampleFavouriteFile()
    {
        return libraryPreferenceFile().getSiblingFile("SampleFileFavourites.txt");
    }

    void toggleSampleFavourite(const juce::File& file)
    {
        juce::StringArray favourites;
        sampleFavouriteFile().readLines(favourites);
        const auto path = file.getFullPathName();
        if (favourites.contains(path)) favourites.removeString(path);
        else favourites.add(path);
        favourites.removeDuplicates(false);
        sampleFavouriteFile().replaceWithText(favourites.joinIntoString("\n"));
    }

    static juce::File sampleFilterPreferenceFile()
    {
        return libraryPreferenceFile().getSiblingFile("SampleSearchPreferences.txt");
    }

    static juce::StringArray sampleFilterNames()
    {
        return { "Kicks", "Snares", "Hi-Hats", "Loops", "FX" };
    }

    static int sampleCategoryMask(const juce::File& file)
    {
        const auto name = (file.getFileNameWithoutExtension() + " " + file.getParentDirectory().getFileName()).toLowerCase();
        const juce::StringArray tokens = juce::StringArray::fromTokens(name.replaceCharacters("-_.", "   "), " ", "");
        auto has = [&tokens](const juce::StringArray& words)
        {
            for (const auto& word : words)
                if (tokens.contains(word) || (word.length() > 3 && tokens.joinIntoString(" ").contains(word))) return true;
            return false;
        };
        int mask = 0;
        if (has({ "kick", "kicks", "bassdrum", "bd" })) mask |= 1;
        if (has({ "snare", "snares", "snr" })) mask |= 2;
        if (has({ "hihat", "hihats", "hat", "hats", "hh" })) mask |= 4;
        if (has({ "loop", "loops", "groove", "grooves" })) mask |= 8;
        if (has({ "fx", "sfx", "effect", "effects", "impact", "riser" })) mask |= 16;
        return mask;
    }

    void saveSampleFilterPreferences()
    {
        sampleFilterPreferenceFile().replaceWithText(juce::String(sampleFilterMask) + "\n" + sampleNameQuery);
    }

    void showMusicalFilters()
    {
        juce::PopupMenu menu;
        menu.addItem(1, "TOUTES LES CATEGORIES", true, sampleFilterMask == 0);
        menu.addSeparator();
        const auto names = sampleFilterNames();
        for (int i = 0; i < names.size(); ++i)
            menu.addItem(i + 2, names[i], true, (sampleFilterMask & (1 << i)) != 0);
        menu.addSeparator();
        menu.addItem(8, "RECHERCHER", !sampleSearchRunning.load());
        juce::Component::SafePointer<BrowserPanel> safe(this);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&musicFilterButton), [safe](int result)
        {
            if (safe == nullptr || result == 0) return;
            if (result == 1) safe->sampleFilterMask = 0;
            else if (result >= 2 && result <= 6) safe->sampleFilterMask ^= (1 << (result - 2));
            if (result != 8) safe->saveSampleFilterPreferences();
            if (safe->sampleSearchRunning.load())
            {
                safe->sampleSearchPending = true;
                safe->sampleSearchChangedAt = juce::Time::getMillisecondCounter();
            }
            else safe->showMusicalFilterResults();
        });
    }

    void showMusicalFilterResults()
    {
        const auto library = findInstalledSampleLibrary();
        if (!library.isDirectory())
        {
            samplePackInfo.setText("Aucune bibliotheque de samples disponible.", juce::dontSendNotification);
            return;
        }
        if (sampleSearchRunning.load())
        {
            sampleSearchPending = true;
            sampleSearchChangedAt = juce::Time::getMillisecondCounter();
            return;
        }
        if (sampleSearchThread.joinable()) sampleSearchThread.join();
        sampleSearchRunning.store(true);
        sampleSearchPending = false;
        musicFilterButton.setEnabled(false);
        samplePackInfo.setText("Recherche de samples en cours...", juce::dontSendNotification);
        const int mask = sampleFilterMask;
        const auto query = sampleNameQuery.trim();
        const bool reuseIndex = sampleIndexLibrary == library && sampleIndexComplete;
        const auto cachedIndex = reuseIndex ? sampleIndex : juce::Array<juce::File>();
        juce::Component::SafePointer<BrowserPanel> safe(this);
        sampleSearchThread = std::thread([this, safe, library, mask, query, reuseIndex, cachedIndex]
        {
            juce::Array<juce::File> matches;
            juce::Array<juce::File> indexedFiles = reuseIndex ? cachedIndex : juce::Array<juce::File>();
            int inspected = 0;
            bool reachedLimit = false;
            int counts[5] = { 0, 0, 0, 0, 0 };
            if (!reuseIndex)
            {
                juce::RangedDirectoryIterator iterator(library, true, "*", juce::File::findFiles);
                for (const auto& entry : iterator)
                {
                    if (stopped.load()) return;
                    if (++inspected > 12000) { reachedLimit = true; break; }
                    const auto file = entry.getFile();
                    if (file.hasFileExtension("wav;aif;aiff;mp3;flac")) indexedFiles.add(file);
                }
            }
            else inspected = indexedFiles.size();
            for (const auto& file : indexedFiles)
            {
                if (stopped.load()) return;
                const int categories = sampleCategoryMask(file);
                for (int i = 0; i < 5; ++i) if ((categories & (1 << i)) != 0) ++counts[i];
                if (mask != 0 && (categories & mask) == 0) continue;
                if (query.isNotEmpty() && !file.getFileName().containsIgnoreCase(query)) continue;
                if (matches.size() < 80) matches.add(file);
            }
            if (stopped.load()) return;
            juce::StringArray details;
            juce::AudioFormatManager metadataFormats;
            metadataFormats.registerBasicFormats();
            for (const auto& file : matches)
            {
                juce::String duration = "--:--";
                std::unique_ptr<juce::AudioFormatReader> reader(metadataFormats.createReaderFor(file));
                if (reader != nullptr && reader->sampleRate > 0.0)
                {
                    const int seconds = (int)(reader->lengthInSamples / reader->sampleRate);
                    duration = juce::String(seconds / 60) + ":" + juce::String(seconds % 60).paddedLeft('0', 2);
                }
                details.add(file.getFileExtension().toUpperCase().trimCharactersAtStart(".") + "  |  " + duration);
            }
            const juce::StringArray labels = sampleFilterNames();
            juce::String summary = juce::String(matches.size()) + " resultats / " + juce::String(inspected) + " fichiers | ";
            for (int i = 0; i < 5; ++i)
                summary += labels[i] + ": " + juce::String(counts[i]) + (i == 4 ? "" : "  ");
            juce::MessageManager::callAsync([safe, matches, details, summary, mask, query, library, indexedFiles, reuseIndex, reachedLimit]
            {
                if (safe == nullptr || safe->stopped.load()) return;
                safe->sampleSearchRunning.store(false);
                safe->musicFilterButton.setEnabled(true);
                if (!reuseIndex)
                {
                    safe->sampleIndex = indexedFiles;
                    safe->sampleIndexLibrary = library;
                    safe->sampleIndexComplete = !reachedLimit;
                }
                if (safe->category != Category::audio) return;
                if (safe->sampleSearchPending || safe->sampleFilterMask != mask || safe->sampleNameQuery.trim() != query)
                {
                    safe->sampleSearchPending = true;
                    safe->sampleSearchChangedAt = juce::Time::getMillisecondCounter();
                    return;
                }
                safe->samplePackInfo.setText(summary, juce::dontSendNotification);
                safe->samplePackInfo.setTooltip(summary + " (12 000 fichiers maximum, 80 resultats affiches)");
                juce::StringArray favourites;
                sampleFavouriteFile().readLines(favourites);
                juce::PopupMenu results;
                for (int i = 0; i < matches.size(); ++i)
                {
                    const auto& file = matches.getReference(i);
                    const bool favourite = favourites.contains(file.getFullPathName());
                    results.addItem(i + 1, (favourite ? "[FAV]  " : "") + file.getFileNameWithoutExtension()
                        + "  |  " + details[i] + "  |  " + file.getParentDirectory().getFileName());
                }
                if (matches.isEmpty()) results.addItem(1, "Aucun resultat", false);
                if (!matches.isEmpty())
                {
                    juce::PopupMenu favouriteMenu;
                    for (int i = 0; i < matches.size(); ++i)
                        favouriteMenu.addItem(1001 + i, (favourites.contains(matches.getReference(i).getFullPathName()) ? "RETIRER  " : "AJOUTER  ")
                            + matches.getReference(i).getFileName());
                    results.addSeparator();
                    results.addSubMenu("GERER LES FAVORIS", favouriteMenu);
                }
                results.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&safe->musicFilterButton),
                    [safe, matches](int result)
                    {
                        if (safe == nullptr || result < 1) return;
                        if (result >= 1001 && result < 1001 + matches.size())
                        {
                            safe->toggleSampleFavourite(matches.getReference(result - 1001));
                            safe->samplePackInfo.setTooltip("Favoris de samples mis a jour");
                            return;
                        }
                        if (result > matches.size()) return;
                        const auto file = matches.getReference(result - 1);
                        if (!file.existsAsFile()) return;
                        if (safe->category != Category::audio) return;
                        safe->setRoot(file.getParentDirectory());
                        safe->fileTree.setSelectedFile(file);
                        safe->updateSamplePackInfo(file);
                    });
            });
        });
    }

    void showInstalledPacks()
    {
        const auto library = findInstalledSampleLibrary();
        if (!library.isDirectory()) return;
        juce::PopupMenu menu;
        juce::Array<juce::File> packFolders;
        juce::Array<juce::File> manufacturers;
        library.findChildFiles(manufacturers, juce::File::findDirectories, false);
        std::sort(manufacturers.begin(), manufacturers.end(), [](const auto& a, const auto& b)
        {
            return a.getFileName().compareNatural(b.getFileName(), false) < 0;
        });
        for (const auto& manufacturer : manufacturers)
        {
            juce::Array<juce::File> packs;
            manufacturer.findChildFiles(packs, juce::File::findDirectories, false);
            std::sort(packs.begin(), packs.end(), [](const auto& a, const auto& b)
            {
                return a.getFileName().compareNatural(b.getFileName(), false) < 0;
            });
            juce::PopupMenu submenu;
            for (const auto& pack : packs)
            {
                juce::PopupMenu packMenu;
                packFolders.add(pack);
                packMenu.addItem(packFolders.size(), "OUVRIR LE PACK");
                juce::Array<juce::File> categories;
                pack.findChildFiles(categories, juce::File::findDirectories, false);
                std::sort(categories.begin(), categories.end(), [](const auto& a, const auto& b)
                {
                    return a.getFileName().compareNatural(b.getFileName(), false) < 0;
                });
                if (!categories.isEmpty()) packMenu.addSeparator();
                for (const auto& folder : categories)
                {
                    packFolders.add(folder);
                    packMenu.addItem(packFolders.size(), folder.getFileName());
                }
                submenu.addSubMenu(pack.getFileName(), packMenu);
            }
            if (!packs.isEmpty()) menu.addSubMenu(manufacturer.getFileName(), submenu);
        }
        if (packFolders.isEmpty())
        {
            menu.addItem(1, "Aucun pack trouvé dans la bibliothèque", false);
        }
        juce::Component::SafePointer<BrowserPanel> safe(this);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&browsePacksButton),
            [safe, packFolders](int result)
            {
                if (safe == nullptr || result < 1 || result > packFolders.size()) return;
                if (packFolders[result - 1].isDirectory()) safe->setRoot(packFolders[result - 1]);
            });
    }

    void updateSamplePackInfo(const juce::File& selected = {})
    {
        const auto library = findInstalledSampleLibrary();
        const auto location = selected.existsAsFile() ? selected.getParentDirectory() : rootDirectory;
        juce::String manufacturer = "Non renseigné";
        juce::String pack = location.getFileName();
        juce::String categoryName = "Autres";
        if (library.isDirectory() && (location == library || location.isAChildOf(library)))
        {
            const auto relative = location.getRelativePathFrom(library).replaceCharacter('\\', '/');
            const auto parts = juce::StringArray::fromTokens(relative, "/", "");
            if (parts.size() >= 1 && location != library) manufacturer = parts[0];
            if (parts.size() >= 2) pack = parts[1];
            if (parts.size() >= 3) categoryName = parts[2];
            if (location == library) pack = "Racine de la bibliothèque";
        }
        const auto summary = "FABRICANT : " + manufacturer + "  |  PACK : " + pack
                           + "  |  CATÉGORIE : " + categoryName;
        samplePackInfo.setText(summary, juce::dontSendNotification);
        samplePackInfo.setTooltip(summary + "\nDossier : " + location.getFullPathName());
    }

    void updateSampleFolderControls()
    {
        const auto path = rootDirectory.getFullPathName();
        const bool saved = sampleFolderFavourites().contains(path);
        saveFolderButton.setButtonText(saved ? "RETIRER FAVORI" : "AJOUTER FAVORI");
        saveFolderButton.setTooltip(path);
        openFavouriteButton.setTooltip("Dossiers favoris de samples");
        parentFolderButton.setTooltip(path);
    }

    static juce::File libraryPreferenceFile()
    {
        return favouritesFile().getSiblingFile("SampleLibraryPath.txt");
    }

    void chooseSampleLibrary()
    {
        auto chooser = std::make_shared<juce::FileChooser>("Choisir le dossier de samples", rootDirectory);
        juce::Component::SafePointer<BrowserPanel> safe(this);
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
            [safe, chooser](const juce::FileChooser& selected)
            {
                if (safe == nullptr) return;
                const auto folder = selected.getResult();
                if (!folder.isDirectory()) return;
                if (libraryPreferenceFile().replaceWithText(folder.getFullPathName()))
                {
                    safe->sampleIndex.clear();
                    safe->sampleIndexComplete = false;
                    safe->sampleIndexLibrary = juce::File();
                    safe->setRoot(folder);
                }
            });
    }

    void refreshOfficialArtwork()
    {
        if (batchArtworkRunning.load()) return;
        // Opt-in catalogue: each entry maps the exact JUCE plug-in identifier hash
        // to an explicit HTTPS image URL. No guessed product URLs or web scraping.
        const auto folder = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("Liberty").getChildFile("PluginArtworkCatalogue");
        folder.createDirectory();
        const auto manifest = folder.getChildFile("OfficialArtwork.tsv");
        if (!manifest.existsAsFile())
        {
            manifest.replaceWithText("# identifier-hash\\tHTTPS-image-URL\\n");
            pluginStatus.setText("Catalogue cree : renseigner les URLs officielles autorisees.", juce::dontSendNotification);
            return;
        }
        if (artworkRefreshing.exchange(true)) return;
        if (artworkThread.joinable()) artworkThread.join();
        const auto lines = juce::StringArray::fromLines(manifest.loadFileAsString());
        juce::Component::SafePointer<BrowserPanel> safe(this);
        pluginStatus.setText("Actualisation des images officielles...", juce::dontSendNotification);
        artworkThread = std::thread([this, safe, folder, lines]
        {
            int saved = 0;
            int failed = 0;
            for (const auto& line : lines)
            {
                if (stopped.load()) break;
                if (line.startsWithChar('#') || line.isEmpty()) continue;
                const auto hash = line.upToFirstOccurrenceOf("\\t", false, false).trim();
                const auto address = line.fromFirstOccurrenceOf("\\t", false, false).trim();
                if (hash.isEmpty() || !hash.containsOnly("0123456789abcdefABCDEF")
                    || !address.startsWithIgnoreCase("https://")) { ++failed; continue; }
                const auto destination = folder.getChildFile(hash + ".png");
                if (destination.existsAsFile()) continue;
                const auto staged = folder.getNonexistentChildFile("artwork-download", ".png", false);
                juce::URL url(address);
                const auto options = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                    .withConnectionTimeoutMs(4000).withNumRedirectsToFollow(2);
                auto stream = url.createInputStream(options);
                if (stream == nullptr) { ++failed; continue; }
                juce::MemoryBlock bytes;
                char buffer[8192];
                while (!stopped.load() && bytes.getSize() <= 5 * 1024 * 1024)
                {
                    const int n = stream->read(buffer, (int)sizeof(buffer));
                    if (n <= 0) break;
                    bytes.append(buffer, (size_t)n);
                }
                if (stopped.load()) break;
                if (bytes.getSize() > 5 * 1024 * 1024 || bytes.isEmpty()) { ++failed; continue; }
                const auto image = juce::ImageFileFormat::loadFrom(bytes.getData(), bytes.getSize());
                if (!image.isValid() || image.getWidth() > 4096 || image.getHeight() > 4096)
                { ++failed; continue; }
                if (staged.replaceWithData(bytes.getData(), bytes.getSize()) && staged.replaceFileIn(destination))
                    ++saved;
                else { staged.deleteFile(); ++failed; }
            }
            juce::MessageManager::callAsync([safe, saved, failed]
            {
                if (safe == nullptr || safe->stopped.load()) return;
                safe->artworkRefreshing.store(false);
                safe->rebuildPluginTree();
                safe->pluginStatus.setText(juce::String(saved) + " images installees, " + juce::String(failed)
                    + " echecs.", juce::dontSendNotification);
            });
        });
    }


    // Explicit opt-in bulk capture. Each AU/VST3 is loaded in the existing
    // disposable child process; no third-party processor enters the DAW itself.
    // Queue is a snapshot, and only the JUCE message thread touches the UI.

    void showArtworkMenu()
    {
        juce::Component::SafePointer<BrowserPanel> safe(this);
        const auto runAction = [safe](int choice)
        {
            if (safe == nullptr || safe->stopped.load()) return;
            if (choice == 1) safe->importSelectedThumbnail();
            if (choice == 2) safe->refreshOfficialArtwork();
            if (choice == 5) safe->captureIsolatedPluginArtwork();
            if (choice == 12) safe->startBatchArtworkCapture(1, false);
            if (choice == 6) safe->startBatchArtworkCapture(10, false);
            if (choice == 7) safe->startBatchArtworkCapture(50, false);
            if (choice == 8) safe->startBatchArtworkCapture(0, false);
            if (choice == 9) safe->cancelBatchArtworkCapture();
            if (choice == 13) safe->startBatchArtworkCapture(1, true);
            if (choice == 10) safe->startBatchArtworkCapture(10, true);
            if (choice == 14) safe->startBatchArtworkCapture(100, true);
            if (choice == 20) safe->setArtworkView(ArtworkView::all);
            if (choice == 21) safe->setArtworkView(ArtworkView::withImage);
            if (choice == 22) safe->setArtworkView(ArtworkView::withoutImage);
            if (choice == 23) safe->setArtworkView(ArtworkView::failed);
            if (choice == 11) safe->showBatchArtworkLog();
            if (choice == 3)
            {
                const auto folder = juce::File::getSpecialLocation(
                    juce::File::userApplicationDataDirectory)
                    .getChildFile("Liberty").getChildFile("PluginArtworkCatalogue");
                folder.createDirectory();
                folder.revealToUser();
            }
        };
        auto panel = std::make_unique<ArtworkMenuPanel>(
            batchArtworkRunning.load(), artworkPresentCount, artworkTotalCount,
            (int) artworkView, std::move(runAction));

        // Anchor the compact card dashboard to the existing IMPORTER IMAGE
        // button. JUCE manages popup placement and Escape/outside-click close.
        auto& popup = juce::CallOutBox::launchAsynchronously(
            std::move(panel), importThumbnailButton.getScreenBounds(), nullptr);
        popup.setArrowSize(10.0f);
        popup.setDismissalMouseClicksAreAlwaysConsumed(true);
    }

    void showBatchArtworkLog()
    {
        const auto folder = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("Liberty");
        const auto report = folder.getChildFile("PluginArtworkBatchFailures.tsv");
        const auto captureLog = folder.getChildFile("PluginArtworkCapture.log");
        if (report.existsAsFile()) report.revealToUser();
        else if (captureLog.existsAsFile()) captureLog.revealToUser();
        else pluginStatus.setText(juce::String::fromUTF8("Aucun journal de capture enregistré."),
                                  juce::dontSendNotification);
    }

    void cancelBatchArtworkCapture()
    {
        if (!batchArtworkRunning.load()) return;
        batchArtworkCancel.store(true);
        pluginStatus.setText(juce::String::fromUTF8("Arrêt demandé : fermeture du processus de capture en cours..."),
                             juce::dontSendNotification);
    }

    void startBatchArtworkCapture(int maximum, bool retryFailuresOnly)
    {
       #if ! JUCE_MAC
        pluginStatus.setText(juce::String::fromUTF8("Capture automatique disponible sur macOS uniquement."),
                             juce::dontSendNotification);
        return;
       #else
        // Do not block capture on CGPreflightScreenCaptureAccess().
        // macOS TCC may report a stale false value immediately after the user
        // grants permission or an updated app binary is installed. The actual
        // ScreenCaptureKit call in the isolated worker is the definitive test.
        // Its diagnostic will be included in the per-plugin failure report.
        // Never invoke CGRequestScreenCaptureAccess() repeatedly from a batch.
        if (batchArtworkRunning.load() || captureRunning.load()
            || scanning.load() || artworkRefreshing.load())
        {
            pluginStatus.setText(juce::String::fromUTF8("Une capture, un scan ou un téléchargement est déjà en cours."),
                                 juce::dontSendNotification);
            return;
        }
        if (batchArtworkThread.joinable()) batchArtworkThread.join();

        const auto folder = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("Liberty").getChildFile("PluginThumbnails");
        if (!folder.createDirectory())
        {
            pluginStatus.setText(juce::String::fromUTF8("Dossier des miniatures inaccessible."),
                                 juce::dontSendNotification);
            return;
        }
        const auto failuresFile = folder.getSiblingFile("PluginArtworkBatchFailures.tsv");
        juce::StringArray failedHashes;
        if (failuresFile.existsAsFile())
            for (const auto& line : juce::StringArray::fromLines(failuresFile.loadFileAsString()))
            {
                const auto hash = line.upToFirstOccurrenceOf("\t", false, false).trim();
                if (hash.isNotEmpty()) failedHashes.addIfNotAlreadyThere(hash);
            }

        juce::Array<juce::PluginDescription> queue;
        juce::StringArray visited;
        const auto catalogue = folder.getSiblingFile("PluginArtworkCatalogue");
        for (const auto& description : pluginDescriptions)
        {
            const auto identifier = description.createIdentifierString();
            if (identifier.isEmpty() || visited.contains(identifier)) continue;
            visited.add(identifier);
            const auto hash = juce::String::toHexString((juce::int64) identifier.hashCode64());
            if (folder.getChildFile(hash + ".png").existsAsFile()
                || catalogue.getChildFile(hash + ".png").existsAsFile()) continue;
            if (retryFailuresOnly != failedHashes.contains(hash)) continue;
            queue.add(description);
            if (maximum > 0 && queue.size() >= maximum) break;
        }
        if (queue.isEmpty())
        {
            pluginStatus.setText(juce::String::fromUTF8(
                retryFailuresOnly ? "Aucune capture en échec à réessayer."
                                  : "Toutes les captures disponibles sont déjà présentes ou répertoriées en échec."),
                juce::dontSendNotification);
            return;
        }

        batchArtworkCancel.store(false);
        batchArtworkRunning.store(true);
        batchArtworkStatusUntilMs = 0;
        pluginStatus.setText(juce::String::fromUTF8("Capture automatique : 0 / ")
            + juce::String(queue.size()) + juce::String::fromUTF8(" — ne pas quitter Liberty."),
            juce::dontSendNotification);

        juce::Component::SafePointer<BrowserPanel> safe(this);
        batchArtworkThread = std::thread([this, safe, folder, failuresFile, queue]
        {
            int completed = 0, succeeded = 0, failed = 0;
            const int total = queue.size();
            const auto executable = juce::File::getSpecialLocation(juce::File::currentExecutableFile);

            for (const auto& description : queue)
            {
                if (stopped.load() || batchArtworkCancel.load()) break;
                const auto identifier = description.createIdentifierString();
                const auto hash = juce::String::toHexString((juce::int64) identifier.hashCode64());
                const auto destination = folder.getChildFile(hash + ".png");
                if (destination.existsAsFile()) { ++completed; ++succeeded; continue; }

                const juce::String pending = juce::String::fromUTF8("Capture en cours (")
                    + juce::String(completed + 1) + " / " + juce::String(total) + ") : "
                    + description.name;
                juce::MessageManager::callAsync([safe, pending]
                {
                    if (safe != nullptr && !safe->stopped.load())
                        safe->pluginStatus.setText(pending, juce::dontSendNotification);
                });

                auto input = juce::File::createTempFile(".xml");
                auto output = juce::File::createTempFile(".png");
                bool success = false;
                bool processStarted = false;
                juce::String reason = "Capture impossible";
                const auto xml = description.createXml();
                if (xml != nullptr && input.replaceWithText(xml->toString()))
                {
                    juce::ChildProcess worker;
                    const bool started = worker.start(juce::StringArray {
                        executable.getFullPathName(), "--liberty-artwork-worker",
                        input.getFullPathName(), output.getFullPathName() });
                    if (started)
                    {
                        processStarted = true;
                        int elapsedMs = 0;
                        while (worker.isRunning() && elapsedMs < 30000
                               && !stopped.load() && !batchArtworkCancel.load())
                        {
                            worker.waitForProcessToFinish(200);
                            elapsedMs += 200;
                        }
                        const bool finished = !worker.isRunning();
                        const int code = finished ? worker.getExitCode() : -1;
                        if (!finished)
                        {
                            worker.kill();
                            worker.waitForProcessToFinish(1000);
                        }
                        if (!finished)
                        {
                            reason = "Capture interrompue ou délai de 30 secondes dépassé";
                        }
                        else if (code != 0)
                        {
                            reason = "Le processus de capture a échoué (code " + juce::String(code) + ").";
                        }
                        else if (!output.existsAsFile())
                        {
                            reason = "Processus terminé sans produire de fichier PNG.";
                        }
                        else if (libertyValidArtworkPNG(output, reason))
                        {
                            if (output.replaceFileIn(destination))
                                success = true;
                            else
                                reason = "La capture PNG est valide, mais son enregistrement a échoué.";
                        }
                    }
                    else reason = "Démarrage du processus isolé impossible";
                }
                else reason = "Description XML du plugin invalide";
                input.deleteFile();
                output.deleteFile();

                // Read the isolated worker's own explanation before the next
                // process truncates its log. Do not just show an opaque exit code.
                if (!success && processStarted)
                {
                    const auto captureLog = folder.getSiblingFile("PluginArtworkCapture.log");
                    if (captureLog.existsAsFile())
                    {
                        const auto details = juce::StringArray::fromLines(captureLog.loadFileAsString());
                        for (int i = details.size() - 1; i >= 0; --i)
                        {
                            const auto line = details[i].trim();
                            if (line.startsWith("ECHEC : "))
                            {
                                reason = line.fromFirstOccurrenceOf("ECHEC : ", false, false);
                                break;
                            }
                        }
                    }
                }
                if (stopped.load() || batchArtworkCancel.load()) break;
                ++completed;
                if (success) ++succeeded;
                else
                {
                    ++failed;
                    auto name = description.name.replaceCharacters("\t\r\n", "   ");
                    auto detail = reason.replaceCharacters("\t\r\n", "   ");
                    failuresFile.appendText(hash + "\t" + name + "\t" + detail + "\n");
                }

                const juce::String progress = juce::String::fromUTF8("Captures : ")
                    + juce::String(completed) + " / " + juce::String(total)
                    + juce::String::fromUTF8(" | PNG : ") + juce::String(succeeded)
                    + juce::String::fromUTF8(" | Échecs : ") + juce::String(failed);
                const juce::String diagnostic = success
                    ? description.name + juce::String::fromUTF8(" : miniature enregistrée.")
                    : description.name + " : " + reason;
                const bool updateImages = success && (total <= 10 || succeeded % 10 == 0);
                juce::MessageManager::callAsync([safe, progress, diagnostic, updateImages]
                {
                    if (safe != nullptr && !safe->stopped.load())
                    {
                        if (updateImages) safe->rebuildPluginTree();
                        safe->pluginStatus.setText(progress, juce::dontSendNotification);
                        safe->pluginStatus.setTooltip(diagnostic);
                    }
                });
            }

            const bool cancelled = stopped.load() || batchArtworkCancel.load();
            juce::MessageManager::callAsync([safe, completed, succeeded, failed, total, cancelled]
            {
                if (safe == nullptr || safe->stopped.load()) return;
                safe->batchArtworkRunning.store(false);
                safe->batchArtworkStatusUntilMs = juce::Time::getMillisecondCounter() + 30000;
                safe->rebuildPluginTree();
                safe->pluginStatus.setText(
                    juce::String::fromUTF8(cancelled ? "Capture interrompue : " : "Lot terminé : ")
                    + juce::String(completed) + " / " + juce::String(total)
                    + juce::String::fromUTF8(" | PNG : ") + juce::String(succeeded)
                    + juce::String::fromUTF8(" | Échecs : ") + juce::String(failed),
                    juce::dontSendNotification);
            });
        });
       #endif
    }

    void captureIsolatedPluginArtwork()
    {
        if (batchArtworkRunning.load())
        {
            pluginStatus.setText(juce::String::fromUTF8("Arrêtez d'abord la capture automatique."), juce::dontSendNotification);
            return;
        }
        const auto* selected = selectedPluginDescription();
        if (selected == nullptr)
        {
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Capture de plugin", juce::String::fromUTF8("Sélectionnez un plugin avant de lancer la capture."));
            return;
        }
        if (captureRunning.exchange(true)) { juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "Capture de plugin", juce::String::fromUTF8("Une capture est déjà en cours.")); return; }
        const auto description = *selected;
        const auto folder = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("Liberty").getChildFile("PluginThumbnails");
        if (!folder.createDirectory()) { captureRunning.store(false); juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Capture de plugin", "Le dossier des captures est inaccessible."); return; }
        const auto destination = folder.getChildFile(juce::String::toHexString(
            (juce::int64) description.createIdentifierString().hashCode64()) + ".png");
        const auto input = juce::File::createTempFile(".xml");
        const auto output = juce::File::createTempFile(".png");
        const auto xml = description.createXml();
        if (xml == nullptr || !input.replaceWithText(xml->toString()))
        {
            captureRunning.store(false);
            input.deleteFile();
            output.deleteFile();
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Capture de plugin", juce::String::fromUTF8("Impossible de préparer la capture du plugin."));
            return;
        }
        if (captureThread.joinable()) captureThread.join();
        pluginStatus.setText(juce::String::fromUTF8("Capture isolée en cours (maximum 30 s)..."), juce::dontSendNotification);
        juce::Component::SafePointer<BrowserPanel> safe(this);
        captureThread = std::thread([this, safe, input, output, destination]
        {
            juce::ChildProcess worker;
            const auto executable = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
            const bool started = worker.start(juce::StringArray {
                executable.getFullPathName(), "--liberty-artwork-worker",
                input.getFullPathName(), output.getFullPathName() });
            const bool finished = started && worker.waitForProcessToFinish(30000);
            if (started && !finished) worker.kill();
            const int exitCode = finished ? worker.getExitCode() : -1;
            bool saved = false;
            if (finished && worker.getExitCode() == 0 && output.existsAsFile()
                && output.getSize() <= 5 * 1024 * 1024)
            {
                const auto image = juce::ImageFileFormat::loadFrom(output);
                if (image.isValid() && image.getWidth() <= 4096 && image.getHeight() <= 4096)
                    saved = output.replaceFileIn(destination);
            }
            input.deleteFile();
            output.deleteFile();
            captureRunning.store(false);
            const auto logFile = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                .getChildFile("Liberty").getChildFile("PluginArtworkCapture.log");
            juce::String detail = !started ? "Le processus de capture n'a pas demarre." :
                !finished ? "Le processus de capture a depasse 30 secondes." :
                "Le processus de capture a echoue (code " + juce::String(exitCode) + ").";
            if (!saved && logFile.existsAsFile())
            {
                const auto lines = juce::StringArray::fromLines(logFile.loadFileAsString());
                for (int i = lines.size() - 1; i >= 0; --i)
                    if (lines[i].isNotEmpty()) { detail += " " + lines[i].trim(); break; }
            }
            juce::MessageManager::callAsync([safe, saved, detail]
            {
                if (safe == nullptr || safe->stopped.load()) return;
                if (saved) safe->rebuildPluginTree();
                safe->pluginStatus.setText(saved ? juce::String::fromUTF8("Interface capturée automatiquement.") : detail,
                    juce::dontSendNotification);
                juce::AlertWindow::showMessageBoxAsync(saved ? juce::MessageBoxIconType::InfoIcon : juce::MessageBoxIconType::WarningIcon,
                    "Capture de plugin", saved ? juce::String::fromUTF8("La miniature du plugin a été enregistrée.") : detail);
            });
        });
    }

    void captureSelectedPluginArtwork()
    {
        // Safety stop: the native ScreenCaptureKit implementation has
        // repeatedly crashed the host process (EXC_BAD_ACCESS) while
        // capturing a third-party plugin. Never invoke it in-process
        // until its asynchronous object lifetimes and thread ownership
        // have been redesigned and tested in an isolated helper.
        const auto message = juce::String::fromUTF8(
            "Capture native temporairement désactivée pour protéger Liberty. "
            "Vous pouvez continuer à utiliser les plugins normalement.");
        pluginStatus.setText(message, juce::dontSendNotification);
        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::WarningIcon,
            "Capture native", message);
    }


    void acceptDroppedArtwork(const juce::File& file, const juce::PluginDescription& description)
    {
        const auto name = description.name;
        const auto identifier = description.createIdentifierString();
        juce::String error;
        if (libertySavePluginArtwork(file, identifier, error))
        {
            rebuildPluginTree();
            pluginStatus.setText("Miniature enregistrée pour " + name + ".", juce::dontSendNotification);
        }
        else pluginStatus.setText(error, juce::dontSendNotification);
    }

    void importSelectedThumbnail()
    {
        const auto* selected = selectedPluginDescription();
        if (selected == nullptr)
        {
            pluginStatus.setText("Sélectionnez d'abord un plugin.", juce::dontSendNotification);
            return;
        }
        const auto identifier = selected->createIdentifierString();
        auto chooser = std::make_shared<juce::FileChooser>("Importer une miniature PNG", juce::File(), "*.png");
        juce::Component::SafePointer<BrowserPanel> safe(this);
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [safe, chooser, identifier](const juce::FileChooser& selectedFile)
            {
                if (safe == nullptr) return;
                const auto source = selectedFile.getResult();
                juce::String error;
                if (libertySavePluginArtwork(source, identifier, error))
                {
                    safe->rebuildPluginTree();
                    safe->pluginStatus.setText("Miniature importée et enregistrée.", juce::dontSendNotification);
                }
                else safe->pluginStatus.setText(error, juce::dontSendNotification);
            });
    }

    void setRoot(const juce::File& directory)
    {
        if (!directory.isDirectory()) return;
        rootDirectory = directory;
        directoryList.setDirectory(rootDirectory, true, true);
        fileTree.refresh();
        updateSampleFolderControls();
        updateSamplePackInfo();
    }

    static juce::File findInstalledSampleLibrary()
    {
        // Discover an already installed partner library without creating folders,
        // downloading content, or assuming redistribution permissions.
        const auto configured = juce::File(libraryPreferenceFile().loadFileAsString().trim());
        if (configured.isDirectory()) return configured;
        const auto home = juce::File::getSpecialLocation(juce::File::userHomeDirectory);
        const juce::File candidates[] = {
            juce::File("/Users/Shared/Liberty Music Software/Sample Libraries"),
            home.getChildFile("Music/Liberty Music Software/Sample Libraries"),
            home.getChildFile("Music/Liberty/Sample Libraries")
        };
        for (const auto& candidate : candidates)
            if (candidate.isDirectory()) return candidate;
        return {};
    }

    void setCategory(Category newCategory)
    {
        // Changing tabs abandons the explicit FX target instead of
        // unexpectedly loading a plugin into a stale slot later.
        if(newCategory!=Category::plugins)
        {
            targetEffectSlot=-1;
            targetEffectLane=-1;
        }
        if (newCategory != Category::audio) previewTransport.stop();
        category = newCategory;
        homeButton.setButtonText(category == Category::audio ? "LIBRARY" : "HOME");
        chooseLibraryButton.setVisible(category == Category::audio);
        saveFolderButton.setVisible(category == Category::audio);
        openFavouriteButton.setVisible(category == Category::audio);
        samplePackInfo.setVisible(category == Category::audio);
        browsePacksButton.setVisible(category == Category::audio);
        musicFilterButton.setVisible(category == Category::audio);
        sampleNameSearch.setVisible(category == Category::audio);
        resized();
        if (category == Category::audio)
        {
            if (sampleNameQuery.isNotEmpty() || sampleFilterMask != 0)
            {
                sampleSearchPending = true;
                sampleSearchChangedAt = juce::Time::getMillisecondCounter();
            }
            const auto library = findInstalledSampleLibrary();
            if (library.isDirectory()) setRoot(library);
        }
        previewPlay.setVisible(category == Category::audio);
        previewStop.setVisible(category == Category::audio);
        previewVolume.setVisible(category == Category::audio);
        previewAuto.setVisible(category == Category::audio);
        previewInfo.setVisible(category == Category::audio);
        filesButton.setToggleState(category == Category::files, juce::dontSendNotification);
        audioButton.setToggleState(category == Category::audio, juce::dontSendNotification);
        midiButton.setToggleState(category == Category::midi, juce::dontSendNotification);
        presetsButton.setToggleState(category == Category::presets, juce::dontSendNotification);
        pluginsButton.setToggleState(category == Category::plugins, juce::dontSendNotification);
        spliceButton.setToggleState(category == Category::splice, juce::dontSendNotification);
        const bool pluginMode = category == Category::plugins;
        const bool spliceMode = category == Category::splice;
        fileTree.setVisible(!pluginMode && !spliceMode);
        homeButton.setVisible(!pluginMode && !spliceMode);
        parentFolderButton.setVisible(!pluginMode && !spliceMode);
        spliceTitle.setVisible(spliceMode); spliceInfo.setVisible(spliceMode); spliceOpenPluginButton.setVisible(spliceMode); spliceScanButton.setVisible(spliceMode);
        if (spliceMode && !scanning.load())
        {
            refreshPlugins();
            const bool found = std::any_of(pluginDescriptions.begin(), pluginDescriptions.end(), [](const auto& d)
            {
                return d.isInstrument && d.name.containsIgnoreCase("Splice") && d.name.containsIgnoreCase("Sounds");
            });
            spliceOpenPluginButton.setEnabled(true);
            spliceInfo.setText(found ? "Splice Sounds détecté - prêt à ouvrir."
                                     : "Splice Sounds non détecté - OUVRIR lancera automatiquement un scan.", juce::dontSendNotification);
        }
        pluginTree.setVisible(pluginMode);
        pluginFiltersButton.setVisible(pluginMode);
        artworkStatsLabel.setVisible(pluginMode && getHeight() >= 540);
        thumbnailModeButton.setVisible(pluginMode);
        importThumbnailButton.setVisible(pluginMode);
        pluginSearch.setVisible(!spliceMode);
        pluginSearch.setTextToShowWhenEmpty(pluginMode ? "Rechercher un plugin ou fabricant..."
                                                   : "Rechercher un fichier...", juce::Colour(0xff8da6b1));
        filter.category = category == Category::audio ? 1 : category == Category::midi ? 2
                          : category == Category::presets ? 3 : category == Category::files ? 4 : 0;
        filter.query = pluginSearch.getText().trim();
        if (!pluginMode && !spliceMode) directoryList.refresh();
        scanPluginsButton.setVisible(pluginMode);
        blacklistButton.setVisible(pluginMode); clearBlacklistButton.setVisible(pluginMode);
        favouritePluginButton.setVisible(pluginMode); loadPluginButton.setVisible(pluginMode);
        openPluginButton.setVisible(pluginMode); unloadPluginButton.setVisible(pluginMode);
        pluginStatus.setVisible(pluginMode);
        chorusButton.setVisible(pluginMode); flangerButton.setVisible(pluginMode);
        phaserButton.setVisible(pluginMode); tremoloButton.setVisible(pluginMode);
        reverbButton.setVisible(pluginMode); delayButton.setVisible(pluginMode); driveButton.setVisible(pluginMode);
        compressorButton.setVisible(pluginMode); saturationButton.setVisible(pluginMode); widthButton.setVisible(pluginMode);
        filterButton.setVisible(pluginMode); doublerButton.setVisible(pluginMode); exciterButton.setVisible(pluginMode);
        deEsserButton.setVisible(pluginMode); gateButton.setVisible(pluginMode); bassBoostButton.setVisible(pluginMode);
        airButton.setVisible(pluginMode); punchButton.setVisible(pluginMode); softClipButton.setVisible(pluginMode); clearOneKnobButton.setVisible(pluginMode);
        if (pluginMode && !scanning.load()) refreshPlugins();
    }

    bool fileMatchesCategory(const juce::File& file) const
    {
        if (file.isDirectory()) return true;
        const auto ext = file.getFileExtension().toLowerCase();
        if (category == Category::files) return ext == ".bsmproj";
        if (category == Category::audio) return ext == ".wav" || ext == ".aif" || ext == ".aiff" || ext == ".mp3" || ext == ".flac";
        if (category == Category::midi) return ext == ".mid" || ext == ".midi";
        if (category == Category::presets) return ext == ".xml" || ext == ".fxp" || ext == ".vstpreset" || ext == ".aupreset";
        return false;
    }

    juce::String categoryHint() const
    {
        switch (category)
        {
            case Category::files: return "Projets Liberty (.bsmproj)";
            case Category::audio: return "WAV / AIFF / MP3 / FLAC";
            case Category::midi: return "Fichiers MIDI";
            case Category::presets: return "Presets";
            case Category::plugins: return "Favoris ou clic droit - glisser-déposer vers une piste";
            default: return rootDirectory.getFullPathName();
        }
    }

    void openFileOrDirectory(const juce::File& file)
    {
        if (file == juce::File{} || !file.exists()) return;
        if (file.isDirectory()) { setRoot(file); return; }
        if (!fileMatchesCategory(file)) return;
        if (file.hasFileExtension("bsmproj"))
        {
            // Use the existing project-open workflow, which owns unsaved-change
            // checks, rather than loading a file behind the user's back.
            owner.openProject();
            return;
        }
        const auto ext = file.getFileExtension().toLowerCase();
        if (ext != ".wav" && ext != ".aif" && ext != ".aiff") return;
        const int track = selectedAudioTrack();
        if (track < 0)
        {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Liberty - Browser",
                                                   "Sélectionne une piste Audio pour charger ce fichier.", "OK");
            return;
        }
        juce::String error;
        if (!owner.audioEngine.loadAudioFileIntoTrack(track, file, error))
        {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Liberty - Browser", error, "OK");
            return;
        }
        owner.trackSourceFiles[(size_t)track] = file;
        owner.audioEngine.setTrackStartSeconds(track, juce::jmax(0.0, owner.playheadSeconds));
        owner.isPlaying = false;
        owner.playheadSeconds = 0.0;
        owner.selectedTrack = track;
        owner.rebuildWaveformCache(track);
        owner.repaint();
    }

    PluginTreeItem* makePluginItem(const juce::PluginDescription& d)
    {
        return new PluginTreeItem(PluginTreeItem::Kind::plugin, d.name, &d,
            [this](const juce::PluginDescription& x) { loadPluginDescription(x); },
            [this](const juce::PluginDescription& x) { return isFavouritePlugin(x); },
            [this](const juce::PluginDescription& x) { toggleFavourite(x); },
            thumbnailModeButton.getToggleState(),
            [this](const juce::File& file, const juce::PluginDescription& description)
            {
                // Rebuilding the TreeView inside TreeViewItem::filesDropped would
                // destroy the item currently handling JUCE's drop callback.
                juce::Component::SafePointer<BrowserPanel> safe(this);
                juce::MessageManager::callAsync([safe, file, description]
                {
                    if (safe != nullptr && !safe->stopped.load())
                        safe->acceptDroppedArtwork(file, description);
                });
            },
            [this](const juce::PluginDescription& description, juce::Point<int> where)
            {
                showPluginFamilyEditor(description, where);
            },
            isPluginManuallyClassified(d));
    }


    static juce::String normalisedManufacturer(const juce::PluginDescription& d)
    {
        const auto name = d.manufacturerName.trim();
        return name.isEmpty() ? "AUTRES" : name;
    }

    bool matchesPluginMetadata(const juce::PluginDescription& d) const
    {
        if (selectedManufacturer.isNotEmpty()
            && !normalisedManufacturer(d).equalsIgnoreCase(selectedManufacturer))
            return false;

        const auto format = d.pluginFormatName.trim();
        if (pluginFormatFilter == PluginFormatFilter::au
            && !(format.equalsIgnoreCase("AudioUnit")
                 || format.equalsIgnoreCase("Audio Unit")
                 || format.equalsIgnoreCase("AU")))
            return false;
        if (pluginFormatFilter == PluginFormatFilter::vst3
            && !format.equalsIgnoreCase("VST3"))
            return false;

        if (pluginTypeFilter == PluginTypeFilter::instrument && !d.isInstrument)
            return false;
        if (pluginTypeFilter == PluginTypeFilter::effect && d.isInstrument)
            return false;
        return true;
    }

    void refreshPluginFiltersButton(int visibleCount = -1)
    {
        const int active = (selectedManufacturer.isNotEmpty() ? 1 : 0)
                         + (pluginFormatFilter != PluginFormatFilter::all ? 1 : 0)
                         + (pluginTypeFilter != PluginTypeFilter::all ? 1 : 0)
                         + (quickView != QuickView::all ? 1 : 0)
                         + (pluginUsageFilter != PluginUsage::all ? 1 : 0)
                         + (selectedCollection.isNotEmpty() ? 1 : 0);
        pluginFiltersButton.setButtonText(active == 0 ? "FILTRES"
            : "FILTRES (" + juce::String(active) + ")");
        pluginFiltersButton.setColour(juce::TextButton::buttonColourId,
            juce::Colour(active > 0 ? 0xff286674 : 0xff203746));

        juce::String detail = juce::String::fromUTF8("Fabricant : ")
            + (selectedManufacturer.isEmpty() ? juce::String("Tous") : selectedManufacturer)
            + juce::String::fromUTF8(" | Format : ")
            + (pluginFormatFilter == PluginFormatFilter::au ? "AU"
                : pluginFormatFilter == PluginFormatFilter::vst3 ? "VST3" : "Tous")
            + juce::String::fromUTF8(" | Type : ")
            + (pluginTypeFilter == PluginTypeFilter::instrument ? "Instruments"
                : pluginTypeFilter == PluginTypeFilter::effect ? "Effets" : "Tous")
            + juce::String::fromUTF8(" | Accès rapide : ")
            + (quickView == QuickView::favourites ? "Favoris"
                : quickView == QuickView::recent ? "Récents" : "Tous")
            + juce::String::fromUTF8(" | Famille : ")
            + usageLabel(pluginUsageFilter)
            + juce::String::fromUTF8(" | Classement : ")
            + (groupByUsage ? "Fonction" : "Fabricant")
            + juce::String::fromUTF8(" | Collection : ")
            + (selectedCollection.isNotEmpty() ? selectedCollection : juce::String("Toutes"));
        if (visibleCount >= 0)
            detail += juce::String::fromUTF8(" | Résultats : ") + juce::String(visibleCount);
        pluginFiltersButton.setTooltip(detail);
    }

    void showPluginFiltersMenu()
    {
        juce::StringArray manufacturers;
        for (const auto& d : pluginDescriptions)
        {
            const auto name = normalisedManufacturer(d);
            if (!manufacturers.contains(name, true))
                manufacturers.add(name);
        }
        manufacturers.sort(true);

        juce::PopupMenu menu;
        menu.addSectionHeader(juce::String::fromUTF8("FILTRES DE PLUGINS"));

        juce::PopupMenu accessMenu;
        accessMenu.addItem(700, juce::String::fromUTF8("TOUS LES PLUGINS"), true,
                           quickView == QuickView::all);
        accessMenu.addItem(701, juce::String::fromUTF8("FAVORIS UNIQUEMENT"), true,
                           quickView == QuickView::favourites);
        accessMenu.addItem(702, juce::String::fromUTF8("RÉCENTS UNIQUEMENT"), true,
                           quickView == QuickView::recent);
        menu.addSubMenu(juce::String::fromUTF8("ACCÈS RAPIDE"), accessMenu);

        juce::StringArray collectionNames;
        juce::PopupMenu collectionsMenu;
        collectionsMenu.addItem(5000, juce::String::fromUTF8("TOUS LES PLUGINS"),
                                 true, selectedCollection.isEmpty());
        for (const auto& item : pluginCollections)
        {
            const int number = collectionNames.size();
            collectionNames.add(item.first);
            collectionsMenu.addItem(5001 + number,
                item.first + "  (" + juce::String(item.second.size()) + ")", true,
                selectedCollection == item.first);
        }
        collectionsMenu.addSeparator();
        collectionsMenu.addItem(5100, juce::String::fromUTF8("NOUVELLE COLLECTION..."),
                                 !collectionsReadOnly && pluginCollections.size() < 64);
        collectionsMenu.addSeparator();
        collectionsMenu.addItem(5400, juce::String::fromUTF8("EXPORTER LES COLLECTIONS..."),
                                 !pluginCollections.empty());
        collectionsMenu.addItem(5401, juce::String::fromUTF8("IMPORTER DES COLLECTIONS..."),
                                 !collectionsReadOnly);
        collectionsMenu.addItem(5402, juce::String::fromUTF8("RESTAURER LA SAUVEGARDE LOCALE..."),
                                 hasRecoverableCollectionsBackup());
        if (collectionsReadOnly)
            collectionsMenu.addSectionHeader(juce::String::fromUTF8(
                "FICHIER PRINCIPAL ILLISIBLE - RESTAURATION CONSEILLÉE"));
        if (!collectionNames.isEmpty())
        {
            juce::PopupMenu renameMenu, deleteMenu;
            for (int i = 0; i < collectionNames.size(); ++i)
            {
                renameMenu.addItem(5200 + i, collectionNames[i], !collectionsReadOnly);
                deleteMenu.addItem(5300 + i, collectionNames[i], !collectionsReadOnly);
            }
            collectionsMenu.addSubMenu(juce::String::fromUTF8("RENOMMER"), renameMenu);
            collectionsMenu.addSubMenu(juce::String::fromUTF8("SUPPRIMER"), deleteMenu);
        }
        menu.addSubMenu("COLLECTIONS", collectionsMenu);

        juce::PopupMenu manufacturerMenu;
        manufacturerMenu.addItem(300, juce::String::fromUTF8("TOUS LES FABRICANTS"),
                                 true, selectedManufacturer.isEmpty());
        manufacturerMenu.addSeparator();

        // Alphabetical submenus avoid opening a 200+-row manufacturer list.
        // Each item ID maps to the stable manufacturers snapshot in callback.
        juce::String letter;
        juce::PopupMenu letterMenu;
        for (int i = 0; i < manufacturers.size(); ++i)
        {
            auto prefix = manufacturers[i].substring(0, 1).toUpperCase();
            if (prefix.isEmpty() || !prefix.containsOnly("ABCDEFGHIJKLMNOPQRSTUVWXYZ"))
                prefix = "#";
            if (prefix != letter)
            {
                if (letter.isNotEmpty())
                    manufacturerMenu.addSubMenu(letter, letterMenu);
                letter = prefix;
                letterMenu = juce::PopupMenu();
            }
            letterMenu.addItem(1000 + i, manufacturers[i], true,
                manufacturers[i].equalsIgnoreCase(selectedManufacturer));
        }
        if (letter.isNotEmpty())
            manufacturerMenu.addSubMenu(letter, letterMenu);
        menu.addSubMenu(juce::String::fromUTF8("FABRICANT"), manufacturerMenu);

        juce::PopupMenu formatMenu;
        formatMenu.addItem(400, "TOUS LES FORMATS", true,
                           pluginFormatFilter == PluginFormatFilter::all);
        formatMenu.addItem(401, "AUDIO UNIT (AU)", true,
                           pluginFormatFilter == PluginFormatFilter::au);
        formatMenu.addItem(402, "VST3", true,
                           pluginFormatFilter == PluginFormatFilter::vst3);
        menu.addSubMenu("FORMAT", formatMenu);

        juce::PopupMenu typeMenu;
        typeMenu.addItem(500, "TOUS LES TYPES", true,
                         pluginTypeFilter == PluginTypeFilter::all);
        typeMenu.addItem(501, "INSTRUMENTS", true,
                         pluginTypeFilter == PluginTypeFilter::instrument);
        typeMenu.addItem(502, "EFFETS AUDIO", true,
                         pluginTypeFilter == PluginTypeFilter::effect);
        menu.addSubMenu("TYPE DE PLUGIN", typeMenu);

        // Category counts come from installed plugin metadata; they never
        // require instantiation, scanning or loading a third-party plugin.
        std::array<int, 17> categoryCounts {};
        for (const auto& d : pluginDescriptions)
        {
            const int index = (int)effectivePluginUsage(d);
            if (index > 0 && index < (int)categoryCounts.size())
                ++categoryCounts[(size_t)index];
        }
        juce::PopupMenu usageMenu;
        usageMenu.addItem(800, juce::String::fromUTF8("TOUTES LES FAMILLES"), true,
                          pluginUsageFilter == PluginUsage::all);
        juce::PopupMenu instrumentMenu, effectMenu;
        for (auto usage : instrumentUsages())
        {
            const auto count = categoryCounts[(size_t)(int)usage];
            instrumentMenu.addItem(800 + (int)usage,
                usageLabel(usage) + "  (" + juce::String(count) + ")", true,
                pluginUsageFilter == usage);
        }
        // An instrument/effect can be classified as 'unclassified'; the
        // dedicated category below includes both.
        for (auto usage : effectUsages())
        {
            const auto count = categoryCounts[(size_t)(int)usage];
            effectMenu.addItem(800 + (int)usage,
                usageLabel(usage) + "  (" + juce::String(count) + ")", true,
                pluginUsageFilter == usage);
        }
        usageMenu.addSubMenu("INSTRUMENTS", instrumentMenu);
        usageMenu.addSubMenu("EFFETS AUDIO", effectMenu);
        usageMenu.addSeparator();
        usageMenu.addItem(800 + (int)PluginUsage::unclassified,
            usageLabel(PluginUsage::unclassified) + "  ("
            + juce::String(categoryCounts[(size_t)(int)PluginUsage::unclassified]) + ")",
            true, pluginUsageFilter == PluginUsage::unclassified);
        menu.addSubMenu(juce::String::fromUTF8("FAMILLE SONORE / FONCTION"), usageMenu);

        juce::PopupMenu layoutMenu;
        layoutMenu.addItem(900, "PAR FONCTION", true, groupByUsage);
        layoutMenu.addItem(901, "PAR FABRICANT", true, !groupByUsage);
        menu.addSubMenu("ORGANISATION", layoutMenu);

        juce::PopupMenu backupMenu;
        backupMenu.addSectionHeader(juce::String::fromUTF8("CATÉGORIES PERSONNALISÉES"));
        backupMenu.addItem(950, juce::String::fromUTF8("EXPORTER LES CLASSEMENTS..."),
                           !pluginFamilyOverrides.empty());
        backupMenu.addItem(951, juce::String::fromUTF8("IMPORTER DES CLASSEMENTS..."),
                           !familyOverridesReadOnly);
        menu.addSubMenu(juce::String::fromUTF8("SAUVEGARDE / RESTAURATION"), backupMenu);

        menu.addSeparator();
        const bool active = selectedManufacturer.isNotEmpty()
            || pluginFormatFilter != PluginFormatFilter::all
            || pluginTypeFilter != PluginTypeFilter::all
            || quickView != QuickView::all
            || pluginUsageFilter != PluginUsage::all
            || selectedCollection.isNotEmpty();
        menu.addItem(600, juce::String::fromUTF8("RÉINITIALISER LES FILTRES"), active);

        juce::Component::SafePointer<BrowserPanel> safe(this);
        menu.showMenuAsync(juce::PopupMenu::Options()
            .withTargetComponent(&pluginFiltersButton)
            .withMaximumNumColumns(1),
            [safe, manufacturers, collectionNames](int chosen)
            {
                if (safe == nullptr || safe->stopped.load() || chosen <= 0) return;
                if (chosen == 600)
                {
                    safe->selectedManufacturer.clear();
                    safe->pluginFormatFilter = PluginFormatFilter::all;
                    safe->pluginTypeFilter = PluginTypeFilter::all;
                    safe->quickView = QuickView::all;
                    safe->pluginUsageFilter = PluginUsage::all;
                    safe->selectedCollection.clear();
                }
                else if (chosen == 5000)
                    safe->selectedCollection.clear();
                else if (chosen > 5000 && chosen <= 5000 + collectionNames.size())
                    safe->selectedCollection = collectionNames[chosen - 5001];
                else if (chosen == 5100)
                {
                    safe->promptCollectionName();
                    return;
                }
                else if (chosen == 5400)
                {
                    safe->exportPluginCollections();
                    return;
                }
                else if (chosen == 5401)
                {
                    safe->importPluginCollections();
                    return;
                }
                else if (chosen == 5402)
                {
                    safe->restorePluginCollectionsBackup();
                    return;
                }
                else if (chosen >= 5200 && chosen < 5200 + collectionNames.size())
                {
                    safe->promptCollectionName(collectionNames[chosen - 5200]);
                    return;
                }
                else if (chosen >= 5300 && chosen < 5300 + collectionNames.size())
                {
                    safe->confirmDeleteCollection(collectionNames[chosen - 5300]);
                    return;
                }
                else if (chosen == 950)
                {
                    safe->exportPluginFamilyOverrides();
                    return;
                }
                else if (chosen == 951)
                {
                    safe->importPluginFamilyOverrides();
                    return;
                }
                else if (chosen == 900 || chosen == 901)
                    safe->groupByUsage = (chosen == 900);
                else if (chosen >= 800 && chosen <= 800 + (int)PluginUsage::unclassified)
                    safe->pluginUsageFilter = (PluginUsage)(chosen - 800);
                else if (chosen >= 700 && chosen <= 702)
                    safe->quickView = (QuickView) (chosen - 700);
                else if (chosen == 300)
                    safe->selectedManufacturer.clear();
                else if (chosen >= 1000 && chosen < 1000 + manufacturers.size())
                    safe->selectedManufacturer = manufacturers[chosen - 1000];
                else if (chosen >= 400 && chosen <= 402)
                    safe->pluginFormatFilter = (PluginFormatFilter) (chosen - 400);
                else if (chosen >= 500 && chosen <= 502)
                    safe->pluginTypeFilter = (PluginTypeFilter) (chosen - 500);
                else return;

                safe->rebuildPluginTree();
            });
    }

    void setArtworkView(ArtworkView next)
    {
        if (artworkView == next) return;
        artworkView = next;
        rebuildPluginTree();
    }

    void navigatePluginResults(int step)
    {
        if (category != Category::plugins) return;
        // Arrow navigation must use the latest typed query even when the
        // debounce delay has not yet expired.
        if (pluginSearchPending) rebuildPluginTree();
        if (keyboardResults.empty()) return;
        juce::String selectedKey;
        if (const auto* old = selectedPluginDescription())
            selectedKey = old->createIdentifierString();
        int index = -1;
        for (int i = 0; i < (int) keyboardResults.size(); ++i)
        {
            if (const auto* d = keyboardResults[(size_t)i]->getPluginDescription();
                d != nullptr && d->createIdentifierString() == selectedKey)
            {
                index = i;
                break;
            }
        }
        index = index < 0 ? (step >= 0 ? 0 : (int) keyboardResults.size() - 1)
                          : juce::jlimit(0, (int) keyboardResults.size() - 1, index + step);
        auto* item = keyboardResults[(size_t)index];
        for (auto* parent = item->getParentItem(); parent != nullptr;
             parent = parent->getParentItem())
            parent->setOpen(true);
        item->setSelected(true, true);
        pluginTree.scrollToKeepItemVisible(item);
    }

    void rebuildPluginTree()
    {
        // Any explicit rebuild (filter, refresh, keyboard action) supersedes a
        // scheduled keystroke rebuild, avoiding a duplicate expensive pass.
        pluginSearchPending = false;
        // Status and filter are based on the same exact JUCE identifiers that
        // the existing PNG importer uses. AU and VST3 variants are independent.
        const auto folder = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("Liberty").getChildFile("PluginThumbnails");
        const auto catalogue = folder.getSiblingFile("PluginArtworkCatalogue");
        const auto failuresFile = folder.getSiblingFile("PluginArtworkBatchFailures.tsv");
        juce::StringArray failedHashes;
        if (failuresFile.existsAsFile())
            for (const auto& line : juce::StringArray::fromLines(failuresFile.loadFileAsString()))
            {
                const auto hash = line.upToFirstOccurrenceOf("\t", false, false).trim();
                if (hash.isNotEmpty()) failedHashes.addIfNotAlreadyThere(hash);
            }

        const auto artworkHash = [](const juce::PluginDescription& description)
        {
            return juce::String::toHexString(
                (juce::int64) description.createIdentifierString().hashCode64());
        };
        const auto hasArtwork = [&folder, &catalogue, &artworkHash](const juce::PluginDescription& d)
        {
            const auto name = artworkHash(d) + ".png";
            return folder.getChildFile(name).existsAsFile()
                || catalogue.getChildFile(name).existsAsFile();
        };
        artworkTotalCount = 0;
        artworkPresentCount = 0;
        int artworkFailedCount = 0;
        juce::StringArray identifiers;
        for (const auto& d : pluginDescriptions)
        {
            const auto identifier = d.createIdentifierString();
            if (identifier.isEmpty() || identifiers.contains(identifier)) continue;
            identifiers.add(identifier);
            ++artworkTotalCount;
            if (hasArtwork(d)) ++artworkPresentCount;
            else if (failedHashes.contains(artworkHash(d))) ++artworkFailedCount;
        }
        const int missing = artworkTotalCount - artworkPresentCount;
        artworkStatsLabel.setText(
            juce::String::fromUTF8("  PNG ") + juce::String(artworkPresentCount) + "/"
                + juce::String(artworkTotalCount)
                + juce::String::fromUTF8("  |  SANS IMAGE ") + juce::String(missing)
                + juce::String::fromUTF8("  |  ÉCHECS ") + juce::String(artworkFailedCount),
            juce::dontSendNotification);
        artworkStatsLabel.setTooltip(
            juce::String::fromUTF8("Miniatures présentes : ") + juce::String(artworkPresentCount)
            + juce::String::fromUTF8(" | Sans image : ") + juce::String(missing)
            + juce::String::fromUTF8(" | Échecs non résolus : ") + juce::String(artworkFailedCount)
            + juce::String::fromUTF8(" | À tenter : ") + juce::String(missing - artworkFailedCount));

        // Preserve the exact AU/VST3 selection across a search/filter refresh.
        juce::String previousSelection;
        if (const auto* selected = selectedPluginDescription())
            previousSelection = selected->createIdentifierString();
        pluginTree.setRootItem(nullptr);
        keyboardResults.clear();
        pluginRoot = std::make_unique<PluginTreeItem>(PluginTreeItem::Kind::root, "ROOT");
        std::sort(pluginDescriptions.begin(), pluginDescriptions.end(), [](const auto& a, const auto& b)
        {
            auto am = a.manufacturerName.trim(); if (am.isEmpty()) am = "Other";
            auto bm = b.manufacturerName.trim(); if (bm.isEmpty()) bm = "Other";
            const int c = am.compareNatural(bm, false);
            return c != 0 ? c < 0 : a.name.compareNatural(b.name, false) < 0;
        });

        const auto query = pluginSearch.getText().trim();
        const auto matches = [this, &query, &hasArtwork, &failedHashes, &artworkHash]
            (const juce::PluginDescription& d)
        {
            const bool searchMatches = query.isEmpty() || d.name.containsIgnoreCase(query)
                || d.manufacturerName.containsIgnoreCase(query)
                || d.pluginFormatName.containsIgnoreCase(query);
            if (!searchMatches || !matchesPluginMetadata(d)) return false;
            if (pluginUsageFilter != PluginUsage::all
                && effectivePluginUsage(d) != pluginUsageFilter) return false;
            if (selectedCollection.isNotEmpty())
            {
                const auto group = pluginCollections.find(selectedCollection);
                if (group == pluginCollections.end()
                    || !group->second.contains(favouriteKey(d))) return false;
            }
            if (quickView == QuickView::favourites && !isFavouritePlugin(d)) return false;
            if (quickView == QuickView::recent && !recentKeys.contains(favouriteKey(d))) return false;
            if (artworkView == ArtworkView::all) return true;
            const bool present = hasArtwork(d);
            if (artworkView == ArtworkView::withImage) return present;
            if (artworkView == ArtworkView::withoutImage) return !present;
            return !present && failedHashes.contains(artworkHash(d));
        };
        // Count actual matching AU/VST3 descriptions (not manufacturer headings).
        int visibleCount = 0;
        for (const auto& d : pluginDescriptions)
            if (matches(d)) ++visibleCount;
        refreshPluginFiltersButton(visibleCount);

        auto* fav = new PluginTreeItem(PluginTreeItem::Kind::manufacturer, "FAVORIS");
        pluginRoot->addSubItem(fav);
        for (const auto& d : pluginDescriptions)
            if (isFavouritePlugin(d) && matches(d))
            {
                auto* item = makePluginItem(d);
                fav->addSubItem(item);
                if (quickView == QuickView::favourites)
                    keyboardResults.push_back(item);
            }

        // Most-recently loaded plugins are displayed in actual usage order.
        // This is a second view of installed descriptions, not a plugin rescan.
        PluginTreeItem* recentGroup = nullptr;
        for (const auto& key : recentKeys)
        {
            for (const auto& d : pluginDescriptions)
            {
                if (favouriteKey(d) != key || !matches(d)) continue;
                if (recentGroup == nullptr)
                {
                    recentGroup = new PluginTreeItem(PluginTreeItem::Kind::manufacturer, "RÉCENTS");
                    pluginRoot->addSubItem(recentGroup);
                }
                auto* item = makePluginItem(d);
                recentGroup->addSubItem(item);
                if (quickView == QuickView::recent)
                    keyboardResults.push_back(item);
                break;
            }
        }

        // A plugin may appear in any number of independent custom collections.
        // Collapsed groups limit UI work and do not change family classification.
        PluginTreeItem* collectionsRoot = nullptr;
        if (!pluginCollections.empty())
        {
            collectionsRoot = new PluginTreeItem(PluginTreeItem::Kind::manufacturer, "COLLECTIONS");
            pluginRoot->addSubItem(collectionsRoot);
            for (const auto& group : pluginCollections)
            {
                if (selectedCollection.isNotEmpty() && group.first != selectedCollection)
                    continue;
                auto* collectionItem = new PluginTreeItem(
                    PluginTreeItem::Kind::manufacturer, group.first);
                collectionsRoot->addSubItem(collectionItem);
                for (const auto& d : pluginDescriptions)
                {
                    if (!group.second.contains(favouriteKey(d)) || !matches(d)) continue;
                    auto* item = makePluginItem(d);
                    collectionItem->addSubItem(item);
                    if (selectedCollection.isNotEmpty() && quickView == QuickView::all)
                        keyboardResults.push_back(item);
                }
                collectionItem->setOpen(selectedCollection.isNotEmpty());
            }
        }

        // Library-style hierarchy: instruments and audio effects are distinct.
        // Keep manufacturers inside each family, and never classify unknown
        // plugins as MIDI effects without reliable plugin metadata.
        auto* instruments = new PluginTreeItem(PluginTreeItem::Kind::manufacturer, "INSTRUMENTS");
        auto* audioEffects = new PluginTreeItem(PluginTreeItem::Kind::manufacturer, "EFFETS AUDIO");
        pluginRoot->addSubItem(instruments);
        pluginRoot->addSubItem(audioEffects);
        for (int family = 0; family < 2; ++family)
        {
            auto* parent = family == 0 ? instruments : audioEffects;
            // Keep the original manufacturer-first hierarchy available.
            // Function-first creates category -> manufacturer -> plugin.
            if (groupByUsage)
            {
                std::vector<PluginUsage> available;
                if (family == 0)
                    available.insert(available.end(), instrumentUsages().begin(), instrumentUsages().end());
                else
                    available.insert(available.end(), effectUsages().begin(), effectUsages().end());
                available.push_back(PluginUsage::unclassified);
                for (const auto usage : available)
                {
                    PluginTreeItem* usageGroup = nullptr;
                    juce::String current;
                    PluginTreeItem* manufacturerGroup = nullptr;
                    for (const auto& d : pluginDescriptions)
                    {
                        if (d.isInstrument != (family == 0) || !matches(d)
                            || effectivePluginUsage(d) != usage) continue;
                        if (usageGroup == nullptr)
                        {
                            usageGroup = new PluginTreeItem(PluginTreeItem::Kind::manufacturer,
                                usageLabel(usage));
                            parent->addSubItem(usageGroup);
                        }
                        const auto manufacturer = normalisedManufacturer(d);
                        if (manufacturer != current)
                        {
                            current = manufacturer;
                            manufacturerGroup = new PluginTreeItem(
                                PluginTreeItem::Kind::manufacturer, manufacturer);
                            usageGroup->addSubItem(manufacturerGroup);
                        }
                        auto* item = makePluginItem(d);
                        manufacturerGroup->addSubItem(item);
                        if (quickView == QuickView::all && selectedCollection.isEmpty())
                            keyboardResults.push_back(item);
                    }
                }
            }
            else
            {
                juce::String current;
                PluginTreeItem* group = nullptr;
                for (const auto& d : pluginDescriptions)
                {
                    if (d.isInstrument != (family == 0) || !matches(d)) continue;
                    const auto manufacturer = normalisedManufacturer(d);
                    if (manufacturer != current)
                    {
                        current = manufacturer;
                        group = new PluginTreeItem(PluginTreeItem::Kind::manufacturer,
                                                   manufacturer);
                        parent->addSubItem(group);
                    }
                    if (group != nullptr)
                    {
                        auto* item = makePluginItem(d);
                        group->addSubItem(item);
                        if (quickView == QuickView::all && selectedCollection.isEmpty())
                            keyboardResults.push_back(item);
                    }
                }
            }
        }
        // During a search, reveal matching manufacturers without requiring
        // the user to expand every nested folder manually.
        if (query.isNotEmpty() || selectedManufacturer.isNotEmpty()
            || pluginFormatFilter != PluginFormatFilter::all
            || pluginTypeFilter != PluginTypeFilter::all
            || quickView != QuickView::all
            || pluginUsageFilter != PluginUsage::all
            || selectedCollection.isNotEmpty()
            || artworkView != ArtworkView::all)
        {
            for (auto* family : { instruments, audioEffects })
                for (int i = 0; i < family->getNumSubItems(); ++i)
                {
                    auto* categoryOrManufacturer = family->getSubItem(i);
                    categoryOrManufacturer->setOpen(true);
                    if (groupByUsage)
                        for (int j = 0; j < categoryOrManufacturer->getNumSubItems(); ++j)
                            categoryOrManufacturer->getSubItem(j)->setOpen(true);
                }
        }
        pluginTree.setRootItem(pluginRoot.get());
        pluginRoot->setOpen(true); fav->setOpen(true);
        if (recentGroup != nullptr) recentGroup->setOpen(true);
        if (collectionsRoot != nullptr) collectionsRoot->setOpen(true);
        instruments->setOpen(true); audioEffects->setOpen(true);

        PluginTreeItem* preferred = nullptr;
        if (previousSelection.isNotEmpty())
            for (auto* item : keyboardResults)
            {
                const auto* d = item->getPluginDescription();
                if (d != nullptr && d->createIdentifierString() == previousSelection)
                {
                    preferred = item;
                    break;
                }
            }
        // A nonempty search presents the first result immediately without
        // loading a plugin or moving keyboard focus away from the search.
        if (preferred == nullptr && query.isNotEmpty() && !keyboardResults.empty())
            preferred = keyboardResults.front();
        if (preferred != nullptr)
        {
            for (auto* parent = preferred->getParentItem(); parent != nullptr;
                 parent = parent->getParentItem())
                parent->setOpen(true);
            preferred->setSelected(true, true);
            pluginTree.scrollToKeepItemVisible(preferred);
        }
        pluginTree.repaint();
        repaint(10, 373, juce::jmax(0, getWidth() - 20), 25);
    }

    void refreshPlugins()
    {
        pluginDescriptions = LibertyPluginHost::instance().getPluginDescriptions();
        rebuildPluginTree(); refreshPluginStatus();
    }

    const juce::PluginDescription* selectedPluginDescription() const
    {
        if (auto* selected = pluginTree.getSelectedItem(0))
            if (auto* item = dynamic_cast<PluginTreeItem*>(selected)) return item->getPluginDescription();
        return nullptr;
    }

    void setScanStatus(const juce::String& text) { const juce::ScopedLock lock(scanStatusLock); scanStatusText = text; }
    juce::String getScanStatus() const { const juce::ScopedLock lock(scanStatusLock); return scanStatusText; }

    void scanPlugins()
    {
        if (scanning.exchange(true)) return;
        if (scanThread.joinable()) scanThread.join();
        scanFinishedPending.store(false);
        for (auto* b : { &scanPluginsButton, &blacklistButton, &clearBlacklistButton, &favouritePluginButton,
                         &loadPluginButton, &openPluginButton, &unloadPluginButton }) b->setEnabled(false);
        spliceScanButton.setEnabled(false);
        setScanStatus("Préparation du scan AU + VST3");
        scanThread = std::thread([this]
        {
            LibertyPluginHost::instance().scanInstalledPlugins([this](const juce::String& formatName, const juce::String& pluginName, float progress)
            {
                if (stopped.load()) return;
                const int percent = juce::jlimit(0, 100, juce::roundToInt(progress * 100.0f));
                setScanStatus(pluginName.isEmpty() ? "Finalisation du scan" : "SCAN " + formatName + "  " + juce::String(percent) + "%  " + pluginName);
            });
            if (!stopped.load()) { scanning.store(false); scanFinishedPending.store(true); }
        });
    }

    void showBlacklist()
    {
        auto& host = LibertyPluginHost::instance();
        auto entries = host.getBlacklistedPlugins();
        juce::String text = entries.isEmpty() ? "Aucun plugin blacklisté." : entries.joinIntoString("\n");
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, "Liberty - Blacklist", text, "OK");
    }

    void clearBlacklist() { LibertyPluginHost::instance().clearBlacklist(); refreshPluginStatus(); }

    void loadPluginDescription(const juce::PluginDescription& d)
    {
        if (scanning.load()) return;
        if(targetEffectSlot>=0)
        {
            if(d.isInstrument)
            {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::WarningIcon,"Liberty - Effets",
                    "Sélectionne un effet audio AU ou VST3, pas un instrument.","OK");
                return;
            }
            auto& host=LibertyPluginHost::instance();
            auto& one=LibertyOneKnobManager::instance();
            const int lane=targetEffectLane,slot=targetEffectSlot;
            const bool instrument=targetIsInstrument;
            const int key=instrument?100000+lane*8+slot:lane*8+slot;
            const bool valid=lane>=0&&slot>=0&&slot<LibertyPluginHost::effectSlotsPerTrack&&
                (instrument?lane<owner.getInstrumentTrackCount()
                           :lane<owner.getAudioTrackCount());
            const bool occupied=one.hasEffect(key)||
                (instrument?host.hasEffectForInstrumentTrackSlot(lane,slot)
                           :host.hasEffectForTrackSlot(lane,slot));
            if(!valid||occupied)
            {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::WarningIcon,"Liberty - Effets",
                    "Ce slot n'est plus libre. Rouvre la chaîne d'effets.","OK");
                return;
            }
            juce::String error;
            const bool ok=instrument
                ?host.loadEffectForInstrumentTrackSlot(lane,slot,d,error)
                :host.loadEffectForTrackSlot(lane,slot,d,error);
            if(!ok)
            {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::WarningIcon,"Liberty - Plugin",error,"OK");
                return;
            }
            rememberRecentPlugin(d);
            toggleButton.setToggleState(false,juce::dontSendNotification);
            setBrowserOpen(false);
            refreshLibertyMixConsole(&owner);
            refreshPluginStatus();
            owner.repaint();
            if(instrument)host.showEffectEditorForInstrumentTrackSlot(lane,slot);
            else host.showEditorForTrackSlot(lane,slot);
            return;
        }
        juce::String error;
        bool ok = false;
        auto& host = LibertyPluginHost::instance();
        if (d.isInstrument)
        {
            const int instrumentLane = selectedInstrumentTrack();
            if (instrumentLane < 0)
            {
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Liberty - Instrument",
                                                       "Sélectionne une piste Instrument pour charger cet instrument.", "OK");
                return;
            }
            ok = host.loadInstrumentForTrack(instrumentLane, d, error);
            if (ok) host.showInstrumentEditorForTrack(instrumentLane);
        }
        else
        {
            const int instrumentLane = selectedInstrumentTrack();
            if (instrumentLane >= 0)
            {
                int slot = -1;
                for (int i = 0; i < LibertyPluginHost::effectSlotsPerTrack; ++i)
                    if (!host.hasEffectForInstrumentTrackSlot(instrumentLane, i)) { slot = i; break; }
                if (slot < 0) { error = "Les 8 slots FX de cette piste Instrument sont occupés."; }
                else
                {
                    ok = host.loadEffectForInstrumentTrackSlot(instrumentLane, slot, d, error);
                    if (ok) host.showEffectEditorForInstrumentTrackSlot(instrumentLane, slot);
                }
            }
            else
            {
                const int track = selectedAudioTrack();
                if (track < 0)
                {
                    juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Liberty - Effet",
                                                           "Sélectionne une piste Audio ou Instrument pour charger cet effet.", "OK");
                    return;
                }
                int slot = -1;
                for (int i = 0; i < LibertyPluginHost::effectSlotsPerTrack; ++i)
                    if (!host.hasEffectForTrackSlot(track, i)) { slot = i; break; }
                if (slot < 0) { error = "Les 8 slots FX de cette piste Audio sont occupés."; }
                else
                {
                    ok = host.loadEffectForTrackSlot(track, slot, d, error);
                    if (ok) host.showEditorForTrackSlot(track, slot);
                }
            }
        }
        if (!ok) juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Liberty - Plugin", error, "OK");
        else rememberRecentPlugin(d);
        refreshPluginStatus();
        refreshLibertyMixConsole(&owner);
        owner.repaint();
    }

    int selectedAudioTrack() const
    {
        const int track = owner.selectedTrack;
        return (track >= 0 && track < owner.getAudioTrackCount()) ? track : -1;
    }

    int selectedInstrumentTrack() const
    {
        const int firstInstrument = owner.getAudioTrackCount() + owner.getMidiTrackCount();
        const int track = owner.selectedTrack;
        return (track >= firstInstrument && track < firstInstrument + owner.getInstrumentTrackCount()) ? track - firstInstrument : -1;
    }

    int firstFreeOneKnobInsertSlot() const
    {
        auto& host = LibertyPluginHost::instance();
        auto& one = LibertyOneKnobManager::instance();
        if (const int instrumentLane = selectedInstrumentTrack(); instrumentLane >= 0)
        {
            for (int slot = 0; slot < LibertyPluginHost::effectSlotsPerTrack; ++slot)
                if (!host.hasEffectForInstrumentTrackSlot(instrumentLane, slot)
                    && !one.hasEffect(100000 + instrumentLane * 8 + slot)) return slot;
        }
        else if (const int audioLane = selectedAudioTrack(); audioLane >= 0)
        {
            for (int slot = 0; slot < LibertyPluginHost::effectSlotsPerTrack; ++slot)
                if (!host.hasEffectForTrackSlot(audioLane, slot)
                    && !one.hasEffect(audioLane * 8 + slot)) return slot;
        }
        return -1;
    }

    void loadOneKnob(LibertyOneKnobRack::Type type)
    {
        // When an empty slot invoked the browser, honor that exact slot ID.
        // The ordinary browser continues using the next free insert.
        const int insertSlot = targetEffectSlot>=0?targetEffectSlot:firstFreeOneKnobInsertSlot();
        const int instrumentLane = targetEffectSlot>=0
            ?(targetIsInstrument?targetEffectLane:-1):selectedInstrumentTrack();
        const int audioLane = targetEffectSlot>=0
            ?(targetIsInstrument?-1:targetEffectLane):selectedAudioTrack();
        if (insertSlot < 0 ||
            (instrumentLane>=0 && instrumentLane>=owner.getInstrumentTrackCount()) ||
            (audioLane>=0 && audioLane>=owner.getAudioTrackCount()))
        {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Liberty - One Knob",
                                                   (instrumentLane < 0 && audioLane < 0)
                                                       ? "Sélectionne une piste Audio ou Instrument pour charger ce One Knob."
                                                       : "Les 8 inserts de cette piste sont déjà occupés.",
                                                   "OK");
            return;
        }
        const int key = instrumentLane >= 0 ? 100000 + instrumentLane * 8 + insertSlot : audioLane * 8 + insertSlot;
        auto& manager = LibertyOneKnobManager::instance();
        auto& host = LibertyPluginHost::instance();
        if((instrumentLane>=0?host.hasEffectForInstrumentTrackSlot(instrumentLane,insertSlot)
                             :host.hasEffectForTrackSlot(audioLane,insertSlot)) ||
           manager.hasEffect(key))
        {
            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::WarningIcon,"Liberty - One Knob",
                "Ce slot FX est déjà occupé.","OK");
            return;
        }
        manager.setEffect(key, type);
        if(targetEffectSlot>=0)
        {
            toggleButton.setToggleState(false,juce::dontSendNotification);
            setBrowserOpen(false);
        }
        manager.showEditor(key);
        refreshPluginStatus();
        refreshLibertyMixConsole(&owner);
        owner.repaint();
    }

    void clearOneKnob()
    {
        auto& manager = LibertyOneKnobManager::instance();
        const int instrumentLane = selectedInstrumentTrack();
        const int audioLane = selectedAudioTrack();
        for (int slot = 0; slot < LibertyPluginHost::effectSlotsPerTrack; ++slot)
        {
            const int key = instrumentLane >= 0 ? 100000 + instrumentLane * 8 + slot
                                               : (audioLane >= 0 ? audioLane * 8 + slot : -1);
            if (key >= 0 && manager.hasEffect(key))
            {
                manager.clearEffect(key);
                refreshPluginStatus();
                refreshLibertyMixConsole(&owner);
                owner.repaint();
                return;
            }
        }
        if (instrumentLane < 0 && audioLane < 0)
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Liberty - One Knob",
                                                   "Sélectionne une piste Audio ou Instrument pour retirer le One Knob.", "OK");
    }

    void openSpliceSounds()
    {
        refreshPlugins();
        const juce::PluginDescription* splice = nullptr;
        for (const auto& d : pluginDescriptions)
        {
            const auto name = d.name.trim();
            if (name.equalsIgnoreCase("Splice Sounds")
                || (name.containsIgnoreCase("Splice") && name.containsIgnoreCase("Sounds")))
            {
                splice = &d;
                if (d.isInstrument) break;
            }
        }

        if (splice == nullptr)
        {
            spliceInfo.setText("Splice Sounds non détecté - lancement du scan AU + VST3...", juce::dontSendNotification);
            scanPlugins();
            return;
        }

        if (!splice->isInstrument)
        {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Liberty - Splice Sounds",
                "Liberty a trouvé un plugin Splice, mais pas l'instrument Splice Sounds. Vérifie que Splice Sounds est installé et rescanné.", "OK");
            return;
        }

        const int lane = selectedInstrumentTrack();
        if (lane < 0)
        {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Liberty - Splice Sounds",
                "Sélectionne d'abord une piste Instrument : Splice Sounds est un plugin instrument.", "OK");
            return;
        }

        loadPluginDescription(*splice);
    }

    void loadSelectedPlugin() { if (const auto* d = selectedPluginDescription()) loadPluginDescription(*d); }
    void openLoadedPluginEditor()
    {
        if (const auto* d = selectedPluginDescription(); d != nullptr && d->isInstrument)
        {
            const int lane = selectedInstrumentTrack();
            if (lane < 0)
            {
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Liberty - Instrument",
                                                       "Sélectionne une piste Instrument pour ouvrir son interface.", "OK");
                return;
            }
            LibertyPluginHost::instance().showInstrumentEditorForTrack(lane);
            return;
        }
        const int track = selectedAudioTrack();
        if (track < 0)
        {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Liberty - Effet",
                                                   "Sélectionne une piste Audio pour ouvrir son effet.", "OK");
            return;
        }
        auto& host = LibertyPluginHost::instance();
        for (int slot = 0; slot < LibertyPluginHost::effectSlotsPerTrack; ++slot)
            if (host.hasEffectForTrackSlot(track, slot)) { host.showEditorForTrackSlot(track, slot); return; }
    }
    void unloadPlugin()
    {
        if (const auto* d = selectedPluginDescription(); d != nullptr && d->isInstrument)
        {
            const int lane = selectedInstrumentTrack();
            if (lane < 0)
            {
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Liberty - Instrument",
                                                       "Sélectionne une piste Instrument pour retirer son instrument.", "OK");
                return;
            }
            LibertyPluginHost::instance().unloadInstrumentForTrack(lane);
        }
        else
        {
            const int track = selectedAudioTrack();
            if (track < 0)
            {
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Liberty - Effet",
                                                       "Sélectionne une piste Audio pour retirer son effet.", "OK");
                return;
            }
            auto& host = LibertyPluginHost::instance();
            for (int slot = 0; slot < LibertyPluginHost::effectSlotsPerTrack; ++slot)
                if (host.hasEffectForTrackSlot(track, slot)) { host.unloadEffectForTrackSlot(track, slot); break; }
        }
        refreshPluginStatus(); owner.repaint();
    }

    void refreshPluginStatus()
    {
        if (scanning.load()) { pluginStatus.setText(getScanStatus(), juce::dontSendNotification); return; }
        auto& host = LibertyPluginHost::instance();
        const int logical = owner.selectedTrack;
        const int audioCount = owner.getAudioTrackCount();
        const int firstInstrument = audioCount + owner.getMidiTrackCount();
        juce::String text;
        if (logical >= 0 && logical < audioCount)
        {
            auto& oneKnob = LibertyOneKnobManager::instance();
            for (int slot = 0; slot < LibertyPluginHost::effectSlotsPerTrack; ++slot)
            {
                const int oneKey = logical * 8 + slot;
                if (host.hasEffectForTrackSlot(logical, slot))
                    text << "FX" << (slot + 1) << ": " << host.getEffectNameForTrackSlot(logical, slot) << "   ";
                else if (oneKnob.hasEffect(oneKey))
                    text << "FX" << (slot + 1) << ": " << oneKnob.getName(oneKey) << "   ";
            }
        }
        else if (logical >= firstInstrument && logical < firstInstrument + owner.getInstrumentTrackCount())
        {
            const int instrumentLane = logical - firstInstrument;
            if (host.hasInstrumentForTrack(instrumentLane)) text << "INST: " << host.getInstrumentNameForTrack(instrumentLane) << "   ";
            auto& oneKnob = LibertyOneKnobManager::instance();
            for (int slot = 0; slot < LibertyPluginHost::effectSlotsPerTrack; ++slot)
            {
                const int oneKey = 100000 + instrumentLane * 8 + slot;
                if (host.hasEffectForInstrumentTrackSlot(instrumentLane, slot))
                    text << "FX" << (slot + 1) << ": " << host.getEffectNameForInstrumentTrackSlot(instrumentLane, slot) << "   ";
                else if (oneKnob.hasEffect(oneKey))
                    text << "FX" << (slot + 1) << ": " << oneKnob.getName(oneKey) << "   ";
            }
        }
        if (batchArtworkRunning.load()
            || juce::Time::getMillisecondCounter() < batchArtworkStatusUntilMs) return;
        if (text.isEmpty()) text = juce::String(pluginDescriptions.size()) + " plugins  " + juce::String(favouriteKeys.size()) + " favoris";
        pluginStatus.setText(text, juce::dontSendNotification);
    }

    void timerCallback() override
    {
        if (stopped.load()) return;
        if (category == Category::plugins && pluginSearchPending && !scanning.load()
            && juce::Time::getMillisecondCounter() - pluginSearchChangedAt >= 200)
            rebuildPluginTree();
        if (category == Category::audio && previewTransport.isPlaying()) repaint();
        if (category == Category::audio && sampleSearchPending && !sampleSearchRunning.load()
            && juce::Time::getMillisecondCounter() - sampleSearchChangedAt >= 350)
        {
            sampleSearchPending = false;
            showMusicalFilterResults();
        }
        const int width = juce::jlimit(260, juce::jmax(260, owner.getWidth() - 220), getLibertyPreferredBrowserWidth(&owner));
        const int panelX = juce::jmax(0, owner.getWidth() - width);
        if (browserOpen)
        {
            const auto wanted = juce::Rectangle<int>(panelX, topBarHeight, juce::jmin(width, owner.getWidth()), juce::jmax(1, owner.getHeight() - topBarHeight));
            if (getBounds() != wanted) setBounds(wanted);
            if (!isVisible()) setVisible(true);
        }

        // The browser starts after PERFORMANCE, never on top of it.
        const auto buttonBounds = juce::Rectangle<int>(LibertyPageNavigation::browserX,
                                                     LibertyPageNavigation::top,
                                                     LibertyPageNavigation::browserWidth,
                                                     LibertyPageNavigation::height);
        if (toggleButton.getBounds() != buttonBounds) toggleButton.setBounds(buttonBounds);

        if (category == Category::plugins || category == Category::splice)
        {
            if (scanning.load())
            {
                const auto progressText = getScanStatus();
                if (category == Category::plugins)
                {
                    pluginStatus.setText(progressText, juce::dontSendNotification);
                    scanPluginsButton.setButtonText(progressText);
                    scanPluginsButton.setTooltip(progressText);
                }
                else spliceInfo.setText(progressText, juce::dontSendNotification);
            }
            else if (scanFinishedPending.exchange(false))
            {
                if (scanThread.joinable()) scanThread.join();
                refreshPlugins();
                scanPluginsButton.setButtonText("SCANNER LES PLUGINS");
                scanPluginsButton.setTooltip("Relancer le scan AU et VST3");
                // Only auto-fetch from a curated catalogue. Never instantiate plugins for artwork.
                const auto artworkManifest = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                    .getChildFile("Liberty").getChildFile("PluginArtworkCatalogue").getChildFile("OfficialArtwork.tsv");
                if (artworkManifest.existsAsFile())
                {
                    const auto entries = juce::StringArray::fromLines(artworkManifest.loadFileAsString());
                    bool hasSources = false;
                    for (const auto& entry : entries)
                        if (!entry.trim().isEmpty() && !entry.startsWithChar('#') && entry.containsChar('\t'))
                        {
                            hasSources = true;
                            break;
                        }
                    if (hasSources) refreshOfficialArtwork();
                }
                for (auto* b : { &scanPluginsButton, &blacklistButton, &clearBlacklistButton, &favouritePluginButton,
                                 &loadPluginButton, &openPluginButton, &unloadPluginButton }) b->setEnabled(true);
                spliceScanButton.setEnabled(true);
                if (category == Category::splice)
                {
                    const bool found = std::any_of(pluginDescriptions.begin(), pluginDescriptions.end(), [](const auto& d)
                    {
                        return d.isInstrument && d.name.containsIgnoreCase("Splice") && d.name.containsIgnoreCase("Sounds");
                    });
                    spliceInfo.setText(found ? "Splice Sounds détecté - prêt à ouvrir."
                                             : "Splice Sounds non détecté après le scan AU + VST3.", juce::dontSendNotification);
                    spliceOpenPluginButton.setEnabled(found);
                }
            }
            else if (category == Category::plugins) refreshPluginStatus();
        }
    }

    MainComponent& owner;
    juce::TimeSliceThread thread { "Liberty Browser" };
    BrowserFileFilter filter;
    juce::DirectoryContentsList directoryList;
    juce::FileTreeComponent fileTree;
    juce::TreeView pluginTree;
    juce::TextButton thumbnailModeButton, importThumbnailButton, chooseLibraryButton, saveFolderButton, openFavouriteButton, pluginFiltersButton;
    LibertyPluginSearchEditor pluginSearch;
    std::unique_ptr<PluginTreeItem> pluginRoot;
    juce::Array<juce::PluginDescription> pluginDescriptions;
    std::vector<PluginTreeItem*> keyboardResults; // owned by pluginRoot, cleared on rebuild
    juce::StringArray favouriteKeys, recentKeys;
    juce::TextButton toggleButton, closeButton, filesButton, audioButton, midiButton, presetsButton, pluginsButton, spliceButton, homeButton, parentFolderButton, previewPlay, previewStop;
    juce::Slider previewVolume;
    juce::ToggleButton previewAuto;
    juce::Label previewInfo, samplePackInfo;
    juce::TextButton browsePacksButton, musicFilterButton;
    juce::TextEditor sampleNameSearch;
    juce::String sampleNameQuery;
    juce::Array<juce::File> sampleIndex;
    juce::File sampleIndexLibrary;
    bool sampleIndexComplete = false;
    int sampleFilterMask = 0;
    bool sampleSearchPending = false;
    juce::uint32 sampleSearchChangedAt = 0;
    bool pluginSearchPending = false;
    juce::uint32 pluginSearchChangedAt = 0;
    juce::AudioFormatManager previewFormats;
    juce::AudioThumbnailCache previewThumbnailCache { 32 };
    juce::AudioThumbnail previewThumbnail { 256, previewFormats, previewThumbnailCache };
    std::unique_ptr<juce::AudioFormatReaderSource> previewReader;
    juce::AudioTransportSource previewTransport;
    juce::AudioSourcePlayer previewPlayer;
    juce::TextButton scanPluginsButton, blacklistButton, clearBlacklistButton, favouritePluginButton, loadPluginButton, openPluginButton, unloadPluginButton;
    OneKnobButton chorusButton, flangerButton, phaserButton, tremoloButton, reverbButton, delayButton, driveButton, compressorButton, saturationButton, widthButton, filterButton, doublerButton, exciterButton, deEsserButton, gateButton, bassBoostButton, airButton, punchButton, softClipButton, clearOneKnobButton;
    juce::Label pluginStatus, spliceTitle, spliceInfo, artworkStatsLabel;
    juce::TextButton spliceOpenPluginButton, spliceScanButton;
    juce::File rootDirectory;
    Category category = Category::files;
    ArtworkView artworkView = ArtworkView::all;
    PluginFormatFilter pluginFormatFilter = PluginFormatFilter::all;
    PluginTypeFilter pluginTypeFilter = PluginTypeFilter::all;
    QuickView quickView = QuickView::all;
    PluginUsage pluginUsageFilter = PluginUsage::all;
    bool groupByUsage = true;
    std::map<juce::String, PluginUsage> pluginFamilyOverrides;
    bool familyOverridesReadOnly = false;
    std::map<juce::String, juce::StringArray> pluginCollections;
    bool collectionsReadOnly = false;
    juce::String selectedCollection;
    juce::String selectedManufacturer;
    int artworkTotalCount = 0, artworkPresentCount = 0;
    std::atomic<bool> stopped { false }, scanning { false }, scanFinishedPending { false };
    mutable juce::CriticalSection scanStatusLock;
    juce::String scanStatusText;
    std::thread scanThread, sampleSearchThread, artworkThread, captureThread, batchArtworkThread;
    std::atomic<bool> batchArtworkRunning { false }, batchArtworkCancel { false };
    juce::uint32 batchArtworkStatusUntilMs = 0;
    std::atomic<bool> captureRunning { false };
    std::atomic<bool> artworkRefreshing { false };
    std::atomic<bool> sampleSearchRunning { false };
    bool browserOpen = false, hostExpanded = false;
    bool targetIsInstrument=false;
    int targetEffectLane=-1,targetEffectSlot=-1;
    juce::Rectangle<int> originalWindowBounds;
};

std::map<MainComponent*, std::unique_ptr<BrowserPanel>> browsers;

class Bootstrap final : private juce::Timer
{
public:
    Bootstrap() { startTimerHz(10); }
    ~Bootstrap() override { shutdown(); }
    void shutdown()
    {
        stopTimer();
        for (auto& item : browsers) if (item.second) item.second->shutdown();
        browsers.clear();
    }
private:
    void timerCallback() override
    {
        auto& desktop = juce::Desktop::getInstance();
        for (int i = 0; i < desktop.getNumComponents(); ++i)
            if (auto* window = dynamic_cast<juce::DocumentWindow*>(desktop.getComponent(i)))
                if (auto* main = dynamic_cast<MainComponent*>(window->getContentComponent()))
                    if (browsers.find(main) == browsers.end()) { browsers.emplace(main, std::make_unique<BrowserPanel>(*main)); resizeLibertyBrowserResizeController(main); stopTimer(); return; }
    }
};

Bootstrap bootstrap;
}

void ensureLibertyBrowserController(MainComponent* main)
{
    if (main == nullptr) return;
    auto it = browsers.find(main);
    if (it == browsers.end())
    {
        it = browsers.emplace(main, std::make_unique<BrowserPanel>(*main)).first;
        resizeLibertyBrowserResizeController(main);
    }
    if (it != browsers.end() && it->second != nullptr)
        it->second->ensureToggleVisible();
}


int getLibertyBrowserReservedWidth(MainComponent* owner)
{
    if(owner==nullptr)return 0;
    const auto it=browsers.find(owner);
    if(it==browsers.end()||!it->second||!it->second->isOpenForMixer())return 0;
    return juce::jlimit(260,juce::jmax(260,owner->getWidth()-220),
                       getLibertyPreferredBrowserWidth(owner));
}

void showLibertyEffectBrowserForSlot(MainComponent* owner,bool instrument,int lane,int slot)
{
    if(owner==nullptr)return;
    ensureLibertyBrowserController(owner);
    const auto it=browsers.find(owner);
    if(it!=browsers.end()&&it->second)
        it->second->openForEffectSlot(instrument,lane,slot);
}

void shutdownLibertyBrowserController()
{
    bootstrap.shutdown();
}
