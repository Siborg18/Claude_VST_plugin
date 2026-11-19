// HardKick Forge - NanoVG user interface

#include "DistrhoUI.hpp"
#include "Params.hpp"
#include "Scope.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include "DistrhoPlugin.hpp"

START_NAMESPACE_DISTRHO

static constexpr float kW = DISTRHO_UI_DEFAULT_WIDTH;
static constexpr float kH = DISTRHO_UI_DEFAULT_HEIGHT;

// Knob layout (unscaled coordinates)
static constexpr float kKnobR = 30.0f;

struct Group { const char* title; int params[4]; int count; Color col; };

// explicit knob layout (x, y) per parameter; pClip is a button, not a knob
static const float kKnobXY[kParamCount][2] = {
    {  60, 165 }, { 148, 165 }, { 236, 165 },          // Drive, Stages, Mode
    { 424, 165 }, { 512, 165 }, { 600, 165 },          // Tone Freq, Gain, Q
    {  56, 340 }, { 140, 340 },                        // Tail, Tail Length
    { 318, 340 }, { 402, 340 }, { 486, 340 }, { 570, 340 }, // Low Cut, High Cut, Mix, Output
    { -1000, -1000 },                                  // Clipper (button)
    { 324, 165 },                                      // Punch
    { 664, 340 }, { 748, 340 }, { 832, 340 },          // Growl, Formant, Motion
    { 224, 340 },                                      // Tail Note
};

class HardKickForgeUI : public UI
{
public:
    HardKickForgeUI()
        : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT)
    {
        for (int i = 0; i < kParamCount; ++i) fValues[i] = kParams[i].def;
        loadSharedResources();

        const double sf = getScaleFactor();
        if (d_isNotEqual(sf, 1.0))
        {
            setGeometryConstraints(kW * sf * 0.6, kH * sf * 0.6, true);
            setSize(kW * sf, kH * sf);
        }
        else
        {
            setGeometryConstraints(kW * 0.6, kH * 0.6, true);
        }
    }

