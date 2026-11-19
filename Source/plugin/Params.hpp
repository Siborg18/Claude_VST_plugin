#pragma once
// Shared parameter definitions for DSP and UI.

enum ParamId {
    pDrive = 0,
    pStages,
    pMode,
    pEqFreq,
    pEqGain,
    pEqQ,
    pTail,
    pTailSize,
    pLowCut,
    pHighCut,
    pMix,
    pOutput,
    pClip,
    pPunch,
    pGrowl,
    pFormant,
    pMotion,
    pTailNote,
    kParamCount
};

enum ClipMode { kModeSoft = 0, kModeHard, kModeFold, kModeAsym, kModeCount };

struct ParamInfo {
    const char* name;
    const char* symbol;
    const char* unit;
    float min, max, def;
    bool isLog;
    bool isInt;
};

static const ParamInfo kParams[kParamCount] = {
    { "Drive",     "drive",    "dB",  0.0f,    60.0f,    36.0f,  false, false },
    { "Stages",    "stages",   "",    1.0f,    4.0f,     3.0f,   false, true  },
    { "Clip Mode", "mode",     "",    0.0f,    3.0f,     1.0f,   false, true  },
    { "Tone Freq", "eqfreq",   "Hz",  100.0f,  5000.0f,  1000.0f, true,  false },
    { "Tone Gain", "eqgain",   "dB", -12.0f,   24.0f,    10.0f,   false, false },
    { "Tone Q",    "eqq",      "",    0.3f,    8.0f,     1.2f,   true,  false },
    { "Tail",      "tail",     "%",   0.0f,    100.0f,   80.0f,  false, false },
    { "Tail Length", "tailsize", "ms", 100.0f,  1200.0f,  500.0f,  false, false },
    { "Low Cut",   "lowcut",   "Hz",  10.0f,   150.0f,   20.0f,  true,  false },
    { "High Cut",  "highcut",  "Hz",  500.0f,  20000.0f, 8000.0f, true,  false },
    { "Mix",       "mix",      "%",   0.0f,    100.0f,   100.0f, false, false },
    { "Output",    "output",   "dB", -24.0f,   12.0f,    0.0f,   false, false },
    { "Clipper",   "clipper",  "",    0.0f,    1.0f,     1.0f,   false, true  },
    { "Punch",     "punch",    "%",   0.0f,    100.0f,   60.0f,  false, false },
    { "Growl",     "growl",    "%",   0.0f,    100.0f,   35.0f,  false, false },
    { "Formant",   "formant",  "Hz",  200.0f,  3000.0f,  900.0f, true,  false },
    { "Motion",    "motion",   "%",   0.0f,    100.0f,   60.0f,  false, false },
    { "Tail Note", "tailnote", "",    0.0f,    12.0f,    0.0f,   false, true  },
};

static const char* const kNoteNames[13] = { "Auto", "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
static const char* const kModeNames[kModeCount] = { "Soft", "Hard", "Fold", "Asym" };

// Factory presets (values in parameter order)
struct Preset { const char* name; float v[kParamCount]; };
static const Preset kPresets[] = {
    //   Drive, Stages, Mode, Freq, Gain, Q, Tail, Length(ms), LoCut, HiCut, Mix, Out, Clip, Punch, Growl, Formant, Motion, TailNote
    { "Init",           { 36, 3, 1, 1000, 10, 1.2f, 80, 500, 20, 8000, 100, 0, 1, 60, 35, 900, 60, 0 } },
    // tuned against the RHSP reference kicks, using clean input kicks (ATE Kick 050, kick-heavy, kick-big, 909 Kick 3)
    { "Euphoric (Ref Match)", { 48, 3, 1, 2200, 16, 2.5f, 70, 550, 20, 15000, 100, 2, 1, 30, 20, 1100, 50, 0 } },
    { "Raw (Ref Match)",      { 48, 4, 0, 800, 8, 0.8f, 90, 450, 20, 11000, 100, -2, 1, 30, 50, 800, 80, 0 } },
    { "Uptempo (Ref Match)",  { 24, 4, 1, 800, 16, 0.8f, 80, 300, 20, 15000, 100, 2, 1, 30, 50, 600, 80, 0 } },
    { "Rawstyle Hard",  { 42, 3, 1, 1400, 12, 1.5f, 90, 450, 20, 8000, 100, 0, 1, 80, 50, 900, 80, 0 } },
    { "Euphoric Punch", { 36, 2, 0, 1000, 10, 1.0f, 70, 500, 20, 10000, 100, -1, 1, 70, 20, 1000, 40, 0 } },
    { "Screech Fold",   { 16, 2, 2, 1800, 10, 2.0f, 60, 400, 20, 8000, 100, -1, 1, 50, 40, 1500, 60, 0 } },
    { "Asym Crunch",    { 40, 3, 3, 1100, 10, 1.4f, 80, 450, 20, 9000, 100, 0, 1, 60, 40, 800, 70, 0 } },
    { "Long Tail",      { 36, 3, 1, 900, 10, 1.2f, 100, 900, 20, 7000, 100, 0, 1, 50, 35, 900, 60, 0 } },
    { "Uptempo",        { 45, 4, 1, 1600, 14, 1.8f, 80, 300, 25, 10000, 100, 2, 1, 90, 45, 1000, 80, 0 } },
};
static const unsigned kPresetCount = sizeof(kPresets) / sizeof(kPresets[0]);

// Shared waveshaper (used by DSP and by the UI curve display)
#include <cmath>
static inline float hkfShape(float x, int mode)
{
    switch (mode)
    {
    case kModeSoft: return std::tanh(x);
    case kModeHard: return x > 1.0f ? 1.0f : (x < -1.0f ? -1.0f : x);
    case kModeFold: { const float t = 6.0f * std::tanh(x / 6.0f); return std::sin(t * 1.5707963f); } // capped sine wavefolder
    case kModeAsym:
    default: {
        const float b = 0.35f;
        const float y = std::tanh(x + b) - std::tanh(b);
        return y > 0.0f ? y / (1.0f - std::tanh(b)) : y / (1.0f + std::tanh(b));
    }
    }
}
