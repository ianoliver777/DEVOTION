#include "Engine.h"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <vector>

using devotion::Controls;
using devotion::Engine;

void require(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

// A synthetic vocal: voiced syllables (180 Hz harmonics with formants) and an "s" every second.
struct Vocal
{
    std::vector<float> left, right;
    std::vector<bool> sibilant;
};

static Vocal makeVocal(double rate, double seconds)
{
    const auto n = static_cast<size_t>(rate * seconds);
    Vocal v{std::vector<float>(n), std::vector<float>(n), std::vector<bool>(n)};
    unsigned noise = 7;
    float previous = 0.0f, previous2 = 0.0f;
    for (size_t i = 0; i < n; ++i)
    {
        const auto t = static_cast<float>(i / rate);
        const auto inSecond = std::fmod(t, 1.0f);
        const bool ess = inSecond > 0.80f && inSecond < 0.92f;
        // Syllable envelope: loud and quiet syllables so levelling has something to do.
        const auto syllable = std::fmod(t, 0.4f);
        const auto loudness = (static_cast<int>(t / 0.4f) % 2 == 0) ? 0.30f : 0.10f;
        const auto env = ess ? 0.0f : loudness * std::min(1.0f, syllable * 40.0f) * std::exp(-syllable * 2.0f);
        float voiced = 0.0f;
        for (int h = 1; h <= 30; ++h)
        {
            const auto f = 180.0f * static_cast<float>(h);
            if (f > 0.45f * static_cast<float>(rate))
                break;
            const auto formant = std::exp(-std::pow((f - 700.0f) / 400.0f, 2.0f)) + 0.6f * std::exp(-std::pow((f - 2600.0f) / 700.0f, 2.0f)) + 0.05f;
            voiced += formant / static_cast<float>(h) * std::sin(6.2831853f * f * t);
        }
        noise = noise * 1664525u + 1013904223u;
        const auto white = static_cast<float>(noise >> 8) / 8388608.0f - 1.0f;
        const auto bright = white - 2.0f * previous + previous2; // second difference: strongly tilted to the top
        previous2 = previous;
        previous = white;
        const auto s = ess ? 0.08f * bright : 0.0f;
        v.left[i] = env * voiced + s;
        v.right[i] = 0.95f * env * voiced + s;
        v.sibilant[i] = ess;
    }
    return v;
}

static double rms(const std::vector<float>& a, const std::vector<bool>& mask, bool want)
{
    double sum = 0.0;
    size_t count = 0;
    for (size_t i = a.size() / 4; i < a.size(); ++i)
        if (mask[i] == want)
        {
            sum += static_cast<double>(a[i]) * a[i];
            ++count;
        }
    return std::sqrt(sum / static_cast<double>(std::max<size_t>(count, 1)));
}

// Energy of a one-frequency probe through the engine, in dB relative to the input.
static double probe(double rate, Controls c, float hz)
{
    const auto n = static_cast<size_t>(rate);
    std::vector<float> l(n), r(n);
    for (size_t i = 0; i < n; ++i)
        l[i] = r[i] = 0.01f * std::sin(6.2831853f * hz * static_cast<float>(i / rate));
    const auto dry = l;
    Engine e;
    e.prepare(rate, c);
    e.process(l.data(), r.data(), static_cast<int>(n), c);
    double a = 0, b = 0;
    for (size_t i = n / 2; i < n; ++i)
    {
        a += static_cast<double>(l[i]) * l[i];
        b += static_cast<double>(dry[i]) * dry[i];
    }
    return 10.0 * std::log10(a / b);
}

int main()
{
    for (const auto rate : {44100.0, 48000.0, 96000.0, 192000.0})
    {
        const auto vocal = makeVocal(rate, 4.0);
        const auto n = vocal.left.size();
        auto l = vocal.left, r = vocal.right;
        Engine engine;
        engine.prepare(rate, {});
        engine.process(l.data(), r.data(), static_cast<int>(n), {});
        require(l == vocal.left && r == vocal.right, "Init must pass audio through bit for bit");

        for (auto setting : {Controls{1, 1, 1, 1}, Controls{1, 1, 1, 0, 0.0f}})
        {
            l = vocal.left;
            r = vocal.right;
            engine.reset(setting);
            engine.process(l.data(), r.data(), static_cast<int>(n), setting);
            require(l == vocal.left && r == vocal.right, "Bypass and MIX 0% must pass audio through bit for bit");
        }

        for (auto setting : {Controls{1, 0, 0, 0}, Controls{0, 1, 0, 0}, Controls{0, 0, 1, 0}, Controls{1, 1, 1, 0}})
        {
            l = vocal.left;
            r = vocal.right;
            engine.reset(setting);
            engine.process(l.data(), r.data(), static_cast<int>(n), setting);
            require(l != vocal.left && r != vocal.right, "Every knob must change the audio");
            for (size_t i = 0; i < n; ++i)
                require(std::isfinite(l[i]) && std::isfinite(r[i]), "Output must stay finite");
        }

        // Tone: AIR lifts the very top, PRESENCE the 3 kHz area, neither touches the lows much.
        const auto airTop = probe(rate, {1, 0, 0, 0}, 14000.0f), airLow = probe(rate, {1, 0, 0, 0}, 300.0f);
        require(airTop > 3.0 && std::abs(airLow) < 0.3, "AIR should lift 14 kHz by 3 dB or more and leave 300 Hz alone");
        const auto presence = probe(rate, {0, 1, 0, 0}, 3200.0f) - probe(rate, {0, 1, 0, 0}, 300.0f);
        require(presence > 3.0, "PRESENCE should lift 3.2 kHz by 3 dB or more relative to 300 Hz");

        // PRESENCE levels loud and quiet syllables closer together.
        l = vocal.left;
        r = vocal.right;
        engine.reset({0, 1, 0, 0});
        engine.process(l.data(), r.data(), static_cast<int>(n), {0, 1, 0, 0});
        const auto syllableRange = [&](const std::vector<float>& a) {
            const auto window = static_cast<size_t>(rate * 0.4);
            double loud = 0, quiet = 0;
            for (size_t w = 2; w < 8; ++w)
            {
                double sum = 0;
                for (size_t i = w * window; i < (w + 1) * window && i < a.size(); ++i)
                    sum += static_cast<double>(a[i]) * a[i];
                (w % 2 == 0 ? loud : quiet) += sum;
            }
            return 10.0 * std::log10(loud / quiet);
        };
        require(syllableRange(l) < syllableRange(vocal.left) - 2.0, "PRESENCE compression should even out syllables by 2 dB or more");
        require(engine.compressionDb() >= 0.0f, "Compression amount must be reported");

        // SILK cuts the "s" bursts hard while leaving the voiced parts close to unchanged.
        l = vocal.left;
        r = vocal.right;
        engine.reset({0, 0, 1, 0});
        engine.process(l.data(), r.data(), static_cast<int>(n), {0, 0, 1, 0});
        const auto essCut = 20.0 * std::log10(rms(l, vocal.sibilant, true) / rms(vocal.left, vocal.sibilant, true));
        const auto voiceChange = 20.0 * std::log10(rms(l, vocal.sibilant, false) / rms(vocal.left, vocal.sibilant, false));
        require(essCut < -4.0, "SILK should cut sibilance by 4 dB or more");
        require(std::abs(voiceChange) < 1.0, "SILK should change the voiced parts by less than 1 dB");
        require(engine.deEssDb() >= 0.0f, "De-ess amount must be reported");

        // MATCH lands within about 1 dB of the input loudness.
        l = vocal.left;
        r = vocal.right;
        engine.reset({1, 1, 0.5f, 0, 1, 1});
        engine.process(l.data(), r.data(), static_cast<int>(n), {1, 1, 0.5f, 0, 1, 1});
        std::vector<bool> all(n, false);
        const auto ratio = rms(l, all, false) / rms(vocal.left, all, false);
        require(ratio > 0.85 && ratio < 1.12, "MATCH should land near the input loudness");

        auto mono = vocal.left;
        engine.reset({0.8f, 0.8f, 0.8f, 0});
        engine.process(mono.data(), nullptr, static_cast<int>(n), {0.8f, 0.8f, 0.8f, 0});
        require(mono != vocal.left, "Mono processing must work");
    }
    std::cout << "DEVOTION DSP checks passed at 44.1, 48, 96 and 192 kHz\n";
}
