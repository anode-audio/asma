// SPDX-License-Identifier: GPL-3.0-only
#include "asma/audio/Voices.h"

#include <algorithm>
#include <cmath>

namespace asma::audio {

void VoicePool::prepare(int outputRate, int maxBlock)
{
    for (auto& v : voices_) v.head.prepare(outputRate);
    for (auto& s : scratch_) s.assign(static_cast<std::size_t>(maxBlock), 0.0f);
    attackStep_ = static_cast<float>(1.0 / std::max(1.0, kAttackSeconds * outputRate));
    releaseStep_ = static_cast<float>(1.0 / std::max(1.0, kReleaseSeconds * outputRate));
}

void VoicePool::noteOn(int note, float velocity, SampleSource& source, std::uint64_t generation, PlayOptions options,
                       int rootNote)
{
    noteOff(note);
    auto pick = std::find_if(voices_.begin(), voices_.end(), [](const Voice& v) { return v.stage == Stage::Off; });
    if (pick == voices_.end()) {
        // Steal: a releasing voice is already on its way out; else the oldest.
        pick = std::min_element(voices_.begin(), voices_.end(), [](const Voice& a, const Voice& b) {
            const bool ar = a.stage == Stage::Release, br = b.stage == Stage::Release;
            return ar != br ? ar : a.started < b.started;
        });
    }
    Voice& v = *pick;
    options.loop = false;
    options.speed = std::pow(2.0, (note - rootNote) / 12.0);
    v.head.start(source, options);
    v.stage = v.head.active() ? Stage::Attack : Stage::Off;
    v.note = note;
    v.velocity = std::clamp(velocity, 0.0f, 1.0f);
    v.level = 0.0f;
    v.started = ++counter_;
    v.generation = generation;
}

void VoicePool::noteOff(int note)
{
    for (auto& v : voices_)
        if (v.note == note && (v.stage == Stage::Attack || v.stage == Stage::Hold)) v.stage = Stage::Release;
}

void VoicePool::releaseAll()
{
    for (auto& v : voices_)
        if (v.stage == Stage::Attack || v.stage == Stage::Hold) v.stage = Stage::Release;
}

void VoicePool::kill()
{
    for (auto& v : voices_) {
        v.stage = Stage::Off;
        v.head.stop();
    }
}

void VoicePool::render(float* const* out, int n)
{
    float* tmp[2] = {scratch_[0].data(), scratch_[1].data()};
    for (auto& v : voices_) {
        int done = 0;
        while (v.stage != Stage::Off && done < n) {
            const int m = std::min(n - done, static_cast<int>(scratch_[0].size()));
            const int sounding = v.head.render(tmp, m);
            for (int i = 0; i < m; ++i) {
                switch (v.stage) {
                case Stage::Attack:
                    v.level += attackStep_;
                    if (v.level >= 1.0f) {
                        v.level = 1.0f;
                        v.stage = Stage::Hold;
                    }
                    break;
                case Stage::Release:
                    v.level = std::max(0.0f, v.level - releaseStep_);
                    if (v.level == 0.0f) v.stage = Stage::Off;
                    break;
                default: break;
                }
                const float g = v.level * v.velocity;
                out[0][done + i] += g * tmp[0][i];
                out[1][done + i] += g * tmp[1][i];
            }
            if (sounding < m) v.stage = Stage::Off; // played to the end
            done += m;
        }
    }
}

int VoicePool::active() const
{
    return static_cast<int>(std::count_if(voices_.begin(), voices_.end(), [](const Voice& v) { return v.stage != Stage::Off; }));
}

std::uint64_t VoicePool::oldestGeneration() const
{
    std::uint64_t oldest = kNone;
    for (const auto& v : voices_)
        if (v.stage != Stage::Off) oldest = std::min(oldest, v.generation);
    return oldest;
}

} // namespace asma::audio
