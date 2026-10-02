// SPDX-License-Identifier: GPL-3.0-only
#include "ui/PreviewPanel.h"

#include "ui/Theme.h"

#include <cmath>

namespace asma::app {

namespace {

constexpr int kControlHeight = 28;
constexpr int kGap = 10;
constexpr int kPadX = 16, kPadY = 12;
constexpr int kTitleHeight = 20;

// Play when stopped, stop when playing; amber while it plays.
class PlayButton final : public juce::Button {
public:
    PlayButton() : juce::Button("Play") { setTitle("Play"); }
    void setPlaying(bool playing)
    {
        if (playing == playing_) return;
        playing_ = playing;
        setTitle(playing ? "Stop" : "Play");
        repaint();
    }
    void paintButton(juce::Graphics& g, bool highlighted, bool down) override
    {
        const auto r = getLocalBounds().toFloat().reduced(0.5f);
        g.setColour(playing_ ? theme::amber.withAlpha(0.12f) : (highlighted || down ? theme::raised : theme::panel));
        g.fillRoundedRectangle(r, theme::kRadius);
        g.setColour(playing_ ? theme::amber : theme::border);
        g.drawRoundedRectangle(r, theme::kRadius, 1.0f);
        const auto c = r.getCentre();
        if (playing_) {
            g.setColour(theme::amber);
            g.fillRoundedRectangle(juce::Rectangle<float>(10.0f, 10.0f).withCentre(c), 1.0f);
        } else {
            juce::Path p;
            p.addTriangle(c.x - 4.0f, c.y - 6.0f, c.x - 4.0f, c.y + 6.0f, c.x + 6.0f, c.y);
            g.setColour(theme::text);
            g.fillPath(p);
        }
    }

private:
    bool playing_ = false;
};

} // namespace

const juce::StringArray& PreviewPanel::keys()
{
    static const juce::StringArray k{"C",  "C#",  "D",  "D#",  "E",  "F",  "F#",  "G",  "G#",  "A",  "A#",  "B",
                                     "Cm", "C#m", "Dm", "D#m", "Em", "Fm", "F#m", "Gm", "G#m", "Am", "A#m", "Bm"};
    return k;
}

juce::String PreviewPanel::fileLine(const std::string& folder, int sampleRate, int channels, double seconds)
{
    juce::StringArray parts;
    if (!folder.empty()) parts.add(juce::String::fromUTF8(folder.c_str()).replace("/", " / "));
    if (sampleRate > 0) {
        const long long tenths = std::llround(sampleRate / 100.0);
        parts.add(juce::String(tenths / 10) + (tenths % 10 ? "." + juce::String(tenths % 10) : juce::String()) + " kHz");
    }
    if (channels == 1) parts.add("mono");
    if (channels == 2) parts.add("stereo");
    if (seconds > 0.0) parts.add(juce::String::fromUTF8(secondsText(seconds).c_str()));
    return parts.joinIntoString(juce::String::fromUTF8(" · "));
}

PreviewPanel::PreviewPanel()
    : direction_({juce::String::fromUTF8("→"), juce::String::fromUTF8("←"), juce::String::fromUTF8("↔")},
                 true, {"Forward", "Reverse", "Ping-pong"}),
      loop_({"Loop auto", "On", "Off"}, false, {"Loop when the sample is a loop", "Always loop", "Play once"})
{
    auto* play = new PlayButton();
    play_.reset(play);
    play->onClick = [this] {
        if (onPlayStop) onPlayStop();
    };
    addAndMakeVisible(*play_);
    addAndMakeVisible(waveform_);
    waveform_.onPlay = [this] {
        if (onPlayStop) onPlayStop();
    };
    waveform_.onTrimChanged = [this](double start, double end) {
        edits_.trimStart = start;
        edits_.trimEnd = end;
        editsChanged();
    };
    direction_.onChange = [this](int i) {
        edits_.direction = static_cast<audio::Direction>(i);
        editsChanged();
    };
    loop_.onChange = [this](int i) {
        edits_.loop = static_cast<audio::LoopMode>(i);
        editsChanged();
    };
    reset_.getProperties().set("asma.quiet", true);
    reset_.getProperties().set("asma.size", 12.0f);
    reset_.onClick = [this] {
        setEdits({});
        editsChanged();
    };
    for (auto* c : {&tempo_, &gain_}) c->setClickingTogglesState(true);
    tempo_.onClick = [this] {
        if (onTempoSync) onTempoSync(tempo_.getToggleState());
    };
    gain_.onClick = [this] {
        if (onGainMatch) onGainMatch(gain_.getToggleState());
    };
    key_.onClick = [this] {
        juce::PopupMenu menu;
        menu.addItem(1, "Off", true, !key_.getToggleState());
        menu.addSeparator();
        for (int i = 0; i < keys().size(); ++i)
            menu.addItem(i + 2, keys()[i], true, key_.getToggleState() && keys()[i].toStdString() == keyName_);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&key_), [this](int result) {
            if (result > 0) chooseKey(result - 1);
        });
    };
    start_.onClick = [this] {
        const double next = quantise_ < 1.0 ? 1.0 : (quantise_ < 4.0 ? 4.0 : 0.0);
        setQuantise(next);
        if (onQuantise) onQuantise(next);
    };
    for (juce::Component* c : {static_cast<juce::Component*>(&direction_), static_cast<juce::Component*>(&loop_),
                               static_cast<juce::Component*>(&reset_), static_cast<juce::Component*>(&tempo_),
                               static_cast<juce::Component*>(&key_), static_cast<juce::Component*>(&gain_),
                               static_cast<juce::Component*>(&start_)})
        addAndMakeVisible(c);
    gain_.setDot(theme::amber, false);
    setKey(false, {}, {});
    setQuantise(0.0);
    setFile({}, {});
}

