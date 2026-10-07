// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <optional>

namespace asma::app {

// A name to give: a title, the field, why a name is refused, Cancel and Save.
// Save, or Return in the field, gives the name only while nothing refuses it;
// a refusal shows under the field in red. Closes its callout box when done.
class NamePopover : public juce::Component {
public:
    // Nothing: the name is fine. Empty text: refused without a word (an
    // empty field). Otherwise what to tell the user.
    using Refusal = std::function<std::optional<juce::String>(const juce::String& name)>;
    NamePopover(const juce::String& title, const juce::String& initial, Refusal refusal,
                std::function<void(const juce::String& name)> onSave);

    juce::TextEditor& field() { return field_; }
    juce::Button& saveButton() { return save_; }
    juce::Button& cancelButton() { return cancel_; }
    juce::String refusalText() const { return refusalText_; }

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void check();
    void save();
    void close();

    juce::String title_;
    Refusal refusal_;
    std::function<void(const juce::String&)> onSave_;
    juce::TextEditor field_;
    juce::TextButton cancel_{"Cancel"}, save_{"Save"};
    juce::String refusalText_;
};

} // namespace asma::app
