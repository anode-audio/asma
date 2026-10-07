// SPDX-License-Identifier: GPL-3.0-only
#include "ui/NamePopover.h"

#include "ui/Theme.h"

namespace asma::app {

namespace {

constexpr int kPad = 14;
constexpr int kTitle = 22;

} // namespace

NamePopover::NamePopover(const juce::String& title, const juce::String& initial, Refusal refusal,
                         std::function<void(const juce::String&)> onSave)
    : title_(title), refusal_(std::move(refusal)), onSave_(std::move(onSave))
{
    setTitle(title);
    field_.setTitle("Name");
    field_.setFont(theme::font(theme::Face::Text, 13.0f));
    field_.setIndents(8, 6);
    field_.setText(initial, false);
    field_.onTextChange = [this] { check(); };
    field_.onReturnKey = [this] { save(); };
    field_.onEscapeKey = [this] { close(); };
    addAndMakeVisible(field_);
    cancel_.onClick = [this] { close(); };
    addAndMakeVisible(cancel_);
    save_.getProperties().set("asma.accent", true);
    save_.onClick = [this] { save(); };
    addAndMakeVisible(save_);
    setSize(300, 150);
    check();
}

void NamePopover::check()
{
    const auto refused = refusal_ ? refusal_(field_.getText()) : std::nullopt;
    refusalText_ = refused.value_or(juce::String());
    save_.setEnabled(!refused);
    field_.setColour(juce::TextEditor::outlineColourId, refusalText_.isNotEmpty() ? theme::refused : theme::border);
    field_.setColour(juce::TextEditor::focusedOutlineColourId, refusalText_.isNotEmpty() ? theme::refused : theme::amber);
    repaint();
}

void NamePopover::save()
{
    check();
    if (!save_.isEnabled()) return;
    if (onSave_) onSave_(field_.getText());
    close();
}

void NamePopover::close()
{
    if (auto* box = findParentComponentOfClass<juce::CallOutBox>()) box->dismiss();
}

void NamePopover::paint(juce::Graphics& g)
{
    auto area = getLocalBounds().reduced(kPad);
    g.setFont(theme::font(theme::Face::Heading, 13.0f));
    g.setColour(theme::text);
    g.drawText(title_, area.removeFromTop(kTitle), juce::Justification::centredLeft, false);
    g.setFont(theme::font(theme::Face::Text, 11.0f));
    g.setColour(theme::muted);
    g.drawText("Name", area.removeFromTop(16), juce::Justification::bottomLeft, false);
    if (refusalText_.isEmpty()) return;
    g.setFont(theme::font(theme::Face::Text, 12.0f));
    g.setColour(theme::refusedText);
    g.drawText(refusalText_, field_.getBounds().translated(0, field_.getHeight() + 4).withHeight(16),
               juce::Justification::centredLeft, true);
}

void NamePopover::resized()
{
    auto area = getLocalBounds().reduced(kPad);
    area.removeFromTop(kTitle + 18);
    field_.setBounds(area.removeFromTop(28));
    auto buttons = area.removeFromBottom(28);
    save_.setBounds(buttons.removeFromRight(64));
    buttons.removeFromRight(8);
    cancel_.setBounds(buttons.removeFromRight(72));
}

} // namespace asma::app
