// HardKick Forge 2 - UI
// Two sample panels (load, before/after view, full per-sample controls), tok->tail timeline with
// fade + filter sweep, and the A/B mix.

#include "DistrhoUI.hpp"
#include "DistrhoPlugin.hpp"
#include "Shared.hpp"
#include <chrono>
#include <memory>
#ifdef _WIN32
#include <shellapi.h>
#include <ole2.h>
#endif

START_NAMESPACE_DISTRHO

static constexpr float kW = DISTRHO_UI_DEFAULT_WIDTH;
static constexpr float kH = DISTRHO_UI_DEFAULT_HEIGHT;
static constexpr float kKnobR = 19.0f;
static constexpr int kBins = 520;

struct PanelGeom { float x, y, w, h; };
static const PanelGeom kPanel[2] = { { 12, 58, 574, 392 }, { 594, 58, 574, 392 } };

// knob grid inside each panel: 3 rows x 5 columns (slot-relative parameter ids, -1 = empty)

// timeline + bottom row
static constexpr float kTx = 24, kTy = 496, kTw = 1132, kTh = 118;
static constexpr int kTBins = 800;
static constexpr float kBottomY = 668;

class HardKickForge2UI : public UI
{
    struct Slot {
        std::shared_ptr<SampleData> data;
        std::string name, error;
        float seconds = 0, pitchHz = 0;
        float inMin[kBins] = {}, inMax[kBins] = {}, outMin[kBins] = {}, outMax[kBins] = {};
        std::vector<float> proc;   // processed mono (full chain incl. output level)
    };

public:
    HardKickForge2UI()
        : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT)
    {
        for (int i = 0; i < kParamCount; ++i) fValues[i] = kParams[i].def;
        loadSharedResources();
        const double sf = getScaleFactor();
        if (d_isNotEqual(sf, 1.0)) { setGeometryConstraints(kW * sf * 0.5, kH * sf * 0.5, true); setSize(kW * sf, kH * sf); }
        else setGeometryConstraints(kW * 0.5, kH * 0.5, true);
        installDropHandler();
    }

    ~HardKickForge2UI() override { removeDropHandler(); }

    // whole UI scales to fit the window (hosts may keep an older window size)
    float uiScale() const { return std::min(getWidth() / kW, getHeight() / kH); }
    float uiOffX() const { return 0.5f * (getWidth() - kW * uiScale()); }
    float lx(double x) const { return (float)((x - uiOffX()) / uiScale()); }
    float ly(double y) const { return (float)(y / uiScale()); }

    void setDropHover(float clientX)
    {
        const int k = lx(clientX) < kW * 0.5f ? 0 : 1;
        if (k != fDropHover) { fDropHover = k; repaint(); }
    }
    void clearDropHover() { if (fDropHover != -1) { fDropHover = -1; repaint(); } }
    void fileDropped(const char* path, float clientX) { loadSlot(lx(clientX) < kW * 0.5f ? 0 : 1, path); }

