// HardKick Forge 2 - two-sample hardstyle kick instrument
// A (tok / body) and B (bass tail) are one-shot samples triggered by MIDI notes.
// Each goes through its own 4x oversampled multi-stage clipper, then an equal-power A/B mix.

#include "DistrhoPlugin.hpp"
#include "Shared.hpp"
#include <mutex>
#include <memory>

START_NAMESPACE_DISTRHO

static constexpr int kOS = 4;
static constexpr int kTaps = 97;
static constexpr int kPhaseTaps = (kTaps + kOS - 1) / kOS; // 25
static constexpr int kLatency = (kTaps - 1) / kOS;         // 24 base-rate samples (up + down)

struct FirTable {
    float h[kTaps];
    float phase[kOS][kPhaseTaps];

    FirTable()
    {
        const double fc = 0.5 / kOS * 0.92; // cutoff in cycles/sample at 4x rate
        const double beta = 8.0;
        auto bessel0 = [](double x) { double s = 1, t = 1; for (int k = 1; k < 30; ++k) { t *= (x / (2 * k)) * (x / (2 * k)); s += t; } return s; };
        const double m = (kTaps - 1) / 2.0;
        double sum = 0;
        for (int n = 0; n < kTaps; ++n)
        {
            const double x = n - m;
            const double sinc = (x == 0) ? 2 * fc : std::sin(2 * M_PI * fc * x) / (M_PI * x);
            const double r = x / m;
            const double w = bessel0(beta * std::sqrt(std::fmax(0.0, 1 - r * r))) / bessel0(beta);
            h[n] = (float)(sinc * w);
            sum += h[n];
        }
        for (int n = 0; n < kTaps; ++n) h[n] = (float)(h[n] / sum);
        for (int p = 0; p < kOS; ++p)
            for (int k = 0; k < kPhaseTaps; ++k)
            {
                const int n = p + k * kOS;
                phase[p][k] = n < kTaps ? h[n] * kOS : 0.0f;
            }
    }
};
static const FirTable gFir;

struct Upsampler {
    float hist[kPhaseTaps * 2] = {};
    int pos = 0;
    void reset() { std::memset(hist, 0, sizeof(hist)); pos = 0; }
    inline void process(float x, float* out)
    {
        pos = (pos == 0) ? kPhaseTaps - 1 : pos - 1;
        hist[pos] = hist[pos + kPhaseTaps] = x;
        const float* hp = hist + pos;
        for (int p = 0; p < kOS; ++p)
        {
            float acc = 0.0f;
            for (int k = 0; k < kPhaseTaps; ++k) acc += gFir.phase[p][k] * hp[k];
            out[p] = acc;
        }
    }
};

struct Downsampler {
    float hist[kTaps * 2] = {};
    int pos = 0;
    void reset() { std::memset(hist, 0, sizeof(hist)); pos = 0; }
    inline float process(const float* in)
    {
        for (int p = 0; p < kOS; ++p)
        {
            pos = (pos == 0) ? kTaps - 1 : pos - 1;
            hist[pos] = hist[pos + kTaps] = in[p];
        }
        const float* hp = hist + pos;
        float acc = 0.0f;
        for (int k = 0; k < kTaps; ++k) acc += gFir.h[k] * hp[k];
        return acc;
    }
};


// ---------------------------------------------------------------------------------------------

struct Voice { double pos = 0; float gain = 0; bool active = false; };

struct SampleSlot {
    std::shared_ptr<SampleData> data;
    Voice cur, old;           // 'old' fades out on retrigger (click-free)
    int idle = 1 << 20;       // samples since the last voice ended (skip DSP when silent)
    Upsampler up[2]; Downsampler down[2]; KickChain chain[2];
    void reset() { cur = Voice(); old = Voice(); for (int c = 0; c < 2; ++c) { up[c].reset(); down[c].reset(); chain[c].reset(); } }
};

