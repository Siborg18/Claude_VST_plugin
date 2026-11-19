#pragma once
// HardKick Forge 2 - shared definitions (DSP + UI)
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>
#include <algorithm>
#include <atomic>

enum ParamId { pADrive = 0, pAStages, pAMode, pBDrive, pBStages, pBMode, pMix, pAOut, pBOut, pFadeOn, pFadePoint, pFadeLen, pMatchPitch,
    // per-sample tone / growl / punch / cut (A block then B block, same order)
    pAToneF, pAToneG, pAToneQ, pAPunch, pAGrowl, pAFormant, pAMotion, pALowCut, pAHighCut,
    pBToneF, pBToneG, pBToneQ, pBPunch, pBGrowl, pBFormant, pBMotion, pBLowCut, pBHighCut,
    // filter sweep over the whole kick
    pSweepOn, pSweepStart, pSweepEnd, pSweepFrom, pSweepTo, pSweepRes,
    pBLength,
    kParamCount };
static constexpr int kSlotBlock = pBToneF - pAToneF; // 9
enum ClipMode { kModeSoft = 0, kModeHard, kModeFold, kModeAsym, kModeCount };

struct ParamInfo { const char* name; const char* symbol; const char* unit; float min, max, def; bool isInt; bool isLog; };
static const ParamInfo kParams[kParamCount] = {
    { "A Drive",     "adrive",  "dB", 0.0f, 60.0f, 30.0f, false, false },
    { "A Stages",    "astages", "",   1.0f, 4.0f,  3.0f,  true , false },
    { "A Clip Mode", "amode",   "",   0.0f, 3.0f,  1.0f,  true , false },
    { "B Drive",     "bdrive",  "dB", 0.0f, 60.0f, 24.0f, false, false },
    { "B Stages",    "bstages", "",   1.0f, 4.0f,  2.0f,  true , false },
    { "B Clip Mode", "bmode",   "",   0.0f, 3.0f,  0.0f,  true , false },
    { "Mix A/B",     "mix",     "%",  0.0f, 100.0f, 50.0f, false, false },
    { "A Output",    "aout",    "dB", -24.0f, 12.0f, 0.0f, false, false },
    { "B Output",    "bout",    "dB", -24.0f, 12.0f, 0.0f, false, false },
    { "Tok>Tail Fade",  "fadeon",    "",   0.0f, 1.0f,    1.0f,  true , false },
    { "Fade Point",     "fadepoint", "ms", 0.0f, 1000.0f, 80.0f, false, false },
    { "Fade Length",    "fadelen",   "ms", 0.0f, 300.0f,  30.0f, false, false },
    { "Match Pitch",    "matchpitch", "",  0.0f, 1.0f,    0.0f,  true , false },
    { "A Tone Freq",  "atonef",   "Hz", 100.0f, 5000.0f, 1000.0f, false, true },
    { "A Tone Gain",  "atoneg",   "dB", -12.0f, 24.0f,   8.0f,    false, false },
    { "A Tone Q",     "atoneq",   "",   0.3f,   8.0f,    1.2f,    false, true },
    { "A Punch",      "apunch",   "%",  0.0f,   100.0f,  60.0f,   false, false },
    { "A Growl",      "agrowl",   "%",  0.0f,   100.0f,  0.0f,    false, false },
    { "A Formant",    "aformant", "Hz", 200.0f, 3000.0f, 900.0f,  false, true },
    { "A Motion",     "amotion",  "%",  0.0f,   100.0f,  60.0f,   false, false },
    { "A Low Cut",    "alowcut",  "Hz", 10.0f,  400.0f,  20.0f,   false, true },
    { "A High Cut",   "ahighcut", "Hz", 500.0f, 20000.0f, 14000.0f, false, true },
    { "B Tone Freq",  "btonef",   "Hz", 100.0f, 5000.0f, 800.0f,  false, true },
    { "B Tone Gain",  "btoneg",   "dB", -12.0f, 24.0f,   4.0f,    false, false },
    { "B Tone Q",     "btoneq",   "",   0.3f,   8.0f,    1.0f,    false, true },
    { "B Punch",      "bpunch",   "%",  0.0f,   100.0f,  0.0f,    false, false },
    { "B Growl",      "bgrowl",   "%",  0.0f,   100.0f,  35.0f,   false, false },
    { "B Formant",    "bformant", "Hz", 200.0f, 3000.0f, 800.0f,  false, true },
    { "B Motion",     "bmotion",  "%",  0.0f,   100.0f,  60.0f,   false, false },
    { "B Low Cut",    "blowcut",  "Hz", 10.0f,  400.0f,  20.0f,   false, true },
    { "B High Cut",   "bhighcut", "Hz", 500.0f, 20000.0f, 8000.0f, false, true },
    { "Filter Sweep", "sweepon",    "",   0.0f,   1.0f,     0.0f,    true , false },
    { "Sweep Start",  "sweepstart", "ms", 0.0f,   1000.0f,  60.0f,   false, false },
    { "Sweep End",    "sweepend",   "ms", 0.0f,   1500.0f,  300.0f,  false, false },
    { "Sweep From",   "sweepfrom",  "Hz", 100.0f, 20000.0f, 16000.0f, false, true },
    { "Sweep To",     "sweepto",    "Hz", 100.0f, 20000.0f, 1500.0f, false, true },
    { "Sweep Res",    "sweepres",   "",   0.5f,   10.0f,    0.8f,    false, true },
    { "B Length",     "blength",    "ms", 50.0f,  2000.0f,  600.0f,  false, false },
};
static const char* const kModeNames[kModeCount] = { "Soft", "Hard", "Fold", "Asym" };
static const char* const kStateKeys[2] = { "sampleA", "sampleB" };

