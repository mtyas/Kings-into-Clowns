#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "PoeticLookAndFeel.h"
#include "../Core/PresetManager.h"
#include <functional>

namespace ric::ui {

// "The Library": every patch with its poem; filter by category or by any word
class PresetBrowserModal : public juce::Component,
                           private juce::ListBoxModel {
public:
    PresetBrowserModal(ric::core::PresetManager& presetManager,
                       std::function<void()> onPresetChanged)
        : presetManager_(presetManager),
          onPresetChanged_(std::move(onPresetChanged)) {
        setWantsKeyboardFocus(true);

        presetListBox_.setModel(this);
        presetListBox_.setRowHeight(46);
        presetListBox_.setOutlineThickness(0);
        addAndMakeVisible(presetListBox_);

        searchEditor_.setTextToShowWhenEmpty("search by name, category or line of verse", Palette::kFaint);
        searchEditor_.setFont(Fonts::serif(14.0f, true));
        searchEditor_.setIndents(12, 7);
        searchEditor_.onTextChange = [this]() { rebuildFilteredList(); };
        searchEditor_.onEscapeKey = [this]() { setVisible(false); };
        addAndMakeVisible(searchEditor_);

        saveNameEditor_.setTextToShowWhenEmpty("name a new patch", Palette::kFaint);
        saveNameEditor_.setFont(Fonts::sans(13.0f));
        saveNameEditor_.setIndents(12, 7);
        addAndMakeVisible(saveNameEditor_);

        poemEditor_.setTextToShowWhenEmpty("and give it a line of verse (optional)", Palette::kFaint);
        poemEditor_.setFont(Fonts::serif(13.0f, true));
        poemEditor_.setIndents(12, 7);
        addAndMakeVisible(poemEditor_);

        saveBtn_.setButtonText("Keep");
        saveBtn_.setColour(juce::TextButton::buttonOnColourId, Palette::kJade);
        saveBtn_.setToggleState(true, juce::dontSendNotification);
        saveBtn_.onClick = [this]() { saveCurrent(); };
        addAndMakeVisible(saveBtn_);

        deleteBtn_.setButtonText("Let go");
        deleteBtn_.setColour(juce::TextButton::buttonOnColourId, Palette::kCoral);
        deleteBtn_.setTooltip("Delete the selected user patch");
        deleteBtn_.onClick = [this]() {
            if (presetManager_.deleteCurrentUserPreset()) {
                rebuildFilteredList();
                if (onPresetChanged_) onPresetChanged_();
            }
        };
        addAndMakeVisible(deleteBtn_);

        importBtn_.setButtonText("Import");
        importBtn_.onClick = [this]() { importPreset(); };
        addAndMakeVisible(importBtn_);

        exportBtn_.setButtonText("Export");
        exportBtn_.onClick = [this]() { exportPreset(); };
        addAndMakeVisible(exportBtn_);

        closeBtn_.setButtonText("Close");
        closeBtn_.onClick = [this]() { setVisible(false); };
        addAndMakeVisible(closeBtn_);

        rebuildCategories();
        rebuildFilteredList();
    }

    void openBrowser() {
        presetManager_.rescanPresets();
        rebuildCategories();
        rebuildFilteredList();
        setVisible(true);
        toFront(true);
        searchEditor_.grabKeyboardFocus();
    }

    bool keyPressed(const juce::KeyPress& key) override {
        if (key == juce::KeyPress::escapeKey) { setVisible(false); return true; }
        return false;
    }

    //==========================================================================
    void resized() override {
        cardBounds_ = getLocalBounds().withSizeKeepingCentre(std::min(860, getWidth() - 80), std::min(560, getHeight() - 80));
        auto r = cardBounds_.reduced(26, 22);
        r.removeFromTop(48);

        auto left = r.removeFromLeft(190);
        r.removeFromLeft(18);
        int y = left.getY();
        for (auto& btn : categoryButtons_) {
            btn->setBounds(left.getX(), y, left.getWidth(), 28);
            y += 33;
        }

        auto io = left.removeFromBottom(28);
        importBtn_.setBounds(io.removeFromLeft(io.getWidth() / 2 - 4));
        io.removeFromLeft(8);
        exportBtn_.setBounds(io);

        searchEditor_.setBounds(r.removeFromTop(34));
        r.removeFromTop(10);

        auto bottom = r.removeFromBottom(34);
        r.removeFromBottom(10);
        presetListBox_.setBounds(r);

        closeBtn_.setBounds(bottom.removeFromRight(84));
        bottom.removeFromRight(8);
        deleteBtn_.setBounds(bottom.removeFromRight(84));
        bottom.removeFromRight(8);
        saveBtn_.setBounds(bottom.removeFromRight(84));
        bottom.removeFromRight(8);
        saveNameEditor_.setBounds(bottom.removeFromLeft(bottom.getWidth() * 2 / 5));
        bottom.removeFromLeft(8);
        poemEditor_.setBounds(bottom);
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colour(0xFF05060C).withAlpha(0.72f));
        const auto card = cardBounds_.toFloat();
        Draw::glow(g, card.getCentre(), card.getWidth() * 0.7f, Palette::kIris, 0.10f);
        Draw::card(g, card, 18.0f);

        auto head = cardBounds_.reduced(26, 18).removeFromTop(40);
        g.setFont(Fonts::serif(24.0f, true));
        g.setColour(Palette::kMoon);
        g.drawText("the library", head.removeFromLeft(220), juce::Justification::centredLeft);

        if (const auto* cur = presetManager_.getCurrentPreset()) {
            g.setFont(Fonts::serif(13.0f, true));
            g.setColour(Palette::kGold.withAlpha(0.9f));
            g.drawText("now playing:  " + cur->name, head, juce::Justification::centredRight);
        }
    }

