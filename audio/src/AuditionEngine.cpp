// SPDX-License-Identifier: GPL-3.0-only
#include "asma/audio/AuditionEngine.h"

#include "asma/audio/Gain.h"

#include <algorithm>
#include <cmath>

namespace asma::audio {

namespace {

enum Flags { kTempoSynced = 1, kKeySynced = 2, kTempoUnsure = 4, kKeyUnsure = 8, kFailed = 16 };

} // namespace

void AuditionEngine::prepare(int sampleRate, int maxBlock)
{
    sampleRate_ = sampleRate;
    maxBlock_ = maxBlock;
    head_.prepare(sampleRate);
    stretcher_.prepare(sampleRate, maxBlock);
    voices_.prepare(sampleRate, maxBlock);
    for (auto* buffers : {&play_, &mix_})
        for (auto& b : *buffers) b.assign(static_cast<std::size_t>(maxBlock), 0.0f);
    fadeFrames_ = std::max(1, static_cast<int>(std::lround(kStopFadeSeconds * sampleRate)));
}

std::uint64_t AuditionEngine::select(const std::filesystem::path& path, SampleInfo info, bool autoplay)
{
    const std::uint64_t generation = loader_.select(path, std::move(info), autoplay);
    selected_.store(generation);
    return generation;
}

void AuditionEngine::push(const Command& command)
{
    commands_.push(command); // a full queue drops the command: 64 control calls inside one block
}

void AuditionEngine::play()
{
    Command c;
    c.type = Command::Type::Play;
    c.generation = selected_.load();
    push(c);
}

void AuditionEngine::stop()
{
    Command c;
    c.type = Command::Type::Stop;
    c.generation = selected_.load(); // also covers selections still loading
    push(c);
}

void AuditionEngine::setEdits(const Edits& edits, bool restart)
{
    Command c;
    c.type = Command::Type::Edits;
    c.edits = edits;
    c.flag = restart;
    push(c);
}

void AuditionEngine::setSync(const SyncSettings& sync)
{
    Command c;
    c.type = Command::Type::Sync;
    c.sync = sync;
    push(c);
}

void AuditionEngine::setGainMatch(bool on)
{
    Command c;
    c.type = Command::Type::GainMatch;
    c.flag = on;
    push(c);
}

void AuditionEngine::setQuantise(double beats)
{
    Command c;
    c.type = Command::Type::Quantise;
    c.value = beats;
    push(c);
}

void AuditionEngine::handle(const Command& c)
{
    const bool sounding = state_ != State::Idle;
    switch (c.type) {
    case Command::Type::Play:
        if (current_ && current_->generation == c.generation) requestStart(0);
        else playWanted_ = c.generation; // not here yet: start when it arrives
        break;
    case Command::Type::Stop:
        stoppedThrough_ = std::max(stoppedThrough_, c.generation);
        playWanted_ = 0;
        restartAfterFade_ = false;
        if (state_ == State::Playing) beginStop();
        else if (state_ == State::Waiting) state_ = State::Idle;
        break;
    case Command::Type::Edits:
        edits_ = c.edits;
        if (c.flag && sounding && !(state_ == State::Stopping && !restartAfterFade_)) requestStart(0);
        break;
    case Command::Type::Sync: {
        // A new manual tempo alone is followed as the loop plays, like a host
        // tempo change; anything else changes the plan and restarts.
        const bool tempoOnly = c.sync.tempo == sync_.tempo && c.sync.key == sync_.key
                            && c.sync.projectKey.view() == sync_.projectKey.view();
        sync_ = c.sync;
        if (!tempoOnly && sounding && !(state_ == State::Stopping && !restartAfterFade_)) requestStart(0);
        break;
    }
    case Command::Type::GainMatch:
        gainMatch_ = c.flag;
        if (playing_) gain_ = gainMatch_ ? matchGain(playing_->info) : 1.0f;
        break;
    case Command::Type::Quantise: quantise_ = c.value; break;
    }
}

void AuditionEngine::adopt(Preview* preview)
{
    // A stop pressed after this selection was made cancels its autoplay; a
    // file that cannot be opened has nothing to start.
    const bool start = preview->source
                    && ((preview->autoplay && preview->generation > stoppedThrough_)
                        || playWanted_ == preview->generation);
    if (playWanted_ <= preview->generation) playWanted_ = 0;
    current_ = preview;
    if (state_ == State::Waiting) state_ = State::Idle;
    if (start) {
        requestStart(0);
    } else if (state_ == State::Playing) {
        restartAfterFade_ = false;
        beginStop(); // a new selection silences the old one
    } else if (state_ == State::Stopping) {
        restartAfterFade_ = false;
    }
}

void AuditionEngine::requestStart(int offset)
{
    if (!current_ || !current_->source) return;
    if (state_ == State::Playing || state_ == State::Stopping) {
        restartAfterFade_ = true;
        if (state_ == State::Playing) beginStop();
        return;
    }
    // Waiting counts from where rendering is now, `offset` frames into the block.
    wait_ = 0;
    primed_ = 0;
    if (quantise_ > 0.0 && transport_.playing && bpm() > 0.0) {
        const double ppq = transport_.ppq + (chunkOffset_ + offset) * bpm() / (60.0 * sampleRate_);
        wait_ = framesToNextBoundary(ppq, bpm(), sampleRate_, quantise_);
    }
    state_ = State::Waiting;
}

bool AuditionEngine::startNow()
{
    SampleSource& source = *current_->source;
    head_.start(source, toPlayOptions(edits_, source.sampleRate(), current_->info.isLoop.value_or(false)));
    // A start deep in a streamed file waits, briefly, for its block: better
    // late than cutting in after silence.
    if (head_.active() && !source.ready(static_cast<std::int64_t>(head_.position()))
        && primed_ < kMaxPrimeSeconds * sampleRate_) {
        head_.stop();
        return false;
    }
    primed_ = 0;
    playing_ = current_;
    plan_ = plan(*current_);
    stretcher_.setTiming(plan_.ratio, plan_.semitones);
    // Synced sounds always run through the stretch, so a tempo change mid-loop can follow.
    stretcher_.start(head_, !(plan_.tempoSynced || plan_.keySynced));
    gain_ = gainMatch_ ? matchGain(current_->info) : 1.0f;
    restartAfterFade_ = false;
    state_ = head_.active() ? State::Playing : State::Idle;
    if (state_ == State::Idle) playing_ = nullptr;
    return true;
}

void AuditionEngine::beginStop()
{
    state_ = State::Stopping;
    fade_ = fadeFrames_;
}

SyncPlan AuditionEngine::plan(const Preview& preview) const
{
    SyncSettings s = sync_;
    s.hostBpm = bpm();
    return planSync(preview.info, s);
}

void AuditionEngine::noteOn(int note, float velocity)
{
    if (!current_ || !current_->source) return;
    const float gain = gainMatch_ ? matchGain(current_->info) : 1.0f;
    voices_.noteOn(note, velocity, *current_->source, current_->generation,
                   toPlayOptions(edits_, current_->source->sampleRate(), false),
                   current_->info.rootNote.value_or(VoicePool::kDefaultRootNote), gain);
}

void AuditionEngine::noteOff(int note) { voices_.noteOff(note); }

void AuditionEngine::allNotesOff() { voices_.releaseAll(); }

void AuditionEngine::process(float* const* out, int n)
{
    // Hosts may send more than they announced: work in prepared-size chunks.
    for (int done = 0; done < n;) {
        const int m = std::min(n - done, maxBlock_);
        float* chunk[2] = {out[0] + done, out[1] + done};
        chunkOffset_ = done;
        processChunk(chunk, m, done == 0);
        done += m;
    }
}

void AuditionEngine::processChunk(float* const* out, int n, bool first)
{
    if (first) {
        Command command;
        while (commands_.pop(command)) handle(command);
        if (Preview* preview = loader_.takeReady()) adopt(preview);
    }

    // A synced loop follows the host tempo as it changes.
    if (state_ == State::Playing && plan_.tempoSynced) {
        const SyncPlan now = plan(*playing_);
        if (now.tempoSynced && now.ratio != plan_.ratio) {
            plan_.ratio = now.ratio;
            stretcher_.setTiming(plan_.ratio, plan_.semitones);
        }
    }

    for (int c = 0; c < 2; ++c) std::fill(out[c], out[c] + n, 0.0f);
    float* tmp[2] = {play_[0].data(), play_[1].data()};
    int done = 0;
    while (done < n && state_ != State::Idle) {
        if (state_ == State::Waiting) {
            if (wait_ >= n - done) {
                wait_ -= n - done;
                break;
            }
            done += static_cast<int>(wait_);
            wait_ = 0;
            if (!startNow()) {
                primed_ += n - done; // try again next block
                break;
            }
            continue;
        }
        int m = n - done;
        if (state_ == State::Stopping) m = std::min(m, fade_);
        stretcher_.process(head_, tmp, m);
        for (int i = 0; i < m; ++i) {
            float g = gain_;
            if (state_ == State::Stopping) g *= static_cast<float>(fade_ - i) / static_cast<float>(fadeFrames_);
            out[0][done + i] = g * tmp[0][i];
            out[1][done + i] = g * tmp[1][i];
        }
        done += m;
        if (state_ == State::Stopping) {
            fade_ -= m;
            if (fade_ == 0) {
                head_.stop();
                playing_ = nullptr;
                state_ = State::Idle;
                if (restartAfterFade_) requestStart(done);
            }
        } else if (!stretcher_.active(head_)) {
            playing_ = nullptr;
            state_ = State::Idle; // played out
        }
    }

    if (voices_.active() > 0) {
        float* mix[2] = {mix_[0].data(), mix_[1].data()};
        for (auto& b : mix_) std::fill(b.begin(), b.begin() + n, 0.0f);
        voices_.render(mix, n);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < n; ++i) out[c][i] += mix[c][i];
    }