static inline float hkfShape(float x, int mode)
{
    switch (mode)
    {
    case kModeSoft: return std::tanh(x);
    case kModeHard: return x > 1.0f ? 1.0f : (x < -1.0f ? -1.0f : x);
    case kModeFold: { const float t = 6.0f * std::tanh(x / 6.0f); return std::sin(t * 1.5707963f); }
    case kModeAsym:
    default: {
        const float b = 0.35f;
        const float y = std::tanh(x + b) - std::tanh(b);
        return y > 0.0f ? y / (1.0f - std::tanh(b)) : y / (1.0f + std::tanh(b));
    }
    }
}

// Distortion chain: total drive split over the stages, each stage = gain -> clipper -> DC blocker
struct Chain {
    float dcX[4] = {}, dcY[4] = {};
    void reset() { std::memset(dcX, 0, sizeof(dcX)); std::memset(dcY, 0, sizeof(dcY)); }
    inline float process(float s, float stageGain, int stages, int mode, float dcR)
    {
        for (int st = 0; st < stages; ++st)
        {
            s = hkfShape(s * (st == 0 ? stageGain : stageGain * 1.5f), mode);
            const float y = s - dcX[st] + dcR * dcY[st];
            dcX[st] = s; dcY[st] = y; s = y;
        }
        return s;
    }
};
// ---- filters ----
struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void reset() { z1 = z2 = 0; }
    inline float process(float xf) { const double x = xf, y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return (float)y; }
    void set(double B0, double B1, double B2, double A0, double A1, double A2) { b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0; }
    void peaking(double fs, double f, double q, double gdb)
    { f = std::min(f, fs * 0.45); const double A = std::pow(10.0, gdb / 40.0), w = 2 * M_PI * f / fs, c = std::cos(w), al = std::sin(w) / (2 * q);
      set(1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A); }
    void lowpass(double fs, double f, double q)
    { f = std::min(f, fs * 0.45); const double w = 2 * M_PI * f / fs, c = std::cos(w), al = std::sin(w) / (2 * q); set((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al); }
    void highpass(double fs, double f, double q)
    { f = std::min(f, fs * 0.45); const double w = 2 * M_PI * f / fs, c = std::cos(w), al = std::sin(w) / (2 * q); set((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al); }
};

struct SvfCoefs { float a1 = 0, a2 = 0, a3 = 0, k = 1; };
static inline SvfCoefs svfCoefs(double fs, double f, double q)
{
    f = std::max(10.0, std::min(f, fs * 0.45));
    SvfCoefs c; const double g = std::tan(M_PI * f / fs), k = 1.0 / q, a1 = 1.0 / (1.0 + g * (g + k));
    c.a1 = (float)a1; c.a2 = (float)(g * a1); c.a3 = (float)(g * g * a1); c.k = (float)k; return c;
}
struct Svf {   // TPT state-variable filter
    float ic1 = 0, ic2 = 0;
    void reset() { ic1 = ic2 = 0; }
    inline void tick(float x, const SvfCoefs& c, float& lp, float& bp)
    { const float v3 = x - ic2, v1 = c.a1 * ic1 + c.a2 * v3, v2 = ic2 + c.a2 * ic1 + c.a3 * v3; ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2; lp = v2; bp = v1; }
    inline float bandpass(float x, const SvfCoefs& c) { float lp, bp; tick(x, c, lp, bp); return c.k * bp; }
    inline float lowpass(float x, const SvfCoefs& c) { float lp, bp; tick(x, c, lp, bp); return lp; }
};

// ---- one sample's full chain (same code for the DSP at 4x and the UI preview at 1x) ----
// tone EQ -> [gain -> clip -> DC block] x stages -> post tone EQ -> growl -> low-mid cleanup -> low cut -> high cut (opened by punch) -> output
struct ChainParams { float drive = 30; int stages = 3, mode = 1; float toneF = 1000, toneG = 0, toneQ = 1, punch = 0, growl = 0, formant = 900, motion = 0, lowCut = 20, highCut = 20000, outDb = 0; };

static inline ChainParams chainParamsFrom(const float* v, int slot)
{
    ChainParams c;
    const int d = slot == 0 ? pADrive : pBDrive, x = slot == 0 ? pAToneF : pBToneF;
    c.drive = v[d]; c.stages = (int)v[d + 1]; c.mode = (int)v[d + 2];
    if (slot == 0)
    {
        // the tok keeps tone + punch; no growl
        c.toneF = v[x]; c.toneG = v[x + 1]; c.toneQ = v[x + 2]; c.punch = v[x + 3] * 0.01f;
        c.growl = 0; c.formant = 900; c.motion = 0;
    }
    else
    {
        // the tail has a simpler set: Motion alone drives the growl (amount + movement)
        const float motion = v[x + 6] * 0.01f;
        c.toneF = 800; c.toneG = 0; c.toneQ = 1; c.punch = 0;
        c.growl = 0.6f * motion; c.formant = 800; c.motion = 1.2f * motion;
    }
    c.lowCut = v[x + 7]; c.highCut = v[x + 8];
    c.outDb = v[slot == 0 ? pAOut : pBOut];
    return c;
}

struct KickChain {
    Biquad eq[4], eqPost, demud, hp, lp1, lp2;
    Svf f1, f2;
    SvfCoefs gc1, gc2;
    float dcX[4] = {}, dcY[4] = {};
    double fs = 176400;
    ChainParams p;
    float stageGain = 1, postComp = 1, outGain = 1, dcR = 0.9995f, punchMix = 0, punchGain = 1;
    int growlDiv = 0, lpDiv = 0;

    void reset()
    {
        for (int i = 0; i < 4; ++i) { eq[i].reset(); dcX[i] = dcY[i] = 0; }
        eqPost.reset(); demud.reset(); hp.reset(); lp1.reset(); lp2.reset(); f1.reset(); f2.reset();
    }
    void setup(const ChainParams& np, double fsRun)
    {
        p = np; fs = fsRun;
        for (int st = 0; st < 4; ++st) eq[st].peaking(fs, p.toneF, p.toneQ, st == 0 ? p.toneG : p.toneG * 0.5);
        eqPost.peaking(fs, p.toneF, p.toneQ * 1.5, p.toneG * 0.6);
        postComp = std::pow(10.0f, -std::max(0.0f, p.toneG * 0.6f) * 0.5f * 0.05f);
        demud.peaking(fs, 180.0, 0.9, -3.0 * p.growl * 2.0);
        hp.highpass(fs, p.lowCut, 0.7071);
        lp1.lowpass(fs, p.highCut, 0.5412); lp2.lowpass(fs, p.highCut, 1.3066);
        lpDiv = 0;
        stageGain = std::pow(10.0f, p.drive / (float)std::max(1, p.stages) * 0.05f);
        outGain = std::pow(10.0f, p.outDb * 0.05f);
        dcR = (float)std::pow(0.9995, 176400.0 / fs);   // same DC-blocker corner at any rate
    }
    // call once per base-rate sample with the time since note-on
    inline void updateTime(float t, bool force = false)
    {
        punchMix = p.punch * std::exp(-t / 0.045f);
        punchGain = 1.0f + 0.4f * punchMix;
        // Punch opens the high cut at the start of the kick (cutoff glides, so no phase cancellation)
        if (p.punch > 0 && (force || ++lpDiv >= 8))
        {
            lpDiv = 0;
            const double open = std::min(20000.0, (double)p.highCut * std::pow(std::max(1.0, 16000.0 / p.highCut), (double)punchMix));
            lp1.lowpass(fs, open, 0.5412); lp2.lowpass(fs, open, 1.3066);
        }
        if (p.growl > 0 && (force || ++growlDiv >= 8))
        {
            growlDiv = 0;
            const float sweep = 1.0f - 1.4f * std::min(t / 0.35f, 1.0f);
            const float wob = std::sin(2.0f * 3.14159265f * 7.0f * t);
            const double f = p.formant * std::pow(2.0, (double)(p.motion * (sweep + 0.2f * wob)));
            gc1 = svfCoefs(fs, f, 5.0); gc2 = svfCoefs(fs, f * 2.4, 6.0);
        }
    }
    inline float process(float s)
    {
        const int stages = std::max(1, std::min(4, p.stages));
        for (int st = 0; st < stages; ++st)
        {
            s = eq[st].process(s);
            s = hkfShape(s * (st == 0 ? stageGain * punchGain : stageGain * 1.5f), p.mode);
            const float y = s - dcX[st] + dcR * dcY[st]; dcX[st] = s; dcY[st] = y; s = y;
        }
        s = eqPost.process(s) * postComp;
        if (p.growl > 0) { const float b = f1.bandpass(s, gc1) + 0.7f * f2.bandpass(s, gc2); s += p.growl * 2.0f * std::tanh(1.5f * b); }
        if (p.growl > 0) s = demud.process(s);
        s = hp.process(s);
        s = lp2.process(lp1.process(s));
        return s * outGain;
    }
};

// cutoff of the filter sweep at time t (ms), exponential glide between start and end
static inline float sweepCutoff(float tMs, float startMs, float endMs, float fromHz, float toHz)
{
    if (tMs <= startMs) return fromHz;
    if (tMs >= endMs || endMs <= startMs) return toHz;
    const float x = (tMs - startMs) / (endMs - startMs);
    return fromHz * std::pow(toHz / fromHz, x);
}

// Tok -> tail crossfade: before the fade the tail (B) is silent, after it the tok (A) is silent.
// Equal-power curve across [point - len/2, point + len/2], measured from the note-on.
static inline void fadeGains(float tSec, bool on, float pointMs, float lenMs, float& ga, float& gb)
{
    if (!on) { ga = gb = 1.0f; return; }
    const float t = tSec * 1000.0f, a = pointMs - 0.5f * lenMs, b = pointMs + 0.5f * lenMs;
    float x;
    if (t <= a) x = 0.0f; else if (t >= b) x = 1.0f; else x = (t - a) / std::max(1e-3f, b - a);
    ga = std::cos(x * 1.5707963f); gb = std::sin(x * 1.5707963f);
}

static inline float stageGainFor(float driveDb, int stages) { return std::pow(10.0f, driveDb / (float)stages * 0.05f); }

// ---- minimal WAV reader (PCM 8/16/24/32-bit int, 32/64-bit float, mono or stereo) ----
struct SampleData {
    std::vector<float> l, r;
    double sampleRate = 44100.0;
    size_t frames() const { return l.size(); }
};

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
static inline std::FILE* openFileUtf8(const char* path)
{
    wchar_t wpath[4096];
    if (MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, 4096) > 0)
        return _wfopen(wpath, L"rb");
    return std::fopen(path, "rb");
}
#else
static inline std::FILE* openFileUtf8(const char* path) { return std::fopen(path, "rb"); }
#endif

