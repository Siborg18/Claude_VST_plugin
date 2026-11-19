// HardKick Forge - Hardstyle kick distortion effect
// DSP: tail generator -> 4x oversampled [tone EQ -> gain -> clipper -> DC block] x stages -> low/high cut -> mix -> output

#include "DistrhoPlugin.hpp"
#include "Params.hpp"
#include "Scope.hpp"
#include <cmath>
#include <cstring>
#include <algorithm>

START_NAMESPACE_DISTRHO

static constexpr float kPi = 3.14159265358979f;
static inline float dbToGain(float db) { return std::pow(10.0f, db * 0.05f); }

// ---------------------------------------------------------------------------------------------
// RBJ biquad (transposed direct form II)

struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1 = 0, z2 = 0;

    void reset() { z1 = z2 = 0.0f; }

    inline float process(float xf)
    {
        const double x = xf;
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return (float)y;
    }

    void set(double B0, double B1, double B2, double A0, double A1, double A2)
    {
        b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0;
    }

    void peaking(double fs, double f, double q, double gainDb)
    {
        if (f > fs * 0.45) f = fs * 0.45;
        const double A = std::pow(10.0, gainDb / 40.0);
        const double w = 2.0 * M_PI * f / fs, c = std::cos(w), al = std::sin(w) / (2.0 * q);
        set(1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
    }

    void lowpass(double fs, double f, double q)
    {
        if (f > fs * 0.45) f = fs * 0.45;
        const double w = 2.0 * M_PI * f / fs, c = std::cos(w), al = std::sin(w) / (2.0 * q);
        set((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
    }

    void highpass(double fs, double f, double q)
    {
        if (f > fs * 0.45) f = fs * 0.45;
        const double w = 2.0 * M_PI * f / fs, c = std::cos(w), al = std::sin(w) / (2.0 * q);
        set((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
    }
};

// ---------------------------------------------------------------------------------------------
// TPT state-variable bandpass (unity peak gain), coefficients shared per sample

struct SvfCoefs { float a1 = 0, a2 = 0, a3 = 0, k = 1; };
static inline SvfCoefs svfCoefs(double fs, double f, double q)
{
    if (f > fs * 0.45) f = fs * 0.45;
    SvfCoefs c; const double g = std::tan(M_PI * f / fs), k = 1.0 / q;
    const double a1 = 1.0 / (1.0 + g * (g + k));
    c.a1 = (float)a1; c.a2 = (float)(g * a1); c.a3 = (float)(g * g * a1); c.k = (float)k;
    return c;
}
struct Svf {
    float ic1 = 0, ic2 = 0;
    void reset() { ic1 = ic2 = 0; }
    inline float bandpass(float x, const SvfCoefs& c)
    {
        const float v3 = x - ic2;
        const float v1 = c.a1 * ic1 + c.a2 * v3;
        const float v2 = ic2 + c.a2 * ic1 + c.a3 * v3;
        ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2;
        return c.k * v1;
    }
};

// ---------------------------------------------------------------------------------------------
// Simple mono reverb (Freeverb-style) used to thicken the kick tail before distortion

struct Comb {
    float* buf = nullptr; int size = 0, idx = 0; float store = 0;
    inline float process(float x, float fb, float damp)
    {
        const float out = buf[idx];
        store = out * (1.0f - damp) + store * damp;
        buf[idx] = x + store * fb;
        if (++idx >= size) idx = 0;
        return out;
    }
};

struct Allpass {
    float* buf = nullptr; int size = 0, idx = 0;
    inline float process(float x)
    {
        const float b = buf[idx];
        const float out = b - x;
        buf[idx] = x + b * 0.5f;
        if (++idx >= size) idx = 0;
        return out;
    }
};

struct Tail {
    static constexpr int kCombs = 4, kAps = 2;
    Comb combs[kCombs];
    Allpass aps[kAps];
    float* mem = nullptr;

    ~Tail() { delete[] mem; }

    void init(double fs, int spread)
    {
        static const int combT[kCombs] = { 1116, 1188, 1277, 1356 };
        static const int apT[kAps] = { 556, 441 };
        const double k = fs / 44100.0;
        int total = 0, sizes[kCombs + kAps];
        for (int i = 0; i < kCombs; ++i) total += sizes[i] = (int)((combT[i] + spread) * k) + 1;
        for (int i = 0; i < kAps; ++i)   total += sizes[kCombs + i] = (int)((apT[i] + spread) * k) + 1;
        delete[] mem;
        mem = new float[total];
        std::memset(mem, 0, sizeof(float) * total);
        float* p = mem;
        for (int i = 0; i < kCombs; ++i) { combs[i].buf = p; combs[i].size = sizes[i]; combs[i].idx = 0; combs[i].store = 0; p += sizes[i]; }
        for (int i = 0; i < kAps; ++i)   { aps[i].buf = p; aps[i].size = sizes[kCombs + i]; aps[i].idx = 0; p += sizes[kCombs + i]; }
    }

    inline float process(float x, float fb, float damp)
    {
        float y = 0.0f;
        const float in = x * 0.25f;
        for (int i = 0; i < kCombs; ++i) y += combs[i].process(in, fb, damp);
        for (int i = 0; i < kAps; ++i) y = aps[i].process(y);
        return y;
    }
};

// ---------------------------------------------------------------------------------------------
// 4x oversampler with linear-phase windowed-sinc FIR (97 taps @ 4x -> 24 samples total latency)

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

static constexpr int kMaxStages = 4;

struct Channel {
    Tail tail;
    Upsampler up;
    Downsampler down;
    Biquad eq[kMaxStages];
    Biquad eqPost;
    float dcX[kMaxStages] = {}, dcY[kMaxStages] = {};
    Biquad hp1, lp1, lp2, lpB, verbLp, verbHp, demud;
    Svf f1, f2;
    float dryDelay[kLatency + 1] = {};
    int dryPos = 0;

    void reset()
    {
        up.reset(); down.reset();
        for (int i = 0; i < kMaxStages; ++i) { eq[i].reset(); dcX[i] = dcY[i] = 0.0f; }
        eqPost.reset();
        hp1.reset(); lp1.reset(); lp2.reset(); lpB.reset(); verbLp.reset(); verbHp.reset(); demud.reset(); f1.reset(); f2.reset();
        std::memset(dryDelay, 0, sizeof(dryDelay)); dryPos = 0;
    }
};

class HardKickForgePlugin : public Plugin, public ScopeProvider
{
public:
    HardKickForgePlugin()
        : Plugin(kParamCount, kPresetCount, 0)
    {
        for (int i = 0; i < kParamCount; ++i) fValues[i] = kParams[i].def;
        setLatency(kLatency);
        sampleRateChanged(getSampleRate());
    }

protected:
    const char* getLabel() const override { return "HardKickForge"; }
    const char* getDescription() const override { return "Hardstyle kick distortion: multi-stage oversampled clipping with tone EQ and pre-distortion tail."; }
    const char* getMaker() const override { return "Simon"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('H', 'k', 'F', 'g'); }

    void initAudioPort(bool input, uint32_t index, AudioPort& port) override
    {
        port.groupId = kPortGroupStereo;
        Plugin::initAudioPort(input, index, port);
    }

    void initParameter(uint32_t index, Parameter& p) override
    {
        const ParamInfo& pi = kParams[index];
        p.hints = kParameterIsAutomatable;
        if (pi.isLog) p.hints |= kParameterIsLogarithmic;
        if (pi.isInt) p.hints |= kParameterIsInteger;
        if (index == pClip) p.hints |= kParameterIsBoolean;
        p.name = pi.name;
        p.symbol = pi.symbol;
        p.unit = pi.unit;
        p.ranges.min = pi.min;
        p.ranges.max = pi.max;
        p.ranges.def = pi.def;

        if (index == pTailNote)
        {
            static const char* const notes[13] = { "Auto", "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
            p.enumValues.count = 13;
            p.enumValues.restrictedMode = true;
            ParameterEnumerationValue* const v = new ParameterEnumerationValue[13];
            for (int i = 0; i < 13; ++i) { v[i].value = (float)i; v[i].label = notes[i]; }
            p.enumValues.values = v;
        }
        if (index == pMode)
        {
            p.enumValues.count = kModeCount;
            p.enumValues.restrictedMode = true;
            ParameterEnumerationValue* const v = new ParameterEnumerationValue[kModeCount];
            for (int i = 0; i < kModeCount; ++i) { v[i].value = (float)i; v[i].label = kModeNames[i]; }
            p.enumValues.values = v;
        }
    }

    void initProgramName(uint32_t index, String& name) override { name = kPresets[index].name; }

    void loadProgram(uint32_t index) override
    {
        for (int i = 0; i < kParamCount; ++i) setParameterValue(i, kPresets[index].v[i]);
    }

    float getParameterValue(uint32_t index) const override { return fValues[index]; }

    void setParameterValue(uint32_t index, float value) override
    {
        const ParamInfo& pi = kParams[index];
        if (value < pi.min) value = pi.min;
        if (value > pi.max) value = pi.max;
        if (pi.isInt) value = std::round(value);
        fValues[index] = value;
        fDirty = true;
    }

    void sampleRateChanged(double sr) override
    {
        fFs = sr;
        for (int c = 0; c < 2; ++c) { fCh[c].tail.init(sr, c * 23); fCh[c].reset(); }
        fDirty = true;
        updateCoeffs();
        snapSmoothers();
    }

    void activate() override
    {
        for (int c = 0; c < 2; ++c) { fCh[c].tail.init(fFs, c * 23); fCh[c].reset(); }
        updateCoeffs();
        snapSmoothers();
    }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        if (fDirty) updateCoeffs();

        const int stages = (int)fValues[pStages];
        const int mode = (int)fValues[pMode];
        const bool clip = fValues[pClip] > 0.5f;
        const float smooth = fSmoothCoef;

        float os[kOS];

        for (uint32_t i = 0; i < frames; ++i)
        {
            // parameter smoothing (per sample, zipper-free)
            fStageGainS += smooth * (fStageGainT - fStageGainS);
            fMixS += smooth * (fMixT - fMixS);
            fOutS += smooth * (fOutT - fOutS);
            fTailS += smooth * (fTailT - fTailS);

            const float monoIn = 0.5f * (inputs[0][i] + inputs[1][i]); // read before outputs are written (buffers may alias)
            bool onset = false;
            // transient detector -> punch envelope (bright + extra drive at the start of each kick)
            {
                const float m = 0.5f * (std::fabs(inputs[0][i]) + std::fabs(inputs[1][i]));
                fEnvFast += (m > fEnvFast ? fFastA : fFastR) * (m - fEnvFast);
                fEnvSlow += fSlowC * (m - fEnvSlow);
                if (fHold > 0) --fHold;
                if (fHold == 0 && fEnvFast > 0.02f && fEnvFast > 2.5f * fEnvSlow + 0.002f)
                {
                    fPunchEnv = 1.0f;
                    onset = true;
                    fHold = (int)(0.12 * fFs);
                }
                fPunchEnv *= fPunchDecay;
            }
            // tail generator: detects the kick's note after the pitch drop and keeps a phase-locked
            // sine going at that note for Tail Length, filling in as the input kick decays.
            // The distortion then turns this into the long hardstyle tail - even if the sample is short.
            float tailSig = 0.0f;
            {
                const float lp = (fTlp2 += fTlpC * ((fTlp1 += fTlpC * (monoIn - fTlp1)) - fTlp2));
                const float al = std::fabs(lp);
                fBodyEnv += (al > fBodyEnv ? fFastA : fBodyR) * (al - fBodyEnv);
                if (onset) { fTT = 0.0f; fTRef = 0.0f; fNPer = 0; fLastCross = -1; fDetecting = true; fTCount = 0; }
                fTT += fInvFs; ++fTCount;
                if (fTT < 0.08f) fTRef = std::max(fTRef, fBodyEnv);
                if (fTPrev <= 0.0f && lp > 0.0f)
                {
                    if (fLastCross >= 0)
                    {
                        const int per = fTCount - fLastCross;
                        if (fTT > 0.03f && per > (int)(fFs / 110.0) && per < (int)(fFs / 27.0))
                        {
                            fPer[fNPer % 2] = (float)per; ++fNPer;
                            const int n = std::min(fNPer, 2);
                            float sum = 0; for (int q = 0; q < n; ++q) sum += fPer[q];
                            fTDetHz = (float)fFs * n / sum;
                        }
                    }
                    fLastCross = fTCount;
                    if (fDetecting) fTPhase = 0.0f; // phase-lock the tail oscillator to the kick
                }
                fTPrev = lp;
                if (fDetecting && fTT > 0.05f && fBodyEnv < 0.3f * fTRef) fDetecting = false; // keep following the kick while its body is strong

                float hz = fNPer > 0 ? fTDetHz : 50.0f;
                if (fTailNoteHz > 0.0f)
                {
                    // snap to the chosen note in the octave closest to the detected pitch
                    float nh = fTailNoteHz;
                    while (nh * 1.4142f < hz) nh *= 2.0f;
                    while (nh / 1.4142f > hz) nh *= 0.5f;
                    hz = nh;
                }
                fTHz += (fDetecting ? 1.0f : 0.002f) * (hz - fTHz);   // tiny glide when switching to a manual note
                fTPhase += fTHz * fInvFs; if (fTPhase >= 1.0f) fTPhase -= 1.0f;

                float target = 0.0f;
                if (fTailAmt > 0.0f && fTT < fTailLen && fTRef > 1e-4f)
                {
                    const float fill = std::min(1.0f, std::max(0.0f, 1.0f - fBodyEnv / fTRef));
                    target = fTailAmt * fTRef * (fDetecting ? 0.5f * fill : fill);
                }
                fTailAmp += (target > fTailAmp ? fTailAtk : fTailRel) * (target - fTailAmp);
                tailSig = fTailAmp * std::sin(2.0f * 3.14159265f * fTPhase);
            }
            // growl: formant resonances that move over the tail of each kick
            {
                if (onset) fGrowlT = 0.0f;
                fGrowlT += fInvFs;
                // formant starts high after the punch and sweeps down through the tail, with a 7 Hz wobble
                const float sweep = 1.0f - 1.4f * std::min(fGrowlT / 0.35f, 1.0f);
                const float wob = std::sin(2.0f * 3.14159265f * 7.0f * fGrowlT);
                const float m = fMotion * (sweep + 0.2f * wob);
                if (fGrowl > 0.0f && (onset || ++fGrowlDiv >= 8)) // update the formant every 8 samples (~0.2 ms)
                {
                    fGrowlDiv = 0;
                    const double f = fFormant * std::pow(2.0, (double)m);
                    fGc1 = svfCoefs(fFs * kOS, f, 5.0);
                    fGc2 = svfCoefs(fFs * kOS, f * 2.4, 6.0);
                }
            }
            const float punchMix = fPunchAmt * fPunchEnv;
            const float punchGain = 1.0f + 0.4f * punchMix; // up to +3 dB extra drive in the punch

            for (int c = 0; c < 2; ++c)
            {
                Channel& ch = fCh[c];
                const float in = inputs[c][i];

                // latency-compensated dry signal for the Mix control
                const float dry = ch.dryDelay[ch.dryPos];
                ch.dryDelay[ch.dryPos] = in;
                if (++ch.dryPos > kLatency) ch.dryPos = 0;

                // tail sustain gain before the distortion
                float x = in + tailSig;

                // oversampled distortion chain
                ch.up.process(x, os);
                for (int k = 0; k < kOS; ++k)
                {
                    float s = os[k];
                    for (int st = 0; st < stages; ++st)
                    {
                        s = ch.eq[st].process(s);
                        s = hkfShape(s * (st == 0 ? fStageGainS * punchGain : fStageGainS * 1.5f), mode);
                        // DC blocker
                        const float y = s - ch.dcX[st] + 0.9995f * ch.dcY[st];
                        ch.dcX[st] = s; ch.dcY[st] = y;
                        s = y;
                    }
                    // post-EQ: the same tone band again after clipping gives the tail its pitched 'grit'
                    s = ch.eqPost.process(s) * fPostComp;
                    // growl: resonant formant band, saturated and added back; low-mid cleanup against mud
                    if (fGrowl > 0.0f)
                    {
                        const float b = ch.f1.bandpass(s, fGc1) + 0.7f * ch.f2.bandpass(s, fGc2);
                        s += fGrowl * 2.0f * std::tanh(1.5f * b);
                    }
                    s = ch.demud.process(s);
                    // post filters, output gain and final clipper (all oversampled)
                    s = ch.hp1.process(s);
                    {
                        // dark tail filter (24 dB/oct) crossfaded with a bright path during the punch
                        const float dark = ch.lp2.process(ch.lp1.process(s));
                        const float bright = ch.lpB.process(s);
                        s = dark + punchMix * (bright - dark);
                    }
                    s *= fOutS;
                    if (clip) s = s > 1.0f ? 1.0f : (s < -1.0f ? -1.0f : s);
                    os[k] = s;
                }
                float wet = ch.down.process(os);
                if (clip) wet = wet > 1.0f ? 1.0f : (wet < -1.0f ? -1.0f : wet); // catch FIR overshoot

                float out = dry * fOutS + fMixS * (wet - dry * fOutS);
                if (!std::isfinite(out)) { out = 0.0f; ch.reset(); }
                outputs[c][i] = out;
            }
            captureScope(onset, monoIn, 0.5f * (outputs[0][i] + outputs[1][i]));
        }
    }

public:
    const KickScope& getScope() const override { return fScope; }

private:
    // record input/output min/max per display column, starting at each detected kick
    void captureScope(bool onset, float in, float out)
    {
        if (onset)
        {
            if (fCapActive && fCapBin > 4) publishScope();
            fCapActive = true; fCapBin = 0; fCapCount = 0;
            fCapInMin = fCapInMax = in; fCapOutMin = fCapOutMax = out;
        }
        if (!fCapActive) return;
        fCapInMin = std::min(fCapInMin, in);   fCapInMax = std::max(fCapInMax, in);
        fCapOutMin = std::min(fCapOutMin, out); fCapOutMax = std::max(fCapOutMax, out);
        if (++fCapCount >= fCapPerBin)
        {
            fWorkInMin[fCapBin] = fCapInMin; fWorkInMax[fCapBin] = fCapInMax;
            fWorkOutMin[fCapBin] = fCapOutMin; fWorkOutMax[fCapBin] = fCapOutMax;
            fCapCount = 0;
            fCapInMin = fCapInMax = in; fCapOutMin = fCapOutMax = out;
            if (++fCapBin >= KickScope::kBins) { publishScope(); fCapActive = false; }
        }
    }

    void publishScope()
    {
        const int n = fCapBin;
        std::memcpy(fScope.inMin, fWorkInMin, sizeof(float) * n);
        std::memcpy(fScope.inMax, fWorkInMax, sizeof(float) * n);
        std::memcpy(fScope.outMin, fWorkOutMin, sizeof(float) * n);
        std::memcpy(fScope.outMax, fWorkOutMax, sizeof(float) * n);
        fScope.used = n;
        fScope.version.fetch_add(1, std::memory_order_release);
    }

    void updateCoeffs()
    {
        fDirty = false;
        const double osFs = fFs * kOS;
        const int stages = (int)fValues[pStages];
        for (int c = 0; c < 2; ++c)
        {
            Channel& ch = fCh[c];
            for (int st = 0; st < kMaxStages; ++st)
            {
                const double g = (st == 0) ? fValues[pEqGain] : fValues[pEqGain] * 0.5;
                ch.eq[st].peaking(osFs, fValues[pEqFreq], fValues[pEqQ], g);
            }
            ch.eqPost.peaking(osFs, fValues[pEqFreq], fValues[pEqQ] * 1.5, fValues[pEqGain] * 0.6);
            ch.hp1.highpass(osFs, fValues[pLowCut], 0.7071);   // 12 dB/oct, keeps the sub intact
            ch.lp1.lowpass(osFs, fValues[pHighCut], 0.5412);   // 24 dB/oct Butterworth
            ch.lp2.lowpass(osFs, fValues[pHighCut], 1.3066);
            ch.lpB.lowpass(osFs, 16000.0, 0.7071);
            ch.verbLp.lowpass(fFs, 900.0, 0.7071);             // keep the reverb tail tonal, not fizzy
            ch.verbHp.highpass(fFs, 150.0, 0.7071);            // and out of the sub so it can't thin the fundamental
            ch.demud.peaking(osFs, 180.0, 0.9, -3.0 - 3.0 * fValues[pGrowl] * 0.01); // tame the 3rd-harmonic mud
        }
        // total drive is split across the stages
        fStageGainT = dbToGain(fValues[pDrive] / (float)stages);
        fMixT = fValues[pMix] * 0.01f;
        fOutT = dbToGain(fValues[pOutput]);
        fTailT = fValues[pTail] * 0.01f * 1.5f;
        fTailAmt = fValues[pTail] * 0.012f;
        fTailLen = fValues[pTailSize] * 0.001f;                     // ms -> s, measured from the start of the kick
        {
            const int note = (int)fValues[pTailNote];
            fTailNoteHz = note > 0 ? 32.7032f * std::pow(2.0f, (note - 1) / 12.0f) : 0.0f;
        }
        fTlpC = 1.0f - std::exp(-2.0f * 3.14159265f * 150.0f / (float)fFs);
        fBodyR = 1.0f - std::exp(-1.0f / (0.015f * (float)fFs));
        fTailAtk = 1.0f - std::exp(-1.0f / (0.004f * (float)fFs));
        fTailRel = 1.0f - std::exp(-1.0f / (0.030f * (float)fFs));
        fSusR = 1.0f - std::exp(-1.0f / (0.015f * (float)fFs));
        fSusC = 1.0f - std::exp(-1.0f / (0.003f * (float)fFs));
        fInvFs = 1.0f / (float)fFs;
        fGrowl = fValues[pGrowl] * 0.01f;
        fFormant = fValues[pFormant];
        fMotion = fValues[pMotion] * 0.012f;
        const float size = fValues[pTailSize] * 0.01f;
        fTailFb = 0.60f + 0.37f * size;
        fTailDamp = 0.45f - 0.25f * size;
        fSmoothCoef = 1.0f - std::exp(-1.0f / (0.01f * (float)fFs)); // 10 ms
        fPunchAmt = fValues[pPunch] * 0.01f;
        fPostComp = dbToGain(-std::fmax(0.0f, fValues[pEqGain] * 0.6f) * 0.5f); // partial level compensation
        fPunchDecay = std::exp(-1.0f / (0.045f * (float)fFs));  // ~45 ms punch
        fFastA = 1.0f - std::exp(-1.0f / (0.0005f * (float)fFs));
        fFastR = 1.0f - std::exp(-1.0f / (0.010f * (float)fFs));
        fSlowC = 1.0f - std::exp(-1.0f / (0.040f * (float)fFs));
        fCapPerBin = std::max(1, (int)(KickScope::kMaxSeconds * fFs / KickScope::kBins));
    }

    void snapSmoothers()
    {
        fStageGainS = fStageGainT; fMixS = fMixT; fOutS = fOutT; fTailS = fTailT;
    }

    double fFs = 44100.0;
    float fValues[kParamCount];
    bool fDirty = true;
    Channel fCh[2];

    float fStageGainT = 1, fStageGainS = 1, fMixT = 1, fMixS = 1, fOutT = 1, fOutS = 1, fTailT = 0, fTailS = 0;
    float fPostComp = 1.0f;
    float fTailFb = 0.8f, fTailDamp = 0.3f, fSmoothCoef = 0.01f;
    float fPunchAmt = 0, fPunchDecay = 0.999f, fPunchEnv = 0, fEnvFast = 0, fEnvSlow = 0, fFastA = 0.1f, fFastR = 0.01f, fSlowC = 0.001f;
    int fHold = 0;
    float fTlp1 = 0, fTlp2 = 0, fTlpC = 0.02f, fBodyEnv = 0, fBodyR = 0.001f, fTT = 10, fTRef = 0, fTPrev = 0, fTDetHz = 50, fTHz = 50, fTPhase = 0;
    float fPer[3] = {882, 882, 882}; int fNPer = 0, fLastCross = -1, fTCount = 0; bool fDetecting = false;
    float fTailAmt = 0, fTailLen = 0.5f, fTailNoteHz = 0, fTailAmp = 0, fTailAtk = 0.01f, fTailRel = 0.001f;
    float fSusEnv = 0, fSusT = 1, fSusRef = 0, fSusGain = 1, fSusAmt = 0, fSusLen = 0.4f, fSusMax = 1, fSusR = 0.01f, fSusC = 0.01f, fInvFs = 1.0f / 44100;
    float fGrowl = 0, fFormant = 900, fMotion = 0, fGrowlT = 1;
    int fGrowlDiv = 0;
    SvfCoefs fGc1, fGc2;

    KickScope fScope;
    float fWorkInMin[KickScope::kBins] = {}, fWorkInMax[KickScope::kBins] = {}, fWorkOutMin[KickScope::kBins] = {}, fWorkOutMax[KickScope::kBins] = {};
    bool fCapActive = false; int fCapBin = 0, fCapCount = 0, fCapPerBin = 58;
    float fCapInMin = 0, fCapInMax = 0, fCapOutMin = 0, fCapOutMax = 0;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HardKickForgePlugin)
};

Plugin* createPlugin() { return new HardKickForgePlugin(); }

END_NAMESPACE_DISTRHO
