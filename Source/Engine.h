#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace devotion
{
// All controls are 0..1. mix defaults to fully wet.
struct Controls { float air{}, presence{}, silk{}, bypass{}, mix{1.0f}, match{}; };

class Engine
{
public:
    void prepare(double rate, Controls initial)
    {
        sampleRate = static_cast<float>(rate);
        smoothing = pole(14.0f);
        const auto safe = [this](float hz) { return std::min(hz, 0.42f * sampleRate); };
        for (auto& c : channels)
        {
            c.airShelf.shelf(safe(12000.0f), sampleRate, 5.0f, true);
            c.airBand.highpass(safe(6000.0f), sampleRate);
            c.airHarm.highpass(safe(8000.0f), sampleRate);
            c.presenceBell.peak(3200.0f, sampleRate, 4.0f, 0.8f);
            c.harshDip.peak(safe(4600.0f), sampleRate, -2.5f, 1.3f);
            c.splitCoefficient = 1.0f - std::exp(-6.283185307f * safe(5500.0f) / sampleRate);
        }
        for (auto& f : sibilanceDetect)
            f.highpass(safe(5000.0f), sampleRate);
        sparkleFollow = pole(50.0f);
        compAttack = pole(1.0f / 0.005f);
        compRelease = pole(1.0f / 0.080f);
        detectorPole = pole(1.0f / 0.010f);
        essAttack = pole(1.0f / 0.0015f);
        essRelease = pole(1.0f / 0.060f);
        loudnessPole = pole(1.0f / 0.4f);
        matchPole = pole(1.0f / 0.08f);
        reset(initial);
    }

    float compressionDb() const { return lastCompressionDb; }
    float deEssDb() const { return lastDeEssDb; }

    void reset(Controls initial)
    {
        current = initial;
        for (auto& c : channels)
            c.clear();
        for (auto& f : sibilanceDetect)
            f.clear();
        compPower = compGainDb = 0.0f;
        essLevel = fullLevel = essGainDb = 0.0f;
        dryPower = wetPower = 0.0f;
        matchGain = 1.0f;
        lastCompressionDb = lastDeEssDb = 0.0f;
    }

    void process(float* left, float* right, int count, Controls target)
    {
        const bool stereo = right != nullptr;
        float maxComp = 0.0f, maxEss = 0.0f;
        for (int i = 0; i < count; ++i)
        {
            smooth(current.air, target.air);
            smooth(current.presence, target.presence);
            smooth(current.silk, target.silk);
            smooth(current.bypass, target.bypass);
            smooth(current.mix, target.mix);
            smooth(current.match, target.match);

            const float inL = left[i];
            const float inR = stereo ? right[i] : inL;
            std::array<float, 2> x{inL, inR};
            const int used = stereo ? 2 : 1;

            // PRESENCE: 3.2 kHz lift, then gentle RMS levelling (2:1 to 3:1).
            for (int c = 0; c < used; ++c)
            {
                const auto bell = channels[c].presenceBell.run(x[c]);
                x[c] += current.presence * (bell - x[c]);
            }
            if (!stereo)
                x[1] = x[0];
            {
                const auto power = 0.5f * (x[0] * x[0] + x[1] * x[1]);
                follow(compPower, power, detectorPole);
                float wanted = 0.0f;
                if (current.presence > 0.0f)
                {
                    const auto p = current.presence;
                    const auto levelDb = 10.0f * std::log10(compPower + 1.0e-12f);
                    const auto threshold = -18.0f - 14.0f * p;
                    const auto ratio = 2.0f + p;
                    constexpr float knee = 10.0f;
                    const auto over = levelDb - threshold;
                    if (over > knee * 0.5f)
                        wanted = over * (1.0f - 1.0f / ratio);
                    else if (over > -knee * 0.5f)
                    {
                        const auto k = over + knee * 0.5f;
                        wanted = (1.0f - 1.0f / ratio) * k * k / (2.0f * knee);
                    }
                    wanted *= std::min(1.0f, 10.0f * p);
                }
                follow(compGainDb, wanted, wanted > compGainDb ? compAttack : compRelease);
                if (current.presence > 0.0f)
                {
                    const auto gain = dbToGain(2.0f * current.presence - compGainDb);
                    x[0] *= gain;
                    x[1] *= gain;
                }
                maxComp = std::max(maxComp, compGainDb);
            }

            // AIR: 12 kHz shelf plus level-normalised second-harmonic sparkle above 6 kHz.
            for (int c = 0; c < used; ++c)
            {
                auto& ch = channels[c];
                const auto shelf = ch.airShelf.run(x[c]);
                const auto band = ch.airBand.run(x[c]);
                follow(ch.sparkleEnvelope, std::abs(band), sparkleFollow);
                const auto sparkle = 0.45f * ch.airHarm.run(band * band / (ch.sparkleEnvelope + 0.02f));
                x[c] += current.air * (shelf - x[c] + sparkle);
            }
            if (!stereo)
                x[1] = x[0];

            // SILK: split-band de-esser above 5.5 kHz plus a gentle dip in the 4-5 kHz harshness zone.
            {
                const auto hiL = sibilanceDetect[0].run(x[0]);
                const auto hiR = sibilanceDetect[1].run(x[1]);
                const auto ess = std::max(std::abs(hiL), std::abs(hiR));
                const auto full = std::max(std::abs(x[0]), std::abs(x[1]));
                follow(essLevel, ess, ess > essLevel ? essAttack : essRelease);
                follow(fullLevel, full, full > fullLevel ? essAttack : essRelease);
                float wanted = 0.0f;
                if (current.silk > 0.0f && essLevel > 1.0e-4f)
                {
                    // Sibilance = high band carrying more than ~35% of the level; cut up to 12 dB.
                    const auto ratioDb = 20.0f * std::log10((essLevel + 1.0e-6f) / (fullLevel + 1.0e-6f) / 0.35f);
                    wanted = std::clamp(ratioDb * 2.0f, 0.0f, 12.0f) * current.silk;
                }
                follow(essGainDb, wanted, wanted > essGainDb ? essAttack : essRelease);
                maxEss = std::max(maxEss, essGainDb);
                if (current.silk > 0.0f)
                {
                    const auto highGain = dbToGain(-essGainDb);
                    for (int c = 0; c < used; ++c)
                    {
                        auto& ch = channels[c];
                        const auto smoothed = ch.harshDip.run(x[c]);
                        const auto tamed = x[c] + current.silk * (smoothed - x[c]);
                        // First-order complementary split (low + high == input), so the cut is a clean,
                        // bump-free dynamic shelf above 5.5 kHz.
                        follow(ch.splitLow, tamed, ch.splitCoefficient);
                        const auto high = tamed - ch.splitLow;
                        x[c] = ch.splitLow + highGain * high;
                    }
                    if (!stereo)
                        x[1] = x[0];
                }
                else
                    for (int c = 0; c < used; ++c)
                    {
                        channels[c].harshDip.run(x[c]);
                        follow(channels[c].splitLow, x[c], channels[c].splitCoefficient);
                    }
            }

            const bool neutral = current.air == 0.0f && current.presence == 0.0f && current.silk == 0.0f;
            float outL = neutral ? inL : inL + current.mix * (x[0] - inL);
            float outR = neutral ? inR : inR + current.mix * (x[1] - inR);

            follow(dryPower, 0.5f * (inL * inL + inR * inR), loudnessPole);
            follow(wetPower, 0.5f * (outL * outL + outR * outR), loudnessPole);
            const auto wantedMatch = wetPower > 1.0e-9f ? std::clamp(std::sqrt(dryPower / wetPower), 0.0625f, 2.0f) : 1.0f;
            follow(matchGain, wantedMatch, matchPole);
            if (current.match > 0.0f && !neutral)
            {
                const auto m = 1.0f + current.match * (matchGain - 1.0f);
                outL *= m;
                outR *= m;
            }

            if (current.bypass == 1.0f)
            {
                outL = inL;
                outR = inR;
            }
            else if (current.bypass > 0.0f)
            {
                outL += current.bypass * (inL - outL);
                outR += current.bypass * (inR - outR);
            }
            left[i] = outL;
            if (stereo)
                right[i] = outR;
        }
        lastCompressionDb = maxComp;
        lastDeEssDb = maxEss;
    }

private:
    struct Biquad
    {
        float b0{}, b1{}, b2{}, a1{}, a2{}, z1{}, z2{};
        void set(float c0, float c1, float c2, float d0, float d1, float d2)
        {
            b0 = c0 / d0; b1 = c1 / d0; b2 = c2 / d0; a1 = d1 / d0; a2 = d2 / d0;
        }
        void lowpass(float hz, float rate)
        {
            const auto w = 6.283185307f * hz / rate, cosw = std::cos(w), alpha = std::sin(w) / (2.0f * 0.70710678f);
            set((1 - cosw) * 0.5f, 1 - cosw, (1 - cosw) * 0.5f, 1 + alpha, -2 * cosw, 1 - alpha);
        }
        void highpass(float hz, float rate)
        {
            const auto w = 6.283185307f * hz / rate, cosw = std::cos(w), alpha = std::sin(w) / (2.0f * 0.70710678f);
            set((1 + cosw) * 0.5f, -(1 + cosw), (1 + cosw) * 0.5f, 1 + alpha, -2 * cosw, 1 - alpha);
        }
        void peak(float hz, float rate, float gainDb, float q)
        {
            const auto a = std::pow(10.0f, gainDb / 40.0f);
            const auto w = 6.283185307f * hz / rate, cosw = std::cos(w), alpha = std::sin(w) / (2.0f * q);
            set(1 + alpha * a, -2 * cosw, 1 - alpha * a, 1 + alpha / a, -2 * cosw, 1 - alpha / a);
        }
        void shelf(float hz, float rate, float gainDb, bool high)
        {
            const auto a = std::pow(10.0f, gainDb / 40.0f);
            const auto w = 6.283185307f * hz / rate, cosw = std::cos(w);
            const auto root = 2.0f * std::sqrt(a) * std::sin(w) / 2.0f * std::sqrt(2.0f);
            const auto s = high ? -1.0f : 1.0f;
            set(a * ((a + 1) - s * (a - 1) * cosw + root), s * 2 * a * ((a - 1) - s * (a + 1) * cosw),
                a * ((a + 1) - s * (a - 1) * cosw - root), (a + 1) + s * (a - 1) * cosw + root,
                -s * 2 * ((a - 1) + s * (a + 1) * cosw), (a + 1) + s * (a - 1) * cosw - root);
        }
        void clear() { z1 = z2 = 0.0f; }
        float run(float x)
        {
            const auto y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    struct Channel
    {
        Biquad airShelf, airBand, airHarm, presenceBell, harshDip;
        float sparkleEnvelope{}, splitLow{}, splitCoefficient{};
        void clear()
        {
            for (auto* f : {&airShelf, &airBand, &airHarm, &presenceBell, &harshDip})
                f->clear();
            sparkleEnvelope = splitLow = 0.0f;
        }
    };

    float pole(float hz) const { return 1.0f - std::exp(-6.283185307f * hz / sampleRate); }
    static float dbToGain(float db) { return std::pow(10.0f, db * 0.05f); }
    static void follow(float& state, float target, float coefficient) { state += coefficient * (target - state); }
    void smooth(float& state, float target) const
    {
        follow(state, target, smoothing);
        if (std::abs(state - target) < 0.00001f)
            state = target;
    }

    Controls current;
    std::array<Channel, 2> channels;
    std::array<Biquad, 2> sibilanceDetect;
    float sampleRate{48000.0f}, smoothing{}, sparkleFollow{}, compAttack{}, compRelease{}, detectorPole{};
    float essAttack{}, essRelease{}, loudnessPole{}, matchPole{};
    float compPower{}, compGainDb{}, essLevel{}, fullLevel{}, essGainDb{};
    float dryPower{}, wetPower{}, matchGain{1.0f}, lastCompressionDb{}, lastDeEssDb{};
};
}