static inline bool loadWav(const char* path, SampleData& out, std::string& err)
{
    std::FILE* f = openFileUtf8(path);
    if (!f) { err = "can't open file"; return false; }
    std::vector<uint8_t> buf;
    std::fseek(f, 0, SEEK_END); long len = std::ftell(f); std::fseek(f, 0, SEEK_SET);
    if (len < 44 || len > 200 * 1024 * 1024) { std::fclose(f); err = "file size not supported"; return false; }
    buf.resize((size_t)len);
    const size_t got = std::fread(buf.data(), 1, (size_t)len, f); std::fclose(f);
    if (got != (size_t)len || std::memcmp(buf.data(), "RIFF", 4) || std::memcmp(buf.data() + 8, "WAVE", 4)) { err = "not a WAV file"; return false; }
    auto u16 = [&](size_t o) { return (uint32_t)buf[o] | ((uint32_t)buf[o + 1] << 8); };
    auto u32 = [&](size_t o) { return u16(o) | (u16(o + 2) << 16); };
    int fmt = 0, ch = 0, bits = 0; uint32_t sr = 0; size_t dataOff = 0, dataLen = 0;
    for (size_t o = 12; o + 8 <= buf.size();)
    {
        const uint32_t sz = u32(o + 4);
        if (!std::memcmp(buf.data() + o, "fmt ", 4) && o + 8 + 16 <= buf.size())
        {
            fmt = (int)u16(o + 8); ch = (int)u16(o + 10); sr = u32(o + 12); bits = (int)u16(o + 22);
            if (fmt == 0xFFFE && sz >= 40) fmt = (int)u16(o + 8 + 24);
        }
        else if (!std::memcmp(buf.data() + o, "data", 4))
        {
            dataOff = o + 8; dataLen = std::min<size_t>(sz, buf.size() - dataOff);
        }
        o += 8 + sz + (sz & 1);
    }
    if (!dataOff || ch < 1 || sr == 0 || bits == 0) { err = "unsupported WAV"; return false; }
    if (!((fmt == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32)) || (fmt == 3 && (bits == 32 || bits == 64))))
    { err = "unsupported WAV format"; return false; }
    const int bps = bits / 8;
    const size_t frames = dataLen / (size_t)(bps * ch);
    out.l.assign(frames, 0.0f); out.r.assign(frames, 0.0f); out.sampleRate = sr;
    const uint8_t* d = buf.data() + dataOff;
    auto rd = [&](const uint8_t* p) -> float {
        if (fmt == 3) { if (bits == 32) { float v; std::memcpy(&v, p, 4); return v; } double v; std::memcpy(&v, p, 8); return (float)v; }
        switch (bits) {
        case 8:  return ((int)p[0] - 128) / 128.0f;
        case 16: return (int16_t)(p[0] | (p[1] << 8)) / 32768.0f;
        case 24: { int32_t v = (p[0] << 8) | (p[1] << 16) | (p[2] << 24); return (v >> 8) / 8388608.0f; }
        default: { int32_t v; std::memcpy(&v, p, 4); return v / 2147483648.0f; }
        }
    };
    for (size_t i = 0; i < frames; ++i)
    {
        const uint8_t* p = d + i * (size_t)(bps * ch);
        out.l[i] = rd(p);
        out.r[i] = ch > 1 ? rd(p + bps) : out.l[i];
    }
    return true;
}