class HardKickForge2 : public Plugin, public PreviewTarget
{
public:
    HardKickForge2() : Plugin(kParamCount, 0, 2)
    {
        for (int i = 0; i < kParamCount; ++i) fValues[i] = kParams[i].def;
        setLatency(kLatency);
        fFadeStep = 1.0f / (0.005f * (float)getSampleRate());
    }

public:
    void requestPreview(int code) override { fPreviewReq.store(code + 1); }

protected:
    const char* getLabel() const override { return "HardKickForge2"; }
    const char* getDescription() const override { return "Two-sample hardstyle kick: sample A for the tok/body, sample B for the bass tail, each with its own distortion."; }
    const char* getMaker() const override { return "Simon"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('H', 'k', 'F', '2'); }

    void initAudioPort(bool input, uint32_t index, AudioPort& port) override
    {
        port.groupId = kPortGroupStereo;
        Plugin::initAudioPort(input, index, port);
    }

    void initParameter(uint32_t index, Parameter& p) override
    {
        const ParamInfo& pi = kParams[index];
        p.hints = kParameterIsAutomatable | (pi.isInt ? kParameterIsInteger : 0);
        p.name = pi.name; p.symbol = pi.symbol; p.unit = pi.unit;
        p.ranges.min = pi.min; p.ranges.max = pi.max; p.ranges.def = pi.def;
        if (index == pFadeOn || index == pMatchPitch || index == pSweepOn) p.hints |= kParameterIsBoolean;
        // the tail's tone / punch / growl knobs are gone from the UI; keep the parameters but hide them
        if (index == pBToneF || index == pBToneG || index == pBToneQ || index == pBPunch || index == pBGrowl || index == pBFormant
            || index == pAGrowl || index == pAFormant || index == pAMotion)
            p.hints |= kParameterIsHidden;
        if (pi.isLog) p.hints |= kParameterIsLogarithmic;
        if (index == pAMode || index == pBMode)
        {
            p.enumValues.count = kModeCount;
            p.enumValues.restrictedMode = true;
            ParameterEnumerationValue* const v = new ParameterEnumerationValue[kModeCount];
            for (int i = 0; i < kModeCount; ++i) { v[i].value = (float)i; v[i].label = kModeNames[i]; }
            p.enumValues.values = v;
        }
    }

    void initState(uint32_t index, State& state) override
    {
        state.key = kStateKeys[index];
        state.label = index == 0 ? "Sample A (tok / body)" : "Sample B (bass tail)";
        state.defaultValue = "";
        state.hints = kStateIsFilenamePath;
    }

    void setState(const char* key, const char* value) override
    {
        const int slot = std::strcmp(key, kStateKeys[0]) == 0 ? 0 : (std::strcmp(key, kStateKeys[1]) == 0 ? 1 : -1);
        if (slot < 0) return;
        std::shared_ptr<SampleData> sd;
        if (value != nullptr && value[0] != '\0')
        {
            sd = std::make_shared<SampleData>();
            std::string err;
            if (!loadWav(value, *sd, err)) { d_stderr("HardKick Forge 2: %s (%s)", err.c_str(), value); sd.reset(); }
        }
        const float hz = sd ? estimatePitch(*sd, slot == 0) : 0.0f;   // analysed here, off the audio thread
        std::lock_guard<std::mutex> lk(fLock);   // swap under lock; the old sample is freed here, not on the audio thread
        fSlots[slot].data.swap(sd);
        fPitchHz[slot] = hz;
        fSlots[slot].cur = Voice(); fSlots[slot].old = Voice();
    }

    float getParameterValue(uint32_t index) const override { return fValues[index]; }

    void setParameterValue(uint32_t index, float v) override
    {
        const ParamInfo& pi = kParams[index];
        v = std::max(pi.min, std::min(pi.max, v));
        if (pi.isInt) v = std::round(v);
        fValues[index] = v;
        fDirty = true;
    }

    void activate() override
    {
        fFadeStep = 1.0f / (0.005f * (float)getSampleRate());
        fFadeSmooth = 1.0f - std::exp(-1.0f / (0.002f * (float)getSampleRate()));
        std::lock_guard<std::mutex> lk(fLock);
        for (auto& s : fSlots) s.reset();
        fSweep[0].reset(); fSweep[1].reset(); fDirty = true;
    }

