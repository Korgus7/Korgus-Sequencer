#pragma once
#include "Engine.h"
#include <functional>

// Prevedie piesen z rozhrania (JSON) na udaje pre zvukovy motor
std::unique_ptr<SongData> parseSong (const juce::var& songJson, const std::function<int (const juce::String&)>& slotFor);