static inline std::string baseName(const char* path)
{
    std::string s(path ? path : "");
    const size_t k = s.find_last_of("/\\");
    return k == std::string::npos ? s : s.substr(k + 1);
}

// ---- pitch detection (fundamental of the kick body) ----
// tok = true : analyse the part right after the attack (the tok's pitched body)
// tok = false: analyse the sustained tail
static inline float estimatePitch(const SampleData& d, bool tok)
{
    const size_t n = d.frames();
    const double sr = d.sampleRate;
    if (n < (size_t)(0.05 * sr)) return 0.0f;
    // mono, low-passed (2x one-pole at 250 Hz) to keep the fundamental
    std::vector<float> x(n);
    const float c = 1.0f - std::exp(-2.0f * 3.14159265f * 250.0f / (float)sr);
    float a = 0, b = 0;
    for (size_t i = 0; i < n; ++i) { a += c * (0.5f * (d.l[i] + d.r[i]) - a); b += c * (a - b); x[i] = b; }
    // envelope peak
    const size_t hop = (size_t)(0.005 * sr);
    float peak = 0; size_t peakAt = 0;
    for (size_t i = 0; i + hop < n; i += hop) { float m = 0; for (size_t j = i; j < i + hop; ++j) m = std::max(m, std::fabs(x[j])); if (m > peak) { peak = m; peakAt = i; } }
    if (peak < 1e-4f) return 0.0f;
    size_t t0 = tok ? peakAt + (size_t)(0.02 * sr) : peakAt + (size_t)(0.08 * sr);
    size_t t1 = tok ? peakAt + (size_t)(0.20 * sr) : peakAt + (size_t)(0.60 * sr);
    t1 = std::min(t1, n);
    const size_t win = (size_t)(0.06 * sr), step = (size_t)(0.02 * sr);
    const int lagMin = (int)(sr / 200.0), lagMax = (int)(sr / 28.0);
    std::vector<float> est;
    for (size_t w = t0; w + win + (size_t)lagMax < t1 + (size_t)lagMax && w + win + (size_t)lagMax < n; w += step)
    {
        float m = 0; for (size_t j = w; j < w + win; ++j) m = std::max(m, std::fabs(x[j]));
        if (m < 0.1f * peak) break;                 // body has died away
        double e0 = 0; for (size_t j = w; j < w + win; ++j) e0 += (double)x[j] * x[j];
        double best = 0; int bestLag = 0;
        for (int lag = lagMin; lag <= lagMax; ++lag)
        {
            double r = 0, e1 = 0;
            for (size_t j = w; j < w + win; ++j) { r += (double)x[j] * x[j + lag]; e1 += (double)x[j + lag] * x[j + lag]; }
            const double nr = r / std::sqrt(e0 * e1 + 1e-12);
            if (nr > best) { best = nr; bestLag = lag; }
        }
        if (best > 0.8 && bestLag > 0) est.push_back((float)(sr / bestLag));
    }
    if (est.empty()) return 0.0f;
    std::sort(est.begin(), est.end());
    return est[est.size() / 2];
}