protected:
    void uiIdle() override
    {
        const ScopeProvider* const sp = scopeProvider();
        if (sp == nullptr) return;
        const KickScope& ks = sp->getScope();
        const uint32_t v = ks.version.load(std::memory_order_acquire);
        if (v == fScopeVersion) return;
        fScopeVersion = v;
        fScopeUsed = ks.used;
        std::memcpy(fInMin, ks.inMin, sizeof(fInMin));
        std::memcpy(fInMax, ks.inMax, sizeof(fInMax));
        std::memcpy(fOutMin, ks.outMin, sizeof(fOutMin));
        std::memcpy(fOutMax, ks.outMax, sizeof(fOutMax));
        repaint();
    }

    const ScopeProvider* scopeProvider()
    {
        if (fScopeChecked) return fScopeProv;
        fScopeChecked = true;
        if (Plugin* const p = static_cast<Plugin*>(getPluginInstancePointer()))
            fScopeProv = dynamic_cast<ScopeProvider*>(p);
        return fScopeProv;
    }

    void parameterChanged(uint32_t index, float value) override
    {
        if (index >= kParamCount) return;
        fValues[index] = value;
        repaint();
    }

    void programLoaded(uint32_t index) override
    {
        if (index >= kPresetCount) return;
        for (int i = 0; i < kParamCount; ++i) fValues[i] = kPresets[index].v[i];
        fPresetIdx = (int)index;
        repaint();
    }

    // -------------------------------------------------------------------------------------
    // value <-> normalized

    static float toNorm(int i, float v)
    {
        const ParamInfo& p = kParams[i];
        if (p.isLog) return std::log(v / p.min) / std::log(p.max / p.min);
        return (v - p.min) / (p.max - p.min);
    }

    static float fromNorm(int i, float n)
    {
        const ParamInfo& p = kParams[i];
        if (n < 0.f) n = 0.f;
        if (n > 1.f) n = 1.f;
        float v = p.isLog ? p.min * std::pow(p.max / p.min, n) : p.min + n * (p.max - p.min);
        if (p.isInt) v = std::round(v);
        return v;
    }

    static void formatValue(int i, float v, char* buf, size_t n)
    {
        const ParamInfo& p = kParams[i];
        if (i == pMode) { std::snprintf(buf, n, "%s", kModeNames[(int)v]); return; }
        if (i == pTailNote) { std::snprintf(buf, n, "%s", kNoteNames[(int)v]); return; }
        if (i == pTailSize) { std::snprintf(buf, n, "%.0f ms", v); return; }
        if (i == pStages) { std::snprintf(buf, n, "%d", (int)v); return; }
        if (p.unit[0] == 'H') {
            if (v >= 1000.f) std::snprintf(buf, n, "%.1f kHz", v / 1000.f);
            else std::snprintf(buf, n, "%.0f Hz", v);
            return;
        }
        if (p.unit[0] == 'd') { std::snprintf(buf, n, "%+.1f dB", v); return; }
        if (p.unit[0] == '%') { std::snprintf(buf, n, "%.0f %%", v); return; }
        std::snprintf(buf, n, "%.2f", v);
    }

    void knobPos(int i, float& x, float& y) const
    {
        x = kKnobXY[i][0];
        y = kKnobXY[i][1];
    }

    int hitKnob(float mx, float my) const
    {
        for (int i = 0; i < kParamCount; ++i)
        {
            if (i == pClip) continue;
            float x, y; knobPos(i, x, y);
            if (std::fabs(mx - x) < 42.f && my > y - 40.f && my < y + 58.f) return i;
        }
        return -1;
    }

    void setParam(int i, float v)
    {
        if (d_isEqual(fValues[i], v)) return;
        fValues[i] = v;
        setParameterValue(i, v);
        repaint();
    }

    // -------------------------------------------------------------------------------------
    // Drawing

    void onNanoDisplay() override
    {
        const float s = getWidth() / kW;
        scale(s, s);

        // background
        beginPath(); rect(0, 0, kW, kH);
        fillPaint(linearGradient(0, 0, 0, kH, Color(22, 20, 26), Color(10, 9, 12)));
        fill();

        // header
        beginPath(); rect(0, 0, kW, 56);
        fillPaint(linearGradient(0, 0, 0, 56, Color(40, 14, 18), Color(20, 10, 12)));
        fill();
        beginPath(); moveTo(0, 56); lineTo(kW, 56); strokeColor(Color(255, 60, 40, 0.706f)); strokeWidth(2); stroke();

        fontFace(NANOVG_DEJAVU_SANS_TTF);
        fontSize(26); textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fillColor(Color(255, 90, 50)); text(22, 28, "HARDKICK", nullptr);
        fillColor(Color(235, 235, 240)); text(146, 28, "FORGE", nullptr);
        fontSize(12); fillColor(Color(150, 140, 150));
        text(232, 31, "hardstyle kick distortion", nullptr);
        drawPresetBar();

        // groups
        const Group groups[] = {
            { "DISTORTION", { pDrive, pStages, pMode, pPunch }, 4, Color(225, 60, 25) },
            { "TONE", { pEqFreq, pEqGain, pEqQ, 0 }, 3, Color(205, 125, 0) },
            { "TAIL", { pTail, pTailSize, pTailNote, 0 }, 3, Color(35, 120, 215) },
            { "OUTPUT", { pLowCut, pHighCut, pMix, pOutput }, 4, Color(30, 150, 65) },
            { "GROWL", { pGrowl, pFormant, pMotion, 0 }, 3, Color(150, 60, 200) },
        };
        for (const Group& g : groups)
        {
            float x0, y0, x1, y1; knobPos(g.params[0], x0, y0); knobPos(g.params[g.count - 1], x1, y1);
            const float left = x0 - 44, right = x1 + 44, top = y0 - 72, bottom = y0 + 70;
            beginPath(); roundedRect(left, top, right - left, bottom - top, 8);
            fillColor(Color(236, 234, 240)); fill();
            strokeColor(Color(255, 255, 255, 0.300f)); strokeWidth(1); stroke();
            fontSize(12); textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
            fillColor(g.col); text(left + 12, top + 14, g.title, nullptr);
            for (int j = 0; j < g.count; ++j) drawKnob(g.params[j], g.col);
        }

        drawCurve();
        drawClipButton();
        drawKickView();
    }

    // Before / after kick waveform (like the sampler waveform view)
    void drawKickView()
    {
        const float x = 16, y = 440, w = kW - 32, h = 118;
        beginPath(); roundedRect(x, y, w, h, 8);
        fillColor(Color(8, 8, 10)); fill();
        strokeColor(Color(255, 255, 255, 0.12f)); strokeWidth(1); stroke();

        fontSize(11); textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fillColor(Color(150, 150, 160)); text(x + 12, y + 12, "BEFORE", nullptr);
        fillColor(Color(255, 90, 50));   text(x + 70, y + 12, "AFTER", nullptr);
        fillColor(Color(110, 105, 115)); textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
        text(x + w - 12, y + 12, "last kick, 0.8 s", nullptr);

        const float lx = x + 10, lw = w - 20;
        const float laneH = (h - 30) / 2.0f;
        const float y1 = y + 22 + laneH * 0.5f;          // before lane centre
        const float y2 = y + 26 + laneH * 1.5f;          // after lane centre
        const float amp = laneH * 0.48f;

        // centre lines + time ticks every 100 ms
        beginPath();
        moveTo(lx, y1); lineTo(lx + lw, y1);
        moveTo(lx, y2); lineTo(lx + lw, y2);
        for (int t = 1; t < 8; ++t) { const float tx = lx + lw * t / 8.0f; moveTo(tx, y + 22); lineTo(tx, y + h - 6); }
        strokeColor(Color(255, 255, 255, 0.07f)); strokeWidth(1); stroke();

        if (fScopeUsed <= 1)
        {
            fontSize(12); textAlign(ALIGN_CENTER | ALIGN_MIDDLE); fillColor(Color(110, 105, 115));
            text(x + w / 2, y + h / 2 + 6, "Play a kick to see it before and after the plugin", nullptr);
            return;
        }

        const int n = fScopeUsed;
        const float dx = lw / (float)KickScope::kBins;
        auto lane = [&](const float* mn, const float* mx, float cy, Color fill_, Color edge)
        {
            beginPath();
            moveTo(lx, cy - clampA(mx[0]) * amp);
            for (int i = 1; i < n; ++i) lineTo(lx + i * dx, cy - clampA(mx[i]) * amp);
            for (int i = n - 1; i >= 0; --i) lineTo(lx + i * dx, cy - clampA(mn[i]) * amp);
            closePath();
            fillColor(fill_); fill();
            strokeColor(edge); strokeWidth(1); stroke();
        };
        lane(fInMin, fInMax, y1, Color(170, 170, 180, 0.55f), Color(200, 200, 210, 0.8f));
        lane(fOutMin, fOutMax, y2, Color(255, 80, 40, 0.55f), Color(255, 120, 80, 0.9f));
    }

    static float clampA(float v) { return v > 1.0f ? 1.0f : (v < -1.0f ? -1.0f : v); }

    bool hitClipButton(float mx, float my) const
    {
        return mx > kW - 170 && mx < kW - 20 && my > 14 && my < 42;
    }

    // ---- built-in preset selector: works in every host, no host preset menu needed ----
    static constexpr float kPbX = 420, kPbY = 14, kPbW = 290, kPbH = 28;

    int matchingPreset() const
    {
        for (unsigned p = 0; p < kPresetCount; ++p)
        {
            bool same = true;
            for (int i = 0; i < kParamCount && same; ++i)
                if (std::fabs(fValues[i] - kPresets[p].v[i]) > 0.01f * (kParams[i].max - kParams[i].min) + 1e-4f) same = false;
            if (same) return (int)p;
        }
        return -1;
    }

    void drawPresetBar()
    {
        beginPath(); roundedRect(kPbX, kPbY, kPbW, kPbH, 6);
        fillColor(Color(0, 0, 0, 0.35f)); fill();
        strokeColor(Color(255, 255, 255, 0.18f)); strokeWidth(1); stroke();
        // arrows
        const float cy = kPbY + kPbH / 2;
        beginPath(); moveTo(kPbX + 18, cy - 6); lineTo(kPbX + 10, cy); lineTo(kPbX + 18, cy + 6); closePath();
        fillColor(Color(235, 235, 240)); fill();
        beginPath(); moveTo(kPbX + kPbW - 18, cy - 6); lineTo(kPbX + kPbW - 10, cy); lineTo(kPbX + kPbW - 18, cy + 6); closePath();
        fill();
        const int m = matchingPreset();
        fontSize(13); textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(m >= 0 ? Color(235, 235, 240) : Color(170, 165, 175));
        char buf[64];
        if (m >= 0) std::snprintf(buf, sizeof(buf), "%s", kPresets[m].name);
        else if (fPresetIdx >= 0) std::snprintf(buf, sizeof(buf), "%s *", kPresets[fPresetIdx].name);
        else std::snprintf(buf, sizeof(buf), "Custom");
        text(kPbX + kPbW / 2, cy + 1, buf, nullptr);
    }

    // returns -1 (prev), +1 (next), 0 (miss)
    int hitPresetBar(float mx, float my) const
    {
        if (my < kPbY || my > kPbY + kPbH || mx < kPbX || mx > kPbX + kPbW) return 0;
        return mx < kPbX + 36 ? -1 : +1;
    }

    void applyPreset(int idx)
    {
        idx = (idx % (int)kPresetCount + (int)kPresetCount) % (int)kPresetCount;
        fPresetIdx = idx;
        for (int i = 0; i < kParamCount; ++i)
        {
            editParameter(i, true);
            fValues[i] = kPresets[idx].v[i];
            setParameterValue(i, fValues[i]);
            editParameter(i, false);
        }
        repaint();
    }

    void drawClipButton()
    {
        const bool on = fValues[pClip] > 0.5f;
        const float x = kW - 170, y = 14, w = 150, h = 28;
        beginPath(); roundedRect(x, y, w, h, 6);
        fillColor(on ? Color(255, 70, 40, 0.784f) : Color(255, 255, 255, 0.071f)); fill();
        strokeColor(on ? Color(255, 120, 90) : Color(255, 255, 255, 0.196f)); strokeWidth(1); stroke();
        fontSize(12); textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(on ? Color(255, 255, 255) : Color(160, 155, 165));
        text(x + w / 2, y + h / 2, on ? "FINAL CLIPPER: ON" : "FINAL CLIPPER: OFF", nullptr);
    }

    void drawKnob(int i, const Color& col)
    {
        float x, y; knobPos(i, x, y);
        const float n = toNorm(i, fValues[i]);
        const float a0 = 0.75f * M_PI, a1 = 2.25f * M_PI;
        const float a = a0 + n * (a1 - a0);
        const bool active = (i == fDragParam || i == fHover);

        // track
        beginPath(); arc(x, y, kKnobR + 6, a0, a1, CW);
        strokeColor(Color(200, 197, 206)); strokeWidth(4); lineCap(ROUND); stroke();
        // value arc
        beginPath(); arc(x, y, kKnobR + 6, a0, a, CW);
        strokeColor(col); strokeWidth(4); stroke();

        // body
        beginPath(); circle(x, y, kKnobR);
        fillPaint(radialGradient(x - 8, y - 10, 4, kKnobR * 1.3f,
                                 active ? Color(80, 76, 88) : Color(62, 58, 68), Color(24, 22, 28)));
        fill();
        strokeColor(Color(0, 0, 0, 0.627f)); strokeWidth(1.5f); stroke();

        // pointer
        beginPath();
        moveTo(x + std::cos(a) * (kKnobR * 0.35f), y + std::sin(a) * (kKnobR * 0.35f));
        lineTo(x + std::cos(a) * (kKnobR * 0.85f), y + std::sin(a) * (kKnobR * 0.85f));
        strokeColor(Color(240, 240, 245)); strokeWidth(3); lineCap(ROUND); stroke();

        // labels
        fontSize(12); textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(Color(30, 28, 34)); text(x, y + kKnobR + 18, kParams[i].name, nullptr);
        char buf[32]; formatValue(i, fValues[i], buf, sizeof(buf));
        fontSize(11); fillColor(active ? col : Color(80, 76, 88)); text(x, y + kKnobR + 33, buf, nullptr);
    }

    void drawCurve()
    {
        const float bx = 662, by = 93, bw = 222, bh = 142;
        beginPath(); roundedRect(bx, by, bw, bh, 8);
        fillColor(Color(0, 0, 0, 0.431f)); fill();
        strokeColor(Color(255, 255, 255, 0.086f)); strokeWidth(1); stroke();

        fontSize(11); textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fillColor(Color(255, 80, 40)); text(bx + 12, by + 14, "TRANSFER CURVE", nullptr);

        const float cx = bx + bw / 2, cy = by + 26 + (bh - 44) / 2, rx = (bw - 40) / 2, ry = (bh - 50) / 2;
        // grid
        beginPath();
        moveTo(cx - rx, cy); lineTo(cx + rx, cy);
        moveTo(cx, cy - ry); lineTo(cx, cy + ry);
        strokeColor(Color(255, 255, 255, 0.098f)); strokeWidth(1); stroke();
        beginPath(); rect(cx - rx, cy - ry, 2 * rx, 2 * ry); stroke();

        // transfer curve through all stages (EQ ignored)
        const int stages = (int)fValues[pStages];
        const int mode = (int)fValues[pMode];
        const float g = std::pow(10.f, fValues[pDrive] / (float)stages * 0.05f);
        beginPath();
        const int N = 200;
        for (int k = 0; k <= N; ++k)
        {
            const float xin = -1.f + 2.f * k / N;
            float s = xin;
            for (int st = 0; st < stages; ++st) s = hkfShape(s * (st == 0 ? g : g * 1.5f), mode);
            if (s > 1.2f) s = 1.2f;
            if (s < -1.2f) s = -1.2f;
            const float px = cx + xin * rx, py = cy - s * ry * 0.8f;
            if (k == 0) moveTo(px, py); else lineTo(px, py);
        }
        strokeColor(Color(255, 90, 50)); strokeWidth(2.5f); lineJoin(ROUND); stroke();

        fontSize(11); textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(Color(150, 145, 155));
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%s  x%d  %.0f dB", kModeNames[mode], stages, fValues[pDrive]);
        text(cx, by + bh - 10, buf, nullptr);
        fontSize(10); fillColor(Color(110, 105, 115));
        text(cx, kH - 12, "drag = adjust   shift+drag = fine   double-click = reset", nullptr);
    }

    // -------------------------------------------------------------------------------------
    // Mouse

    bool onMouse(const MouseEvent& ev) override
    {
        if (ev.button != 1) return false;
        const float s = getWidth() / kW;
        const float mx = ev.pos.getX() / s, my = ev.pos.getY() / s;

        if (ev.press)
        {
            if (const int dir = hitPresetBar(mx, my))
            {
                const int cur = matchingPreset() >= 0 ? matchingPreset() : (fPresetIdx >= 0 ? fPresetIdx : 0);
                applyPreset(cur + dir);
                return true;
            }
        }

        if (ev.press && hitClipButton(mx, my))
        {
            editParameter(pClip, true);
            setParam(pClip, fValues[pClip] > 0.5f ? 0.0f : 1.0f);
            editParameter(pClip, false);
            return true;
        }

        if (ev.press)
        {
            const int i = hitKnob(mx, my);
            if (i < 0) return false;

            // double click -> reset to default
            if (i == fLastClickParam && ev.time - fLastClickTime < 350)
            {
                editParameter(i, true);
                setParam(i, kParams[i].def);
                editParameter(i, false);
                fLastClickParam = -1;
                return true;
            }
            fLastClickParam = i;
            fLastClickTime = ev.time;

            fDragParam = i;
            fDragStartY = my;
            fDragStartNorm = toNorm(i, fValues[i]);
            fDragFine = (ev.mod & kModifierShift) != 0;
            editParameter(i, true);
            repaint();
            return true;
        }
        else if (fDragParam >= 0)
        {
            editParameter(fDragParam, false);
            fDragParam = -1;
            repaint();
            return true;
        }
        return false;
    }

    bool onMotion(const MotionEvent& ev) override
    {
        const float s = getWidth() / kW;
        const float mx = ev.pos.getX() / s, my = ev.pos.getY() / s;

        if (fDragParam >= 0)
        {
            const bool fine = (ev.mod & kModifierShift) != 0;
            if (fine != fDragFine) { // re-anchor when shift changes
                fDragFine = fine;
                fDragStartY = my;
                fDragStartNorm = toNorm(fDragParam, fValues[fDragParam]);
            }
            const float range = fine ? 1200.f : 220.f;
            float n = fDragStartNorm + (fDragStartY - my) / range;
            if (kParams[fDragParam].isInt) {
                // integer params: step with less travel
                const ParamInfo& p = kParams[fDragParam];
                const float steps = p.max - p.min;
                n = fDragStartNorm + (fDragStartY - my) / (steps * 30.f);
            }
            setParam(fDragParam, fromNorm(fDragParam, n));
            return true;
        }

        const int h = hitKnob(mx, my);
        if (h != fHover) { fHover = h; repaint(); }
        return false;
    }

    bool onScroll(const ScrollEvent& ev) override
    {
        const float s = getWidth() / kW;
        const int i = hitKnob(ev.pos.getX() / s, ev.pos.getY() / s);
        if (i < 0) return false;
        const ParamInfo& p = kParams[i];
        float v;
        if (p.isInt)
            v = fValues[i] + (ev.delta.getY() > 0 ? 1.f : -1.f);
        else
            v = fromNorm(i, toNorm(i, fValues[i]) + ev.delta.getY() * ((ev.mod & kModifierShift) ? 0.005f : 0.03f));
        if (v < p.min) v = p.min;
        if (v > p.max) v = p.max;
        editParameter(i, true);
        setParam(i, v);
        editParameter(i, false);
        return true;
    }

private:
    float fValues[kParamCount];
    int fDragParam = -1, fHover = -1, fLastClickParam = -1;
    uint fLastClickTime = 0;
    float fDragStartY = 0, fDragStartNorm = 0;
    bool fDragFine = false;
    int fPresetIdx = 0;

    float fInMin[KickScope::kBins] = {}, fInMax[KickScope::kBins] = {}, fOutMin[KickScope::kBins] = {}, fOutMax[KickScope::kBins] = {};
    int fScopeUsed = 0;
    uint32_t fScopeVersion = 0;
    bool fScopeChecked = false;
    const ScopeProvider* fScopeProv = nullptr;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HardKickForgeUI)
};

UI* createUI() { return new HardKickForgeUI(); }

END_NAMESPACE_DISTRHO
