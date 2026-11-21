#include "guitar/RenderList.h"

namespace guitar::renders
{
    const Spec& spec (Articulation a)
    {
        static const Spec sustain { 3, { 45, 85, 118 }, 2, 4.0, 5.5, keys::sustain };
        // MUTE: Jupiter's Mute Layer table (the owner's screenshot, factory map, November 2026, measured from its bars):
        // five degrees of muting in zones 1-25, 26-51, 52-76, 77-102, 103-127; one velocity mid-zone each, at least 12 from
        // a zone's edge.
        static const Spec mute { 5, { 13, 38, 64, 89, 115 }, 8, 1.0, 1.6, keys::mute };
        return a == Articulation::mute ? mute : sustain;
    }

    int layerFor (Articulation a, int velocity) noexcept
    {
        const auto& s = spec (a);
        auto layer = 0;
        while (layer + 1 < s.layers && velocity >= (s.velocities[(size_t) layer] + s.velocities[(size_t) layer + 1] + 1) / 2)
            ++layer;
        return layer;
    }

    std::vector<Take> takes (Articulation a, int string)
    {
        const auto& s = spec (a);
        std::vector<Take> result;
        auto t = leadInSeconds + noteDelaySeconds;
        for (int fret = 0; fret <= numFrets; ++fret)
            for (int layer = 0; layer < s.layers; ++layer)
            {
                // Each note's downs in a row, then its ups (the "v2" lists): 0.6.2 for palm mute, 0.7.3 for sustain. The
                // owner's palm mute and order checks: Jupiter has 8 down / 6 up palm mutes a note but plays only ~4 of
                // each when the strokes alternate, all of them when a stroke repeats. The A/B suite (0.7.3) found the same
                // for sustain: the first sustain lists (down, up, down, up) left PR7-I one sustain take a stroke (its low A
                // repeated 8ths alternated two recordings) where the round-robin check measured Jupiter's two a stroke.
                for (const auto stroke : { Stroke::down, Stroke::up })
                    for (int rr = 0; rr < s.roundRobins; ++rr)
                    {
                        result.push_back ({ string, fret, layer, stroke, rr, t, a });
                        t += s.slotSeconds;
                    }
            }
        return result;
    }

    double lengthSeconds (Articulation a, int string)
    {
        return leadInSeconds + spec (a).slotSeconds * (double) takes (a, string).size();
    }

    juce::MidiFile toMidi (Articulation a, int string)
    {
        constexpr int ppq = 960;
        const auto ticks = [] (double seconds) { return seconds * bpm / 60.0 * ppq; };
        juce::MidiMessageSequence track;
        track.addEvent (juce::MidiMessage::textMetaEvent (3, fileName (a, string)));
        track.addEvent (juce::MidiMessage::tempoMetaEvent (juce::roundToInt (60.0e6 / bpm)));
        track.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4));
        const auto key = [&] (int note, double at)
        {
            track.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 100).withTimeStamp (ticks (at)));
            track.addEvent (juce::MidiMessage::noteOff (1, note).withTimeStamp (ticks (at + 0.05)));
        };
        key (keys::forceStringLowA + string, 0.5);
        key (keys::legatoOff, 0.75);
        for (const auto& take : takes (a, string))
        {
            const auto slot = take.noteSeconds - noteDelaySeconds;
            key (spec (a).key, slot);
            key (take.stroke == Stroke::down ? keys::pickDown : keys::pickUp, slot + 0.05);
            track.addEvent (juce::MidiMessage::textMetaEvent (6, "F" + juce::String (take.fret) + " L" + juce::String (take.layer + 1)
                                                                    + (take.stroke == Stroke::down ? " DOWN " : " UP ") + juce::String (take.roundRobin + 1)).withTimeStamp (ticks (take.noteSeconds)));
            track.addEvent (juce::MidiMessage::noteOn (1, take.note(), (juce::uint8) take.velocity()).withTimeStamp (ticks (take.noteSeconds)));
            track.addEvent (juce::MidiMessage::noteOff (1, take.note()).withTimeStamp (ticks (take.noteSeconds + spec (a).holdSeconds)));
        }
        track.addEvent (juce::MidiMessage::endOfTrack().withTimeStamp (ticks (lengthSeconds (a, string))));
        track.sort();
        track.updateMatchedPairs();
        juce::MidiFile file;
        file.setTicksPerQuarterNote (ppq);
        file.addTrack (track);
        return file;
    }

    juce::String fileName (Articulation a, int string)
    {
        const auto which = string == 0 ? juce::String ("low A") : string == numStrings - 1 ? juce::String ("high E") : stringName (string);
        // (0.6.2 palm mute, 0.7.3 sustain: the lists in their new order are "v2", so a render of the first ones, whose
        // strokes alternate, isn't taken for one of these.)
        return "PR7-I " + articulationName (a) + " v2 - String " + juce::String (playerNumber (string))
             + " (" + which + ")";
    }

    juce::String takeFileName (Articulation a, const Take& t)
    {
        return articulationName (a) + "/s" + juce::String (t.string) + "_f" + juce::String (t.fret).paddedLeft ('0', 2) + "_l"
             + juce::String (t.layer) + (t.stroke == Stroke::down ? "_d" : "_u") + "_r" + juce::String (t.roundRobin) + ".wav";
    }

    std::optional<TakeId> parseTakeFile (const juce::File& file)
    {
        TakeId id;
        const auto folder = file.getParentDirectory().getFileName();
        if (folder.equalsIgnoreCase ("Sustain"))
            id.articulation = Articulation::sustain;
        else if (folder.equalsIgnoreCase ("Mute"))
            id.articulation = Articulation::mute;
        else
            return std::nullopt;
        const auto parts = juce::StringArray::fromTokens (file.getFileNameWithoutExtension(), "_", {});
        if (parts.size() != 5 || ! parts[0].startsWith ("s") || ! parts[1].startsWith ("f") || ! parts[2].startsWith ("l")
            || (parts[3] != "d" && parts[3] != "u") || ! parts[4].startsWith ("r"))
            return std::nullopt;
        id.roundRobin = parts[4].substring (1).getIntValue();
        id.string = parts[0].substring (1).getIntValue();
        id.fret = parts[1].substring (1).getIntValue();
        id.layer = parts[2].substring (1).getIntValue();
        id.stroke = parts[3] == "d" ? Stroke::down : Stroke::up;
        if (id.string < 0 || id.string >= numStrings || id.fret < 0 || id.fret > numFrets || id.layer < 0
            || id.layer >= spec (id.articulation).layers || id.roundRobin < 0 || id.roundRobin >= spec (id.articulation).roundRobins)
            return std::nullopt;
        return id;
    }
}