void PreviewPanel::setFile(const juce::String& name, const juce::String& line)
{
    name_ = name;
    line_ = line;
    const bool any = name.isNotEmpty();
    for (juce::Component* c : {static_cast<juce::Component*>(play_.get()), static_cast<juce::Component*>(&direction_),
                               static_cast<juce::Component*>(&loop_)})
        c->setEnabled(any);
    updateReset();
    repaint();
}

void PreviewPanel::setEdits(const audio::Edits& edits)
{
    edits_ = edits;
    waveform_.setTrim(edits.trimStart, edits.trimEnd);
    direction_.setSelected(static_cast<int>(edits.direction), juce::dontSendNotification);
    loop_.setSelected(static_cast<int>(edits.loop), juce::dontSendNotification);
    updateReset();
}

void PreviewPanel::updateReset()
{
    reset_.setEnabled(name_.isNotEmpty() && (edits_.changesAudio() || edits_.loop != audio::LoopMode::Auto));
}

void PreviewPanel::editsChanged()
{
    updateReset();
    if (onEditsChanged) onEditsChanged(edits_);
}

void PreviewPanel::setPlaying(bool playing) { static_cast<PlayButton*>(play_.get())->setPlaying(playing); }

void PreviewPanel::setTempo(bool on, const ChipText& chip)
{
    tempo_.setToggleState(on, juce::dontSendNotification);
    tempo_.setDot(theme::colourFor(chip.tone), on && chip.tone != Tone::Muted);
    tempo_.setDetail(juce::String::fromUTF8(chip.text.c_str()), theme::colourFor(chip.tone));
    resized();
}

void PreviewPanel::setKey(bool on, const std::string& key, const std::string& status)
{
    keyName_ = key;
    key_.setToggleState(on, juce::dontSendNotification);
    key_.setDot(theme::amber, on);
    juce::String detail = on && !key.empty() ? juce::String(key) : juce::String("off");
    if (on && !status.empty()) detail << " " << juce::String::fromUTF8(status.c_str());
    key_.setDetail(detail, on ? theme::text : theme::muted);
    resized();
}

