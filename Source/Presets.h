#pragma once

#include "Params.h"
#include <map>

namespace pd
{
// Plain (real-unit) values keyed by parameter ID.
using Snapshot = std::map<juce::String, float>;

// A KIT: all 16 slots (sound + mix) and the master section, with a name for every slot's sound.
struct Kit
{
    juce::String name, category, group;
    Snapshot values;
    std::array<juce::String, numSlots> slotNames;
    juce::File file;          // empty for factory kits
    bool favourite = false;
    bool isFactory() const { return file == juce::File(); }
};

// A SOUND: one slot's sound (TYPE, MODEL, the six knobs, drive mode and gain), keyed by the parameter name without the
// slot prefix ("type", "tune", ...). The factory sounds are the slots of the factory kits; a slot can load any of them.
struct Sound
{
    juce::String name, kit;   // kit: where it comes from (shown in the list)
    DrumType type = DrumType::kick;
    Snapshot values;
    juce::File file;          // empty for factory sounds
    bool isFactory() const { return file == juce::File(); }
};

inline const juce::String favouritesCategory { "Favourites" };

const std::vector<Kit>& factoryKits();
const std::vector<Sound>& factorySounds();

Snapshot captureSnapshot (const juce::AudioProcessorValueTreeState& state);
Snapshot captureSound (const juce::AudioProcessorValueTreeState& state, int slot);
// Missing IDs fall back to parameter defaults, so a kit always defines the full sound. Instance settings are kept:
// the OUT of every slot (the routing belongs to the DAW project) and DIGITAL | ANALOGUE.
void applyKit (juce::AudioProcessorValueTreeState& state, const Snapshot& values);
// Only the sound of one slot; its mix (level, pan, sends, output...) stays.
void applySound (juce::AudioProcessorValueTreeState& state, int slot, const Snapshot& soundValues);
DrumParams paramsFromSnapshot (const Snapshot& values);
// MULTI: slot k -> OUT k for every slot (true), or everything back to MAIN (false).
void setMultiOut (juce::AudioProcessorValueTreeState& state, bool multi);
bool isMultiOut (const juce::AudioProcessorValueTreeState& state);

// Kit browser filter (as in the other plug-ins): category "" = all, favouritesCategory = starred, "User" = own kits;
// every word of text must appear in the name, category or group. Genres in library order, names natural within.
std::vector<int> filterKits (const std::vector<Kit>& kits, const juce::String& category, const juce::String& text);

juce::String kitToXml (const juce::String& name, const juce::String& category, const Snapshot& values,
                       const std::array<juce::String, numSlots>& slotNames);
bool kitFromXml (const juce::String& xml, Kit& out, juce::String& error);
juce::String soundToXml (const juce::String& name, const Snapshot& values);
bool soundFromXml (const juce::String& xml, Sound& out, juce::String& error);

class KitLibrary
{
public:
    static constexpr const char* extension = ".pdkit";

    explicit KitLibrary (juce::File userDirectory = defaultUserDirectory());
    static juce::File defaultUserDirectory();   // Documents/PowerDrum/Kits

    void rescan();
    const std::vector<Kit>& all() const { return kits; }  // factory first, then user
    int indexOf (const juce::String& name) const;

    bool saveUser (const juce::String& name, const Snapshot& values, const std::array<juce::String, numSlots>& slotNames, juce::String& error);
    bool rename (int index, const juce::String& newName, juce::String& error);
    bool remove (int index, juce::String& error);
    bool importFile (const juce::File& source, juce::String& importedName, juce::String& error);
    bool exportTo (const juce::File& target, const juce::String& name, const Snapshot& values,
                   const std::array<juce::String, numSlots>& slotNames, juce::String& error) const;

    bool toggleFavourite (int index);
    juce::File favouritesFile() const { return userDir.getChildFile ("Favourites.json"); }
    std::function<bool (const juce::File&)> moveToTrash = [] (const juce::File& f) { return f.moveToTrash(); };

private:
    juce::File userDir;
    std::vector<Kit> kits;
    juce::File fileFor (const juce::String& name) const;
};

class SoundLibrary
{
public:
    static constexpr const char* extension = ".pdsound";

    explicit SoundLibrary (juce::File userDirectory = defaultUserDirectory());
    static juce::File defaultUserDirectory();   // Documents/PowerDrum/Sounds

    void rescan();
    const std::vector<Sound>& all() const { return sounds; }
    // The sounds of one type, in library order (factory by kit, then the user's own).
    std::vector<int> ofType (DrumType) const;
    int indexOf (const juce::String& name, DrumType) const;
    bool saveUser (const juce::String& name, const Snapshot& values, juce::String& error);

private:
    juce::File userDir;
    std::vector<Sound> sounds;
};
}