    void run(const float**, float** outputs, uint32_t frames, const MidiEvent* midi, uint32_t midiCount) override
    {
        std::unique_lock<std::mutex> lk(fLock, std::try_to_lock);
        if (!lk.owns_lock()) { std::memset(outputs[0], 0, sizeof(float) * frames); std::memset(outputs[1], 0, sizeof(float) * frames); return; }

        const double hostSr = getSampleRate();
        const float mix = fValues[pMix] * 0.01f;
        // each sample's output level feeds the equal-power A/B mix
        const float gA = std::cos(mix * 1.5707963f);   // each chain applies its own Output level
        const float gB = std::sin(mix * 1.5707963f);
        // refresh chain coefficients when parameters changed
        if (fDirty)
        {
            fDirty = false;
            for (int k = 0; k < 2; ++k)
            {
                const ChainParams cp = chainParamsFrom(fValues, k);
                for (int c = 0; c < 2; ++c) { fSlots[k].chain[c].setup(cp, hostSr * kOS); fSlots[k].chain[c].updateTime((float)(fNoteT / hostSr), true); }
            }
        }
        const bool sweepOn = fValues[pSweepOn] > 0.5f;
        const float swStart = fValues[pSweepStart], swEnd = fValues[pSweepEnd], swFrom = fValues[pSweepFrom], swTo = fValues[pSweepTo], swRes = fValues[pSweepRes];
        uint32_t ev = 0;
        const bool fadeOn = fValues[pFadeOn] > 0.5f;
        const float fadePoint = fValues[pFadePoint], fadeLen = fValues[pFadeLen];
        const double invSr = 1.0 / hostSr;
        // Match Pitch: resample the tail (B) onto the tok's (A) note
        const double pitchRatio = fValues[pMatchPitch] > 0.5f ? std::pow(2.0, matchSemitones(fPitchHz[1], fPitchHz[0]) / 12.0) : 1.0;

        for (uint32_t i = 0; i < frames; ++i)
        {
            // click-to-preview from the UI: play one sample solo, raw (before) or processed (after)
            if (i == 0)
            {
                const int req = fPreviewReq.exchange(0) - 1;
                if (req >= 0)
                {
                    const int ps = req / 2;
                    for (int k = 0; k < 2; ++k) { fSlots[k].old = fSlots[k].cur; fSlots[k].cur.active = false; }
                    if (fSlots[ps].data) { fSlots[ps].cur.pos = 0; fSlots[ps].cur.gain = 1; fSlots[ps].cur.active = true; }
                    fPreviewSlot = ps; fPreviewRaw = (req % 2) == 0;
                    fNoteT = 0;
                }
            }
            // note-on -> retrigger both samples (one-shot, original pitch)
            for (; ev < midiCount && midi[ev].frame <= i; ++ev)
            {
                if (midi[ev].size != 3) continue;
                const uint8_t st = midi[ev].data[0] & 0xF0;
                if (st == 0x90 && midi[ev].data[2] > 0)
                    for (auto& s : fSlots)
                        if (s.data)
                        {
                            s.old = s.cur;                     // fade the previous hit out over 5 ms
                            s.cur.pos = 0; s.cur.gain = 1; s.cur.active = true;
                        }
                if (st == 0x90 && midi[ev].data[2] > 0) { fNoteT = 0; fPreviewSlot = -1; }
            }

            const float tNote = (float)(fNoteT * invSr);
            for (int k = 0; k < 2; ++k)
                for (int c = 0; c < 2; ++c) fSlots[k].chain[c].updateTime(tNote);
            float fa = 1.0f, fb = 1.0f;
            fadeGains((float)(fNoteT * invSr), fadeOn, fadePoint, fadeLen, fa, fb);
            fFa += fFadeSmooth * (fa - fFa); fFb += fFadeSmooth * (fb - fFb);   // 2 ms smoothing: no clicks on retrigger
            fa = fFa; fb = fFb;
            if (fNoteT < (1u << 30)) ++fNoteT;

            float outL = 0, outR = 0;
            for (int k = 0; k < 2; ++k)
            {
                SampleSlot& s = fSlots[k];
                float in[2] = { 0, 0 };
                if (s.data)
                {
                    const SampleData& d = *s.data;
                    const bool rawPreview = fPreviewSlot == k && fPreviewRaw;
                    const double rate = d.sampleRate / hostSr * ((k == 1 && !rawPreview) ? pitchRatio : 1.0);
                    auto readVoice = [&](Voice& v, float g)
                    {
                        if (!v.active) return;
                        const size_t n = d.frames();
                        const size_t i0 = (size_t)v.pos;
                        if (i0 + 1 >= n) { v.active = false; return; }
                        const float fr = (float)(v.pos - (double)i0);
                        in[0] += g * (d.l[i0] + fr * (d.l[i0 + 1] - d.l[i0]));
                        in[1] += g * (d.r[i0] + fr * (d.r[i0 + 1] - d.r[i0]));
                        v.pos += rate;
                    };
                    readVoice(s.cur, 1.0f);
                    if (s.old.active)
                    {
                        s.old.gain -= fFadeStep;
                        if (s.old.gain <= 0) s.old.active = false; else readVoice(s.old, s.old.gain);
                    }
                }
                if (s.cur.active || s.old.active) s.idle = 0; else ++s.idle;
                if (s.idle > 4096) continue;            // chain has settled: nothing to process
                float os[kOS];
                float y[2];
                for (int c = 0; c < 2; ++c)
                {
                    s.up[c].process(in[c], os);
                    for (int q = 0; q < kOS; ++q) os[q] = s.chain[c].process(os[q]);
                    y[c] = s.down[c].process(os);
                }
                if (fPreviewSlot >= 0)
                {
                    // preview: only the clicked sample, no fade / mix / sweep
                    if (k != fPreviewSlot) continue;
                    if (fPreviewRaw) { y[0] = in[0]; y[1] = in[1]; }
                    const float pg = (k == 1 && !fPreviewRaw) ? tailLengthGain(tNote, fValues[pBLength]) : 1.0f;
                    outL += pg * y[0]; outR += pg * y[1];
                    continue;
                }
                const float g = k == 0 ? gA * fa : gB * fb * tailLengthGain(tNote, fValues[pBLength]);
                outL += g * y[0]; outR += g * y[1];
            }
            // filter sweep over the whole kick (resonant low-pass, cutoff glides between Start and End)
            if (sweepOn && fPreviewSlot < 0)
            {
                if (++fSweepDiv >= 8 || fSweepFirst)
                {
                    fSweepDiv = 0; fSweepFirst = false;
                    fSweepC = svfCoefs(hostSr, sweepCutoff(tNote * 1000.0f, swStart, swEnd, swFrom, swTo), swRes);
                }
                outL = fSweep[0].lowpass(outL, fSweepC);
                outR = fSweep[1].lowpass(outR, fSweepC);
            }
            // safety ceiling at 0 dBFS
            outputs[0][i] = std::max(-1.0f, std::min(1.0f, outL));
            outputs[1][i] = std::max(-1.0f, std::min(1.0f, outR));
        }
    }

private:
    float fValues[kParamCount];
    SampleSlot fSlots[2];
    std::mutex fLock;
    float fFadeStep = 0.005f;
    float fFa = 1, fFb = 1, fFadeSmooth = 0.01f;
    float fPitchHz[2] = { 0, 0 };
    std::atomic<int> fPreviewReq{0};
    int fPreviewSlot = -1; bool fPreviewRaw = false;
    bool fDirty = true, fSweepFirst = true;
    Svf fSweep[2];
    SvfCoefs fSweepC;
    int fSweepDiv = 0;
    uint32_t fNoteT = 1u << 30;   // samples since the last note-on (drives the tok -> tail fade)

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HardKickForge2)
};

Plugin* createPlugin() { return new HardKickForge2(); }

END_NAMESPACE_DISTRHO