void PreviewPanel::chooseKey(int item)
{
    if (item <= 0 || item > keys().size()) {
        setKey(false, keyName_, {});
        if (onKeySync) onKeySync(std::nullopt);
        return;
    }
    const std::string key = keys()[item - 1].toStdString();
    setKey(true, key, {});
    if (onKeySync) onKeySync(key);
}

void PreviewPanel::setGainMatch(bool on)
{
    gain_.setToggleState(on, juce::dontSendNotification);
    gain_.setDot(theme::amber, on);
}

void PreviewPanel::setQuantise(double beats)
{
    quantise_ = beats;
    start_.setToggleState(beats > 0.0, juce::dontSendNotification);
    start_.setDot(theme::amber, beats > 0.0);
    start_.setDetail(beats >= 4.0 ? "bar" : (beats >= 1.0 ? "beat" : "now"), beats > 0.0 ? theme::text : theme::muted);
    resized();
}

void PreviewPanel::paint(juce::Graphics& g)
{
    g.setColour(theme::border);
    g.fillRect(separator_);
    auto title = getLocalBounds().reduced(kPadX, kPadY).removeFromTop(kTitleHeight);
    if (name_.isEmpty()) {
        g.setFont(theme::font(theme::Face::Text, 13.0f));
        g.setColour(theme::muted);
        g.drawText("Select a sample to hear it", title, juce::Justification::centredLeft, true);
        return;
    }
    const auto nameFont = theme::font(theme::Face::Heading, 15.0f);
    const int nameWidth = std::min(title.getWidth(),
                                   static_cast<int>(std::ceil(juce::GlyphArrangement::getStringWidth(nameFont, name_))));
    g.setFont(nameFont);
    g.setColour(theme::text);
    g.drawText(name_, title.removeFromLeft(nameWidth), juce::Justification::centredLeft, true);
    title.removeFromLeft(12);
    g.setFont(theme::font(theme::Face::Mono, 11.0f));
    g.setColour(theme::muted);
    g.drawText(line_, title.withTrimmedTop(3), juce::Justification::centredLeft, true);
}

int PreviewPanel::controlRows(int width) const
{
    // The controls in order with their widths; a row breaks where they no longer fit.
    const int widths[] = {32, direction_.idealWidth(), loop_.idealWidth(), 90, 1, tempo_.idealWidth(),
                          key_.idealWidth(), gain_.idealWidth(), start_.idealWidth()};
    int rows = 1, x = 0;
    for (const int w : widths) {
        if (x > 0 && x + w > width) {
            ++rows;
            x = 0;
        }
        x += w + kGap;
    }
    return rows;
}

void PreviewPanel::layoutControls(juce::Rectangle<int> area)
{
    struct Item {
        juce::Component* c;
        int w;
    };
    const Item items[] = {{play_.get(), 32},
                          {&direction_, direction_.idealWidth()},
                          {&loop_, loop_.idealWidth()},
                          {&reset_, 90},
                          {nullptr, 1}, // the line between edits and settings
                          {&tempo_, tempo_.idealWidth()},
                          {&key_, key_.idealWidth()},
                          {&gain_, gain_.idealWidth()},
                          {&start_, start_.idealWidth()}};
    int x = area.getX(), y = area.getY();
    for (const auto& item : items) {
        if (x > area.getX() && x + item.w > area.getRight()) {
            x = area.getX();
            y += kControlHeight + kGap;
        }
        if (item.c) item.c->setBounds(x, y, item.w, kControlHeight);
        else separator_ = {x, y + 3, 1, kControlHeight - 6};
        x += item.w + kGap;
    }
    // A line that ends a row parts nothing.
    if (tempo_.getY() != separator_.getY() - 3) separator_ = {};
}

void PreviewPanel::resized()
{
    auto area = getLocalBounds().reduced(kPadX, kPadY);
    area.removeFromTop(kTitleHeight + kGap);
    const int rows = controlRows(area.getWidth());
    const int controls = rows * kControlHeight + (rows - 1) * kGap;
    layoutControls(area.removeFromBottom(controls));
    area.removeFromBottom(kGap);
    waveform_.setBounds(area);
}

} // namespace asma::app