    void mouseDown(const juce::MouseEvent& e) override {
        if (!cardBounds_.contains(e.getPosition())) setVisible(false);
    }

private:
    void rebuildCategories() {
        for (auto& b : categoryButtons_) removeChildComponent(b.get());
        categoryButtons_.clear();

        // Categories come from the presets themselves, so every filter shows something
        for (const auto& cat : presetManager_.getCategories()) {
            auto btn = std::make_unique<juce::TextButton>(cat);
            btn->setColour(juce::TextButton::buttonOnColourId, Palette::kGold);
            btn->onClick = [this, cat]() {
                selectedCategory_ = cat;
                rebuildFilteredList();
            };
            addAndMakeVisible(*btn);
            categoryButtons_.push_back(std::move(btn));
        }
        if (!presetManager_.getCategories().contains(selectedCategory_)) selectedCategory_ = "All";
        resized();
    }

    void rebuildFilteredList() {
        filteredIndices_.clear();
        const auto query = searchEditor_.getText().trim();
        const auto& all = presetManager_.getAllPresets();
        for (size_t i = 0; i < all.size(); ++i) {
            const auto& p = all[i];
            const bool catOk = selectedCategory_ == "All"
                            || (selectedCategory_ == "User" && !p.isFactory)
                            || p.category.equalsIgnoreCase(selectedCategory_);
            const bool textOk = query.isEmpty()
                             || p.name.containsIgnoreCase(query)
                             || p.category.containsIgnoreCase(query)
                             || p.poemLine.containsIgnoreCase(query);
            if (catOk && textOk) filteredIndices_.push_back(static_cast<int>(i));
        }

        for (auto& btn : categoryButtons_)
            btn->setToggleState(btn->getButtonText() == selectedCategory_, juce::dontSendNotification);

        presetListBox_.updateContent();
        presetListBox_.deselectAllRows();
        const int curIdx = presetManager_.getCurrentIndex();
        for (size_t r = 0; r < filteredIndices_.size(); ++r) {
            if (filteredIndices_[r] == curIdx) {
                presetListBox_.selectRow(static_cast<int>(r), false, true);
                break;
            }
        }

        const auto* cur = presetManager_.getCurrentPreset();
        deleteBtn_.setEnabled(cur != nullptr && !cur->isFactory);
        repaint();
    }

    int getNumRows() override { return static_cast<int>(filteredIndices_.size()); }

    void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override {
        if (rowNumber < 0 || rowNumber >= static_cast<int>(filteredIndices_.size())) return;
        const int presetIdx = filteredIndices_[static_cast<size_t>(rowNumber)];
        const auto& all = presetManager_.getAllPresets();
        if (presetIdx < 0 || presetIdx >= static_cast<int>(all.size())) return;
        const auto& p = all[static_cast<size_t>(presetIdx)];

        auto row = juce::Rectangle<float>(2.0f, 2.0f, static_cast<float>(width) - 4.0f, static_cast<float>(height) - 4.0f);
        if (rowIsSelected) {
            g.setColour(Palette::kGold.withAlpha(0.10f));
            g.fillRoundedRectangle(row, 10.0f);
            g.setColour(Palette::kGold.withAlpha(0.6f));
            g.drawRoundedRectangle(row, 10.0f, 1.0f);
        } else if (rowNumber % 2 == 1) {
            g.setColour(juce::Colours::white.withAlpha(0.02f));
            g.fillRoundedRectangle(row, 10.0f);
        }

        auto text = row.reduced(16.0f, 5.0f);
        auto meta = text.removeFromRight(170.0f);
        g.setFont(Fonts::sans(14.0f, rowIsSelected));
        g.setColour(Palette::kMoon);
        g.drawText(p.name, text.removeFromTop(text.getHeight() * 0.52f), juce::Justification::bottomLeft);
        g.setFont(Fonts::serif(12.0f, true));
        g.setColour(Palette::kMist);
        g.drawText(p.poemLine, text, juce::Justification::topLeft, true);

        g.setFont(Fonts::caps(8.5f));
        g.setColour(p.isFactory ? Palette::kFaint : Palette::kJade);
        g.drawText((p.category + (p.isFactory ? "" : "  -  yours")).toUpperCase(), meta, juce::Justification::centredRight);
    }