protected:
    // ---------------------------------------------------------------- host / DSP callbacks
    void parameterChanged(uint32_t index, float value) override
    {
        if (index >= kParamCount) return;
        fValues[index] = value;
        afterParamChange((int)index);
        repaint();
    }

    void stateChanged(const char* key, const char* value) override
    {
        for (int k = 0; k < 2; ++k)
            if (std::strcmp(key, kStateKeys[k]) == 0) { readSample(k, value); repaint(); }
    }

    void uiFileBrowserSelected(const char* filename) override
    {
        if (filename != nullptr && fPendingSlot >= 0) loadSlot(fPendingSlot, filename);
        fPendingSlot = -1;
    }

    // ---------------------------------------------------------------- parameter layout
    static int slotOf(int i)
    {
        if (i == pADrive || i == pAStages || i == pAMode || i == pAOut || (i >= pAToneF && i < pBToneF)) return 0;
        if (i == pBDrive || i == pBStages || i == pBMode || i == pBOut || i == pBLength || (i >= pBToneF && i < pSweepOn)) return 1;
        return -1;
    }
    static int gridParam(int k, int row, int col)
    {
        if (k == 0)
        {
            static const int A[3][5] = {
                { pADrive, pAStages, pAMode, pAPunch, pAOut },
                { pAToneF, pAToneG, pAToneQ, pALowCut, pAHighCut },
                { -1, -1, -1, -1, -1 } };
            return A[row][col];
        }
        static const int B[3][5] = {
            { pBDrive, pBStages, pBMode, pBOut, -1 },
            { pBLowCut, pBHighCut, pBLength, pBMotion, -1 },
            { -1, -1, -1, -1, -1 } };
        return B[row][col];
    }
    static const char* rowTitle(int k, int row)
    {
        static const char* const A[3] = { "DISTORTION", "TONE / CUT", "" };
        static const char* const B[3] = { "DISTORTION", "CUT / TAIL", "" };
        return (k == 0 ? A : B)[row];
    }
    void afterParamChange(int i)
    {
        const int k = slotOf(i);
        if (k >= 0) recomputeAfter(k); else recomputeTimeline();
    }

    // ---------------------------------------------------------------- sample handling
    void loadSlot(int k, const char* path)
    {
        setState(kStateKeys[k], path);
        readSample(k, path);
        repaint();
    }

    void readSample(int k, const char* path)
    {
        Slot& s = fSlot[k];
        s.name.clear(); s.error.clear(); s.data.reset(); s.seconds = 0; s.pitchHz = 0; s.proc.clear();
        if (path == nullptr || path[0] == '\0') { recomputeTimeline(); return; }
        auto sd = std::make_shared<SampleData>();
        std::string err;
        if (!loadWav(path, *sd, err)) { s.error = err; s.name = baseName(path); recomputeTimeline(); return; }
        s.data = sd; s.name = baseName(path);
        s.seconds = (float)(sd->frames() / sd->sampleRate);
        s.pitchHz = estimatePitch(*sd, k == 0);
        binify(*sd, nullptr, s.inMin, s.inMax);
        recomputeAfter(k);
    }

    void binify(const SampleData& d, const std::vector<float>* proc, float* mn, float* mx)
    {
        const size_t n = d.frames();
        for (int b = 0; b < kBins; ++b) { mn[b] = 0; mx[b] = 0; }
        if (n == 0) return;
        for (int b = 0; b < kBins; ++b)
        {
            const size_t a = (size_t)((double)b / kBins * n), e = std::max(a + 1, (size_t)((double)(b + 1) / kBins * n));
            float lo = 0, hi = 0;
            for (size_t i = a; i < e && i < n; ++i)
            {
                const float v = proc ? (*proc)[i] : 0.5f * (d.l[i] + d.r[i]);
                lo = std::min(lo, v); hi = std::max(hi, v);
            }
            mn[b] = lo; mx[b] = hi;
        }
    }

    // run the same chain offline for the "after" view
    void recomputeAfter(int k)
    {
        Slot& s = fSlot[k];
        if (!s.data) { recomputeTimeline(); return; }
        const SampleData& d = *s.data;
        std::vector<float> out(d.frames());
        KickChain ch; ch.reset(); ch.setup(chainParamsFrom(fValues, k), d.sampleRate);
        const float inv = 1.0f / (float)d.sampleRate;
        for (size_t i = 0; i < d.frames(); ++i) { ch.updateTime(i * inv, i == 0); out[i] = ch.process(0.5f * (d.l[i] + d.r[i])); }
        binify(d, &out, s.outMin, s.outMax);
        s.proc.swap(out);
        recomputeTimeline();
    }

    // ---------------------------------------------------------------- value helpers
    static float toNorm(int i, float v)
    {
        const ParamInfo& p = kParams[i];
        if (p.isLog) return std::log(v / p.min) / std::log(p.max / p.min);
        return (v - p.min) / (p.max - p.min);
    }
    static float fromNorm(int i, float n)
    {
        const ParamInfo& p = kParams[i]; n = std::max(0.0f, std::min(1.0f, n));
        float v = p.isLog ? p.min * std::pow(p.max / p.min, n) : p.min + n * (p.max - p.min);
        if (p.isInt) v = std::round(v);
        return v;
    }
    static void formatValue(int i, float v, char* buf, size_t n)
    {
        const ParamInfo& p = kParams[i];
        if (i == pAMode || i == pBMode) { std::snprintf(buf, n, "%s", kModeNames[(int)v]); return; }
        if (i == pAStages || i == pBStages) { std::snprintf(buf, n, "%d", (int)v); return; }
        if (i == pMix) { std::snprintf(buf, n, "A %.0f / B %.0f", 100 - v, v); return; }
        if (p.unit[0] == 'H') { if (v >= 1000.f) std::snprintf(buf, n, "%.1f kHz", v / 1000.f); else std::snprintf(buf, n, "%.0f Hz", v); return; }
        if (p.unit[0] == 'd') { std::snprintf(buf, n, "%+.1f dB", v); return; }
        if (p.unit[0] == '%') { std::snprintf(buf, n, "%.0f %%", v); return; }
        if (p.unit[0] == 'm') { std::snprintf(buf, n, "%.0f ms", v); return; }
        std::snprintf(buf, n, "%.2f", v);
    }
    static const char* shortName(int i)
    {
        const char* nm = kParams[i].name;
        return (slotOf(i) >= 0 && (nm[0] == 'A' || nm[0] == 'B') && nm[1] == ' ') ? nm + 2 : nm;
    }

    // knob positions (unscaled)
    bool knobPos(int i, float& x, float& y) const
    {
        for (int k = 0; k < 2; ++k)
            for (int r = 0; r < 3; ++r)
                for (int c = 0; c < 5; ++c)
                    if (gridParam(k, r, c) == i)
                    {
                        const PanelGeom& g = kPanel[k];
                        x = g.x + g.w * (0.1f + 0.2f * c); y = g.y + 236 + r * 94;
                        return true;
                    }
        static const struct { int p; float x; } bottom[] = {
            { pFadePoint, 70 }, { pFadeLen, 160 },
            { pSweepStart, 330 }, { pSweepEnd, 420 }, { pSweepFrom, 510 }, { pSweepTo, 600 }, { pSweepRes, 690 },
            { pMix, 980 } };
        for (const auto& b : bottom) if (b.p == i) { x = b.x; y = kBottomY; return true; }
        return false;
    }

    int hitKnob(float mx, float my) const
    {
        for (int i = 0; i < kParamCount; ++i)
        {
            float x, y;
            if (!knobPos(i, x, y)) continue;
            if (std::fabs(mx - x) < 36.f && my > y - 28.f && my < y + 44.f) return i;
        }
        return -1;
    }

    bool hitLoad(int k, float mx, float my) const
    {
        const PanelGeom& g = kPanel[k];
        return mx > g.x + g.w - 132 && mx < g.x + g.w - 12 && my > g.y + 8 && my < g.y + 34;
    }
    bool hitWave(int k, float mx, float my) const
    {
        const PanelGeom& g = kPanel[k];
        return mx > g.x + 12 && mx < g.x + g.w - 12 && my > g.y + 58 && my < g.y + 188;
    }

    void setParam(int i, float v)
    {
        if (d_isEqual(fValues[i], v)) return;
        fValues[i] = v; setParameterValue(i, v);
        afterParamChange(i);
        repaint();
    }
    void toggleParam(int i) { editParameter(i, true); setParam(i, fValues[i] > 0.5f ? 0.0f : 1.0f); editParameter(i, false); }

    // ---------------------------------------------------------------- click-to-preview
    void startPreview(int k, bool after)
    {
        if (PreviewTarget* const t = previewTarget()) t->requestPreview(k * 2 + (after ? 1 : 0));
        fPlayingSlot = k; fPlayingAfter = after;
        const double len = fSlot[k].seconds * (k == 1 && after ? 1.0 / pitchRatio() : 1.0);
        fPlayUntil = nowSec() + std::max(0.15, len);
        repaint();
    }
    static double nowSec() { using namespace std::chrono; return duration<double>(steady_clock::now().time_since_epoch()).count(); }
    PreviewTarget* previewTarget()
    {
        Plugin* const p = static_cast<Plugin*>(getPluginInstancePointer());
        return p ? dynamic_cast<PreviewTarget*>(p) : nullptr;
    }
    void uiIdle() override
    {
        if (fPlayingSlot >= 0 && nowSec() > fPlayUntil) { fPlayingSlot = -1; repaint(); }
    }

    // ---------------------------------------------------------------- timeline
    float timelineSeconds() const
    {
        float m = std::max(fSlot[0].seconds, fSlot[1].seconds * (float)(1.0 / pitchRatio()));
        return std::max(0.3f, std::min(1.5f, m > 0 ? m : 0.8f));
    }
    float tToX(float sec) const { return kTx + kTw * std::max(0.0f, std::min(1.0f, sec / timelineSeconds())); }
    float xToMs(float x) const { return std::max(0.0f, (x - kTx) / kTw * timelineSeconds() * 1000.0f); }
    bool hitTimeline(float mx, float my) const { return mx >= kTx && mx <= kTx + kTw && my >= kTy && my <= kTy + kTh; }
    double pitchRatio() const
    {
        if (fValues[pMatchPitch] < 0.5f) return 1.0;
        return std::pow(2.0, matchSemitones(fSlot[1].pitchHz, fSlot[0].pitchHz) / 12.0);
    }

    struct Button { float x, y, w; int param; const char* on; const char* off; Color col; };
    static const Button* buttons(int& n)
    {
        static const Button b[3] = {
            { 690, 466, 150, pMatchPitch, "MATCH PITCH: ON", "MATCH PITCH: OFF", Color(70, 150, 255) },
            { 848, 466, 150, pSweepOn, "SWEEP: ON", "SWEEP: OFF", Color(170, 90, 230) },
            { 1006, 466, 150, pFadeOn, "FADE: ON", "FADE: OFF", Color(255, 90, 50) } };
        n = 3; return b;
    }
    int hitButton(float mx, float my) const
    {
        int n; const Button* b = buttons(n);
        for (int i = 0; i < n; ++i) if (mx > b[i].x && mx < b[i].x + b[i].w && my > b[i].y && my < b[i].y + 24) return b[i].param;
        return -1;
    }

    void recomputeTimeline()
    {
        const float T = timelineSeconds();
        const bool on = fValues[pFadeOn] > 0.5f;
        const float mix = fValues[pMix] * 0.01f;
        const float mg[2] = { std::cos(mix * 1.5707963f), std::sin(mix * 1.5707963f) };
        const double ratio = pitchRatio();
        for (int k = 0; k < 2; ++k)
        {
            float* mn = fTMin[k]; float* mx = fTMax[k];
            const Slot& s = fSlot[k];
            for (int b = 0; b < kTBins; ++b) { mn[b] = 0; mx[b] = 0; }
            if (!s.data || s.proc.empty()) continue;
            const double sr = s.data->sampleRate * (k == 1 ? ratio : 1.0); const size_t n = s.proc.size();
            for (int b = 0; b < kTBins; ++b)
            {
                const double t0 = (double)b / kTBins * T, t1 = (double)(b + 1) / kTBins * T;
                const size_t i0 = (size_t)(t0 * sr), i1 = std::max(i0 + 1, (size_t)(t1 * sr));
                float lo = 0, hi = 0;
                for (size_t i = i0; i < i1 && i < n; ++i)
                {
                    const float ti = (float)(i / sr);
                    float ga, gb; fadeGains(ti, on, fValues[pFadePoint], fValues[pFadeLen], ga, gb);
                    const float v = s.proc[i] * mg[k] * (k == 0 ? ga : gb * tailLengthGain(ti, fValues[pBLength]));
                    lo = std::min(lo, v); hi = std::max(hi, v);
                }
                mn[b] = lo; mx[b] = hi;
            }
        }
    }

    // which timeline handle is under the mouse: 1 = sweep start, 2 = sweep end, 0 = fade point
    // 1 = sweep start, 2 = sweep end, 3 = tail length, 0 = fade point
    int timelineHandleAt(float mx) const
    {
        if (fValues[pSweepOn] > 0.5f)
        {
            const float xs = tToX(fValues[pSweepStart] / 1000.f), xe = tToX(fValues[pSweepEnd] / 1000.f);
            if (std::fabs(mx - xs) < 8) return 1;
            if (std::fabs(mx - xe) < 8) return 2;
        }
        if (std::fabs(mx - tToX(fValues[pBLength] / 1000.f)) < 8) return 3;
        return 0;
    }

    void drawTimeline()
    {
        const float px = 12, py = 458, pw = 1156, ph = 164;
        beginPath(); roundedRect(px, py, pw, ph, 8); fillColor(Color(236, 234, 240)); fill();
        fontSize(13); textAlign(ALIGN_LEFT | ALIGN_MIDDLE); fillColor(Color(60, 56, 66));
        text(px + 14, py + 20, "TOK  >  TAIL", nullptr);
        fontSize(11); fillColor(Color(110, 105, 115));
        text(px + 110, py + 20, "drag: white = fade point, blue = tail length, purple = sweep start / end   |   wheel = fade length", nullptr);
        // pitch readout
        {
            char info[96];
            const float st = matchSemitones(fSlot[1].pitchHz, fSlot[0].pitchHz);
            if (fSlot[0].pitchHz <= 0 || fSlot[1].pitchHz <= 0) std::snprintf(info, sizeof(info), "pitch: needs a pitched A and B");
            else if (fValues[pMatchPitch] > 0.5f) std::snprintf(info, sizeof(info), "tail shifted %+.1f st", st);
            else std::snprintf(info, sizeof(info), "tail is %+.1f st off", -st);
            textAlign(ALIGN_RIGHT | ALIGN_MIDDLE); fillColor(Color(90, 86, 96)); text(682, py + 20, info, nullptr);
        }
        int nb; const Button* b = buttons(nb);
        for (int i = 0; i < nb; ++i)
        {
            const bool on = fValues[b[i].param] > 0.5f;
            beginPath(); roundedRect(b[i].x, b[i].y, b[i].w, 24, 6); fillColor(on ? b[i].col : Color(40, 38, 46)); fill();
            fontSize(12); textAlign(ALIGN_CENTER | ALIGN_MIDDLE); fillColor(Color(255, 255, 255));
            text(b[i].x + b[i].w / 2, b[i].y + 12, on ? b[i].on : b[i].off, nullptr);
        }

        beginPath(); roundedRect(kTx, kTy, kTw, kTh, 6); fillColor(Color(12, 12, 15)); fill();
        const float T = timelineSeconds();
        fontSize(9); textAlign(ALIGN_LEFT | ALIGN_TOP);
        for (int ms = 100; ms < T * 1000; ms += 100)
        {
            const float gx = tToX(ms / 1000.0f);
            beginPath(); moveTo(gx, kTy + 2); lineTo(gx, kTy + kTh - 2); strokeColor(Color(255, 255, 255, 0.07f)); strokeWidth(1); stroke();
            fillColor(Color(120, 115, 125)); char bb[16]; std::snprintf(bb, sizeof(bb), "%d", ms); text(gx + 3, kTy + 3, bb, nullptr);
        }
        // fade region
        if (fValues[pFadeOn] > 0.5f)
        {
            const float p = fValues[pFadePoint] / 1000.0f, l = fValues[pFadeLen] / 1000.0f;
            const float x0 = tToX(p - l / 2), x1 = tToX(p + l / 2);
            beginPath(); rect(x0, kTy, std::max(1.0f, x1 - x0), kTh); fillColor(Color(255, 255, 255, 0.10f)); fill();
        }
        // waveforms
        const float cy = kTy + kTh / 2 + 4, amp = kTh * 0.42f, dx = kTw / (kTBins - 1);
        const Color cols[2] = { Color(255, 90, 50), Color(70, 150, 255) };
        for (int k = 1; k >= 0; --k)
        {
            if (!fSlot[k].data) continue;
            auto cl = [](float v) { return v > 1 ? 1.f : (v < -1 ? -1.f : v); };
            beginPath(); moveTo(kTx, cy - cl(fTMax[k][0]) * amp);
            for (int i = 1; i < kTBins; ++i) lineTo(kTx + i * dx, cy - cl(fTMax[k][i]) * amp);
            for (int i = kTBins - 1; i >= 0; --i) lineTo(kTx + i * dx, cy - cl(fTMin[k][i]) * amp);
            closePath(); fillColor(Color(cols[k].red, cols[k].green, cols[k].blue, 0.45f)); fill();
            strokeColor(cols[k]); strokeWidth(1); stroke();
        }
        // sweep: cutoff curve (log frequency, 100 Hz bottom .. 20 kHz top) and start/end handles
        if (fValues[pSweepOn] > 0.5f)
        {
            const Color pc(190, 110, 255);
            auto fy = [&](float hz) { return kTy + kTh - 6 - (kTh - 12) * std::log(hz / 100.0f) / std::log(200.0f); };
            beginPath();
            for (int i = 0; i <= 200; ++i)
            {
                const float t = T * i / 200.0f;
                const float hz = sweepCutoff(t * 1000.0f, fValues[pSweepStart], fValues[pSweepEnd], fValues[pSweepFrom], fValues[pSweepTo]);
                const float xx = kTx + kTw * i / 200.0f, yy = fy(hz);
                if (i == 0) moveTo(xx, yy); else lineTo(xx, yy);
            }
            strokeColor(pc); strokeWidth(2); stroke();
            for (int h = 0; h < 2; ++h)
            {
                const float xx = tToX(fValues[h == 0 ? pSweepStart : pSweepEnd] / 1000.0f);
                beginPath(); moveTo(xx, kTy); lineTo(xx, kTy + kTh); strokeColor(Color(190, 110, 255, 0.8f)); strokeWidth(1.5f); stroke();
                beginPath(); moveTo(xx - 6, kTy + kTh); lineTo(xx + 6, kTy + kTh); lineTo(xx, kTy + kTh - 9); closePath(); fillColor(pc); fill();
            }
        }
        // tail length handle (blue): where the B tail stops
        {
            const float xb = tToX(fValues[pBLength] / 1000.0f);
            beginPath(); moveTo(xb, kTy); lineTo(xb, kTy + kTh); strokeColor(Color(70, 150, 255, 0.85f)); strokeWidth(2); stroke();
            beginPath(); moveTo(xb - 7, kTy + kTh); lineTo(xb + 7, kTy + kTh); lineTo(xb, kTy + kTh - 10); closePath();
            fillColor(Color(70, 150, 255)); fill();
        }
        // fade point handle
        if (fValues[pFadeOn] > 0.5f)
        {
            const float xp = tToX(fValues[pFadePoint] / 1000.0f);
            beginPath(); moveTo(xp, kTy); lineTo(xp, kTy + kTh); strokeColor(Color(255, 255, 255, 0.9f)); strokeWidth(2); stroke();
            beginPath(); moveTo(xp - 7, kTy); lineTo(xp + 7, kTy); lineTo(xp, kTy + 9); closePath(); fillColor(Color(255, 255, 255)); fill();
        }
        tag(kTx + kTw - 172, kTy + kTh - 18, "A tok (orange) + B tail (blue)", Color(170, 165, 175));
    }

    // ---------------------------------------------------------------- drawing
    void onNanoDisplay() override
    {
        beginPath(); rect(0, 0, getWidth(), getHeight()); fillColor(Color(10, 9, 12)); fill();
        translate(uiOffX(), 0);
        const float s = uiScale();
        scale(s, s);
        beginPath(); rect(0, 0, kW, kH);
        fillPaint(linearGradient(0, 0, 0, kH, Color(22, 20, 26), Color(10, 9, 12))); fill();
        beginPath(); rect(0, 0, kW, 48);
        fillPaint(linearGradient(0, 0, 0, 48, Color(40, 14, 18), Color(20, 10, 12))); fill();
        beginPath(); moveTo(0, 48); lineTo(kW, 48); strokeColor(Color(255, 60, 40, 0.7f)); strokeWidth(2); stroke();

        fontFace(NANOVG_DEJAVU_SANS_TTF);
        fontSize(24); textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fillColor(Color(255, 90, 50)); text(18, 24, "HARDKICK", nullptr);
        fillColor(Color(235, 235, 240)); text(134, 24, "FORGE 2", nullptr);
        fontSize(12); fillColor(Color(150, 140, 150));
        text(244, 27, "two-sample hardstyle kick", nullptr);
        fontSize(11); textAlign(ALIGN_RIGHT | ALIGN_MIDDLE); fillColor(Color(120, 115, 125));
        text(kW - 18, 18, "play any note to trigger both samples   |   drop a .wav on a panel or click Load", nullptr);
        text(kW - 18, 34, "drag = adjust   shift+drag = fine   double-click = reset", nullptr);

        drawPanel(0, "A  -  TOK / BODY", Color(255, 90, 50));
        drawPanel(1, "B  -  BASS TAIL", Color(70, 150, 255));
        drawTimeline();

        // bottom row
        fontSize(11); textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fillColor(Color(255, 255, 255, 0.6f)); text(26, kBottomY - 36, "FADE", nullptr);
        fillColor(Color(190, 110, 255)); text(286, kBottomY - 36, "FILTER SWEEP", nullptr);
        drawKnob(pFadePoint, Color(235, 235, 240), false);
        drawKnob(pFadeLen, Color(235, 235, 240), false);
        for (int i = pSweepStart; i <= pSweepRes; ++i) drawKnob(i, Color(190, 110, 255), false);
        fontSize(13); textAlign(ALIGN_RIGHT | ALIGN_MIDDLE); fillColor(Color(255, 90, 50));
        text(980 - 52, kBottomY, "A", nullptr);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE); fillColor(Color(70, 150, 255));
        text(980 + 52, kBottomY, "B", nullptr);
        drawKnob(pMix, Color(200, 200, 210), false);


    }

    void drawPanel(int k, const char* title, Color col)
    {
        const PanelGeom& g = kPanel[k];
        beginPath(); roundedRect(g.x, g.y, g.w, g.h, 8); fillColor(Color(236, 234, 240)); fill();
        const Color dim(col.red * 0.85f, col.green * 0.85f, col.blue * 0.85f);
        fontSize(13); textAlign(ALIGN_LEFT | ALIGN_MIDDLE); fillColor(dim); text(g.x + 14, g.y + 20, title, nullptr);

        const bool hover = fHoverLoad == k;
        beginPath(); roundedRect(g.x + g.w - 132, g.y + 8, 120, 26, 6); fillColor(hover ? col : Color(40, 38, 46)); fill();
        fontSize(12); textAlign(ALIGN_CENTER | ALIGN_MIDDLE); fillColor(Color(255, 255, 255));
        text(g.x + g.w - 72, g.y + 21, "LOAD SAMPLE", nullptr);

        const Slot& s = fSlot[k];
        char info[256];
        if (!s.error.empty()) std::snprintf(info, sizeof(info), "%s - %s", s.name.c_str(), s.error.c_str());
        else if (s.data) { char nb[32]; noteName(s.pitchHz, nb, sizeof(nb)); std::snprintf(info, sizeof(info), "%s   (%.2f s, %s)", s.name.c_str(), s.seconds, nb); }
        else std::snprintf(info, sizeof(info), "No sample loaded");
        fontSize(12); textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fillColor(!s.error.empty() ? Color(200, 40, 30) : Color(40, 38, 46));
        text(g.x + 14, g.y + 44, info, nullptr);

        const float wx = g.x + 12, wy = g.y + 56, ww = g.w - 24, wh = 132;
        beginPath(); roundedRect(wx, wy, ww, wh, 6); fillColor(Color(12, 12, 15)); fill();
        fontSize(10); textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        const float laneH = (wh - 20) / 2, c1 = wy + 12 + laneH / 2, c2 = wy + 16 + laneH * 1.5f, amp = laneH * 0.46f;
        beginPath(); moveTo(wx + 4, c1); lineTo(wx + ww - 4, c1); moveTo(wx + 4, c2); lineTo(wx + ww - 4, c2);
        strokeColor(Color(255, 255, 255, 0.08f)); strokeWidth(1); stroke();
        if (!s.data)
        {
            fontSize(12); textAlign(ALIGN_CENTER | ALIGN_MIDDLE); fillColor(Color(120, 115, 125));
            text(wx + ww / 2, wy + wh / 2, k == 0 ? "Drop a punchy kick here (the tok)" : "Drop a kick with a long bass tail here", nullptr);
        }
        else
        {
            lane(s.inMin, s.inMax, wx + 6, ww - 12, c1, amp, Color(170, 170, 180, 0.6f), Color(200, 200, 210, 0.9f));
            lane(s.outMin, s.outMax, wx + 6, ww - 12, c2, amp, Color(col.red, col.green, col.blue, 0.55f), col);
        }
        tag(wx + 6, wy + 4, "BEFORE", Color(170, 170, 180));
        tag(wx + 6, wy + wh / 2 + 2, "AFTER", col);
        if (s.data)
        {
            fontSize(10); textAlign(ALIGN_RIGHT | ALIGN_TOP); fillColor(Color(140, 135, 145));
            text(wx + ww - 6, wy + 4, "click a lane to preview", nullptr);
        }
        if (fPlayingSlot == k)
        {
            const float ly0 = fPlayingAfter ? wy + wh / 2 : wy;
            beginPath(); roundedRect(wx + 1, ly0 + 1, ww - 2, wh / 2 - 2, 5);
            strokeColor(fPlayingAfter ? col : Color(220, 220, 230)); strokeWidth(2); stroke();
            tag(wx + 70, ly0 + (fPlayingAfter ? 2 : 4), "PLAYING", Color(255, 255, 255));
        }
        for (int r = 0; r < 3; ++r)
        {
            fontSize(10); textAlign(ALIGN_LEFT | ALIGN_MIDDLE); fillColor(dim);
            if (rowTitle(k, r)[0] != '\0') text(g.x + 14, g.y + 236 + r * 94 - 38, rowTitle(k, r), nullptr);
            for (int c = 0; c < 5; ++c) { const int p = gridParam(k, r, c); if (p >= 0) drawKnob(p, col, true); }
        }
        if (fDropHover == k) { beginPath(); roundedRect(g.x + 2, g.y + 2, g.w - 4, g.h - 4, 8); strokeColor(col); strokeWidth(3); stroke(); }
    }

    // small label on a dark backing so it stays readable over waveforms
    void tag(float x, float y, const char* t, Color c)
    {
        fontSize(10); textAlign(ALIGN_LEFT | ALIGN_TOP);
        DGL_NAMESPACE::Rectangle<float> b; textBounds(x + 4, y + 2, t, nullptr, b);
        beginPath(); roundedRect(x, y, b.getWidth() + 8, 14, 3); fillColor(Color(0, 0, 0, 0.7f)); fill();
        fillColor(c); text(x + 4, y + 2, t, nullptr);
    }

    void lane(const float* mn, const float* mx, float x, float w, float cy, float amp, Color f, Color e)
    {
        const float dx = w / (kBins - 1);
        auto cl = [](float v) { return v > 1 ? 1.f : (v < -1 ? -1.f : v); };
        beginPath(); moveTo(x, cy - cl(mx[0]) * amp);
        for (int i = 1; i < kBins; ++i) lineTo(x + i * dx, cy - cl(mx[i]) * amp);
        for (int i = kBins - 1; i >= 0; --i) lineTo(x + i * dx, cy - cl(mn[i]) * amp);
        closePath(); fillColor(f); fill(); strokeColor(e); strokeWidth(1); stroke();
    }

    void drawKnob(int i, Color col, bool darkText)
    {
        float x, y; if (!knobPos(i, x, y)) return;
        const float n = toNorm(i, fValues[i]);
        const float a0 = 0.75f * M_PI, a1 = 2.25f * M_PI, a = a0 + n * (a1 - a0);
        const bool active = (i == fDragParam || i == fHover);
        beginPath(); arc(x, y, kKnobR + 5, a0, a1, CW); strokeColor(Color(200, 197, 206, darkText ? 1.0f : 0.3f)); strokeWidth(3.5f); lineCap(ROUND); stroke();
        beginPath(); arc(x, y, kKnobR + 5, a0, a, CW); strokeColor(col); strokeWidth(3.5f); stroke();
        beginPath(); circle(x, y, kKnobR);
        fillPaint(radialGradient(x - 6, y - 7, 3, kKnobR * 1.3f, active ? Color(80, 76, 88) : Color(62, 58, 68), Color(24, 22, 28))); fill();
        beginPath();
        moveTo(x + std::cos(a) * kKnobR * 0.3f, y + std::sin(a) * kKnobR * 0.3f);
        lineTo(x + std::cos(a) * kKnobR * 0.85f, y + std::sin(a) * kKnobR * 0.85f);
        strokeColor(Color(240, 240, 245)); strokeWidth(2.5f); lineCap(ROUND); stroke();
        char buf[40]; formatValue(i, fValues[i], buf, sizeof(buf));
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fontSize(11); fillColor(darkText ? Color(30, 28, 34) : Color(210, 205, 215)); text(x, y + kKnobR + 14, i == pMix ? "Mix A/B" : shortName(i), nullptr);
        fontSize(10); fillColor(active ? col : (darkText ? Color(80, 76, 88) : Color(150, 145, 155))); text(x, y + kKnobR + 27, buf, nullptr);
    }

    // ---------------------------------------------------------------- mouse
    bool onMouse(const MouseEvent& ev) override
    {
        if (ev.button != 1) return false;
        const float mx = lx(ev.pos.getX()), my = ly(ev.pos.getY());
        if (ev.press)
        {
            for (int k = 0; k < 2; ++k)
                if (hitWave(k, mx, my) && fSlot[k].data)
                {
                    const PanelGeom& g = kPanel[k];
                    const bool after = my > g.y + 56 + 66;     // lower half of the view = AFTER lane
                    startPreview(k, after);
                    return true;
                }
            for (int k = 0; k < 2; ++k)
                if (hitLoad(k, mx, my) || (hitWave(k, mx, my) && !fSlot[k].data))
                {
                    fPendingSlot = k;
                    FileBrowserOptions opts;
                    opts.title = k == 0 ? "Load sample A (tok / body)" : "Load sample B (bass tail)";
                    openFileBrowser(opts);
                    return true;
                }
            const int bp = hitButton(mx, my);
            if (bp >= 0) { toggleParam(bp); return true; }
            if (hitTimeline(mx, my))
            {
                const int h = timelineHandleAt(mx);
                fTimelineParam = h == 1 ? pSweepStart : (h == 2 ? pSweepEnd : (h == 3 ? pBLength : pFadePoint));
                editParameter(fTimelineParam, true);
                setParam(fTimelineParam, std::min(kParams[fTimelineParam].max, xToMs(mx)));
                return true;
            }
            const int i = hitKnob(mx, my);
            if (i < 0) return false;
            if (i == fLastClickParam && ev.time - fLastClickTime < 350)
            {
                editParameter(i, true); setParam(i, kParams[i].def); editParameter(i, false);
                fLastClickParam = -1; return true;
            }
            fLastClickParam = i; fLastClickTime = ev.time;
            fDragParam = i; fDragStartY = my; fDragStartNorm = toNorm(i, fValues[i]);
            fDragFine = (ev.mod & kModifierShift) != 0;
            editParameter(i, true); repaint();
            return true;
        }
        if (fTimelineParam >= 0) { editParameter(fTimelineParam, false); fTimelineParam = -1; return true; }
        if (fDragParam >= 0) { editParameter(fDragParam, false); fDragParam = -1; repaint(); return true; }
        return false;
    }

    bool onMotion(const MotionEvent& ev) override
    {
        const float mx = lx(ev.pos.getX()), my = ly(ev.pos.getY());
        if (fTimelineParam >= 0) { setParam(fTimelineParam, std::min(kParams[fTimelineParam].max, xToMs(mx))); return true; }
        if (fDragParam >= 0)
        {
            const bool fine = (ev.mod & kModifierShift) != 0;
            if (fine != fDragFine) { fDragFine = fine; fDragStartY = my; fDragStartNorm = toNorm(fDragParam, fValues[fDragParam]); }
            const ParamInfo& p = kParams[fDragParam];
            const float range = p.isInt ? (p.max - p.min) * 30.f : (fine ? 1200.f : 220.f);
            setParam(fDragParam, fromNorm(fDragParam, fDragStartNorm + (fDragStartY - my) / range));
            return true;
        }
        const int h = hitKnob(mx, my);
        const int hl = hitLoad(0, mx, my) ? 0 : (hitLoad(1, mx, my) ? 1 : -1);
        if (h != fHover || hl != fHoverLoad) { fHover = h; fHoverLoad = hl; repaint(); }
        return false;
    }

    bool onScroll(const ScrollEvent& ev) override
    {
        const float mx = lx(ev.pos.getX()), my = ly(ev.pos.getY());
        if (hitTimeline(mx, my))
        {
            const float v = std::max(0.0f, std::min(kParams[pFadeLen].max, fValues[pFadeLen] + (ev.delta.getY() > 0 ? 5.0f : -5.0f)));
            editParameter(pFadeLen, true); setParam(pFadeLen, v); editParameter(pFadeLen, false);
            return true;
        }
        const int i = hitKnob(mx, my);
        if (i < 0) return false;
        const ParamInfo& p = kParams[i];
        float v = p.isInt ? fValues[i] + (ev.delta.getY() > 0 ? 1.f : -1.f)
                          : fromNorm(i, toNorm(i, fValues[i]) + ev.delta.getY() * ((ev.mod & kModifierShift) ? 0.005f : 0.03f));
        v = std::max(p.min, std::min(p.max, v));
        editParameter(i, true); setParam(i, v); editParameter(i, false);
        return true;
    }

    // ---------------------------------------------------------------- Windows drag & drop