    std::uint64_t oldest = voices_.oldestGeneration();
    for (const Preview* p : {current_, playing_})
        if (p) oldest = std::min(oldest, p->generation);
    loader_.release(oldest);
    publish();
}

void AuditionEngine::publish()
{
    statusGeneration_.store(current_ ? current_->generation : 0, std::memory_order_relaxed);
    statusPlaying_.store(state_ != State::Idle, std::memory_order_relaxed);
    const double rate = playing_ && playing_->source ? playing_->source->sampleRate() : 0.0;
    statusPosition_.store(rate > 0.0 ? head_.position() / rate : 0.0, std::memory_order_relaxed);
    statusRatio_.store(plan_.ratio, std::memory_order_relaxed);
    statusSemitones_.store(plan_.semitones, std::memory_order_relaxed);
    statusFlags_.store((plan_.tempoSynced ? kTempoSynced : 0) | (plan_.keySynced ? kKeySynced : 0)
                           | (plan_.tempoUnsure ? kTempoUnsure : 0) | (plan_.keyUnsure ? kKeyUnsure : 0)
                           | (current_ && !current_->source ? kFailed : 0),
                       std::memory_order_relaxed);
    statusVoices_.store(voices_.active(), std::memory_order_relaxed);
}

EngineStatus AuditionEngine::status() const
{
    EngineStatus s;
    s.generation = statusGeneration_.load(std::memory_order_relaxed);
    s.playing = statusPlaying_.load(std::memory_order_relaxed);
    s.position = statusPosition_.load(std::memory_order_relaxed);
    s.ratio = statusRatio_.load(std::memory_order_relaxed);
    s.semitones = statusSemitones_.load(std::memory_order_relaxed);
    const int flags = statusFlags_.load(std::memory_order_relaxed);
    s.tempoSynced = flags & kTempoSynced;
    s.keySynced = flags & kKeySynced;
    s.tempoUnsure = flags & kTempoUnsure;
    s.keyUnsure = flags & kKeyUnsure;
    s.failed = flags & kFailed;
    s.voices = statusVoices_.load(std::memory_order_relaxed);
    return s;
}

} // namespace asma::audio