    void listBoxItemClicked(int row, const juce::MouseEvent&) override { loadRow(row); }

    void loadRow(int row) {
        if (row >= 0 && row < static_cast<int>(filteredIndices_.size())) {
            presetManager_.loadPreset(filteredIndices_[static_cast<size_t>(row)]);
            const auto* cur = presetManager_.getCurrentPreset();
            deleteBtn_.setEnabled(cur != nullptr && !cur->isFactory);
            if (onPresetChanged_) onPresetChanged_();
            repaint();
        }
    }

    void listBoxItemDoubleClicked(int row, const juce::MouseEvent& e) override {
        listBoxItemClicked(row, e);
        setVisible(false);
    }

    void returnKeyPressed(int lastRowSelected) override {
        loadRow(lastRowSelected);
        setVisible(false);
    }

    void saveCurrent() {
        juce::String name = saveNameEditor_.getText().trim();
        if (name.isEmpty()) {
            const auto* cur = presetManager_.getCurrentPreset();
            name = cur ? cur->name + " (variation)" : "Untitled patch";
        }
        const juce::String cat = (selectedCategory_ == "All" || selectedCategory_ == "User") ? "User" : selectedCategory_;
        const juce::String poem = poemEditor_.getText().trim().isEmpty() ? "A patch of my own." : poemEditor_.getText().trim();
        presetManager_.saveUserPreset(name, cat, poem);
        saveNameEditor_.clear();
        poemEditor_.clear();
        rebuildCategories();
        rebuildFilteredList();
        if (onPresetChanged_) onPresetChanged_();
    }

    void importPreset() {
        fileChooser_ = std::make_unique<juce::FileChooser>("Import a Kings into Clowns patch",
                                                           presetManager_.getUserPresetDirectory(), ric::core::PresetManager::kPresetPattern);
        juce::Component::SafePointer<PresetBrowserModal> safe(this);
        fileChooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [safe](const juce::FileChooser& fc) {
                                      if (safe == nullptr) return;
                                      const auto file = fc.getResult();
                                      if (file.existsAsFile()) {
                                          safe->presetManager_.importPresetFromFile(file);
                                          safe->rebuildCategories();
                                          safe->rebuildFilteredList();
                                          if (safe->onPresetChanged_) safe->onPresetChanged_();
                                      }
                                  });
    }

    void exportPreset() {
        const auto* cur = presetManager_.getCurrentPreset();
        const juce::String defaultName = juce::File::createLegalFileName(cur ? cur->name : "Patch") + ric::core::PresetManager::kPresetExtension;
        fileChooser_ = std::make_unique<juce::FileChooser>("Export this patch",
                                                           presetManager_.getUserPresetDirectory().getChildFile(defaultName),
                                                           ric::core::PresetManager::kPresetPattern);
        juce::Component::SafePointer<PresetBrowserModal> safe(this);
        fileChooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                      | juce::FileBrowserComponent::warnAboutOverwriting,
                                  [safe](const juce::FileChooser& fc) {
                                      if (safe == nullptr) return;
                                      const auto file = fc.getResult();
                                      if (file == juce::File{}) return;
                                      const auto* c = safe->presetManager_.getCurrentPreset();
                                      safe->presetManager_.savePresetToFile(file,
                                                                           c ? c->name : file.getFileNameWithoutExtension(),
                                                                           c ? c->category : "User",
                                                                           c ? c->poemLine : "Exported patch.");
                                  });
    }

    ric::core::PresetManager& presetManager_;
    std::function<void()> onPresetChanged_;
    juce::String selectedCategory_ = "All";
    std::vector<int> filteredIndices_;

    juce::Rectangle<int> cardBounds_;
    std::vector<std::unique_ptr<juce::TextButton>> categoryButtons_;
    juce::ListBox presetListBox_;
    juce::TextEditor searchEditor_;
    juce::TextEditor saveNameEditor_;
    juce::TextEditor poemEditor_;
    juce::TextButton saveBtn_;
    juce::TextButton deleteBtn_;
    juce::TextButton importBtn_;
    juce::TextButton exportBtn_;
    juce::TextButton closeBtn_;
    std::unique_ptr<juce::FileChooser> fileChooser_;
};

} // namespace ric::ui