// semitones needed to move pitch 'from' onto the note of 'to', folded to the nearest octave (-6..+6 st)
static inline float matchSemitones(float from, float to)
{
    if (from <= 0 || to <= 0) return 0.0f;
    float st = 12.0f * std::log2(to / from);
    while (st > 6.0f) st -= 12.0f;
    while (st < -6.0f) st += 12.0f;
    return st;
}

static inline void noteName(float hz, char* buf, size_t n)
{
    if (hz <= 0) { std::snprintf(buf, n, "no pitch"); return; }
    static const char* const names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const float midi = 69.0f + 12.0f * std::log2(hz / 440.0f);
    const int m = (int)std::lround(midi);
    std::snprintf(buf, n, "%s%d %.1f Hz", names[((m % 12) + 12) % 12], m / 12 - 1, hz);
}

// tail length: the B sample is held until 'lenMs' from the note-on, then fades out over 30 ms
static inline float tailLengthGain(float tSec, float lenMs)
{
    const float t = tSec * 1000.0f;
    if (t <= lenMs) return 1.0f;
    if (t >= lenMs + 30.0f) return 0.0f;
    return 1.0f - (t - lenMs) / 30.0f;
}

// UI -> DSP preview requests (direct access, same process)
// code: 0 = A before, 1 = A after, 2 = B before, 3 = B after
struct PreviewTarget {
    virtual ~PreviewTarget() {}
    virtual void requestPreview(int code) = 0;
};