#ifdef _WIN32
    // OLE drop target: accepts files dragged from FL Studio's browser and from Windows Explorer
    struct DropTarget : public IDropTarget
    {
        HardKickForge2UI* ui; HWND hwnd; LONG refs = 1; bool ok = false;
        DropTarget(HardKickForge2UI* u, HWND h) : ui(u), hwnd(h) {}
        virtual ~DropTarget() {}
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override
        {
            if (riid == IID_IUnknown || riid == IID_IDropTarget) { *ppv = static_cast<IDropTarget*>(this); AddRef(); return S_OK; }
            *ppv = nullptr; return E_NOINTERFACE;
        }
        ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)InterlockedIncrement(&refs); }
        ULONG STDMETHODCALLTYPE Release() override { const LONG r = InterlockedDecrement(&refs); if (r == 0) delete this; return (ULONG)r; }
        static bool hasFiles(IDataObject* d)
        {
            FORMATETC fe = { CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
            return d != nullptr && d->QueryGetData(&fe) == S_OK;
        }
        float clientX(POINTL pt) const { POINT p = { pt.x, pt.y }; ScreenToClient(hwnd, &p); return (float)p.x; }
        HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* d, DWORD, POINTL pt, DWORD* eff) override
        {
            ok = hasFiles(d); *eff = ok ? DROPEFFECT_COPY : DROPEFFECT_NONE;
            if (ok) ui->setDropHover(clientX(pt));
            return S_OK;
        }
        HRESULT STDMETHODCALLTYPE DragOver(DWORD, POINTL pt, DWORD* eff) override
        {
            *eff = ok ? DROPEFFECT_COPY : DROPEFFECT_NONE;
            if (ok) ui->setDropHover(clientX(pt));
            return S_OK;
        }
        HRESULT STDMETHODCALLTYPE DragLeave() override { ui->clearDropHover(); return S_OK; }
        HRESULT STDMETHODCALLTYPE Drop(IDataObject* d, DWORD, POINTL pt, DWORD* eff) override
        {
            ui->clearDropHover();
            *eff = DROPEFFECT_NONE;
            FORMATETC fe = { CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
            STGMEDIUM sm = {};
            if (d != nullptr && d->GetData(&fe, &sm) == S_OK)
            {
                HDROP hd = (HDROP)GlobalLock(sm.hGlobal);
                if (hd != nullptr)
                {
                    wchar_t wpath[4096];
                    if (DragQueryFileW(hd, 0, wpath, 4096) > 0)
                    {
                        char utf8[8192];
                        if (WideCharToMultiByte(CP_UTF8, 0, wpath, -1, utf8, sizeof(utf8), nullptr, nullptr) > 0)
                        {
                            ui->fileDropped(utf8, clientX(pt));
                            *eff = DROPEFFECT_COPY;
                        }
                    }
                    GlobalUnlock(sm.hGlobal);
                }
                ReleaseStgMedium(&sm);
            }
            return S_OK;
        }
    };

    void installDropHandler()
    {
        fHwnd = (HWND)getWindow().getNativeWindowHandle();
        if (!fHwnd) return;
        fOleInit = SUCCEEDED(OleInitialize(nullptr));  // S_FALSE if the host already initialised OLE on this thread
        fDrop = new DropTarget(this, fHwnd);
        if (RegisterDragDrop(fHwnd, fDrop) != S_OK)
        {
            // someone else registered already: fall back to the classic WM_DROPFILES route
            DragAcceptFiles(fHwnd, TRUE);
            SetPropW(fHwnd, L"HKF2_UI", (HANDLE)this);
            SetPropW(fHwnd, L"HKF2_OLD", (HANDLE)GetWindowLongPtrW(fHwnd, GWLP_WNDPROC));
            SetWindowLongPtrW(fHwnd, GWLP_WNDPROC, (LONG_PTR)&dropProc);
            fDrop->Release(); fDrop = nullptr;
        }
    }
    void removeDropHandler()
    {
        if (!fHwnd) return;
        if (fDrop) { RevokeDragDrop(fHwnd); fDrop->Release(); fDrop = nullptr; }
        else
        {
            const WNDPROC old = (WNDPROC)GetPropW(fHwnd, L"HKF2_OLD");
            if (old) SetWindowLongPtrW(fHwnd, GWLP_WNDPROC, (LONG_PTR)old);
            RemovePropW(fHwnd, L"HKF2_UI"); RemovePropW(fHwnd, L"HKF2_OLD");
            DragAcceptFiles(fHwnd, FALSE);
        }
        if (fOleInit) { OleUninitialize(); fOleInit = false; }
        fHwnd = nullptr;
    }
    static LRESULT CALLBACK dropProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
    {
        HardKickForge2UI* const ui = (HardKickForge2UI*)GetPropW(h, L"HKF2_UI");
        const WNDPROC old = (WNDPROC)GetPropW(h, L"HKF2_OLD");
        if (msg == WM_DROPFILES && ui != nullptr)
        {
            HDROP d = (HDROP)wp; wchar_t wpath[4096]; POINT pt = {};
            DragQueryPoint(d, &pt);
            if (DragQueryFileW(d, 0, wpath, 4096) > 0)
            {
                char utf8[8192];
                if (WideCharToMultiByte(CP_UTF8, 0, wpath, -1, utf8, sizeof(utf8), nullptr, nullptr) > 0)
                    ui->fileDropped(utf8, (float)pt.x);
            }
            DragFinish(d);
            return 0;
        }
        return old ? CallWindowProcW(old, h, msg, wp, lp) : DefWindowProcW(h, msg, wp, lp);
    }
    HWND fHwnd = nullptr;
    DropTarget* fDrop = nullptr;
    bool fOleInit = false;
#else
    void installDropHandler() {}
    void removeDropHandler() {}
#endif

private:
    Slot fSlot[2];
    float fValues[kParamCount];
    float fTMin[2][kTBins] = {}, fTMax[2][kTBins] = {};
    int fDropHover = -1, fDragParam = -1, fHover = -1, fHoverLoad = -1, fLastClickParam = -1, fPendingSlot = -1, fTimelineParam = -1;
    uint fLastClickTime = 0;
    float fDragStartY = 0, fDragStartNorm = 0;
    bool fDragFine = false, fPlayingAfter = false;
    int fPlayingSlot = -1;
    double fPlayUntil = 0;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HardKickForge2UI)
};

UI* createUI() { return new HardKickForge2UI(); }

END_NAMESPACE_DISTRHO
