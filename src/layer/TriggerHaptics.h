#pragma once

#include <cstdint>
#include <string>

namespace tf2vr
{
// one adaptive trigger effect, parsed from "<mode> <parameters>":
//   off
//   feedback <position 0-9> <strength 0-8>                     resistance from a position on
//   weapon <start 2-7> <end 3-8> <strength 0-8>                  a gun trigger that breaks between start and end
//   vibration <position 0-9> <amplitude 0-8> <frequency Hz>      buzzes past a position
//   slope <start 0-8> <end 1-9> <start strength 1-8> <end strength 1-8>
//   multi <ten strengths 0-8>                                    resistance at each of the ten positions
//   multi_vibration <frequency Hz> <ten amplitudes 0-8>
struct TriggerEffect
{
    uint8_t mode = 0;
    uint8_t data[11] = {};

    bool operator==(const TriggerEffect& other) const;
    bool operator!=(const TriggerEffect& other) const { return !(*this == other); }
};

bool ParseTriggerEffect(const std::string& text, TriggerEffect& effect);
std::string DescribeTriggerEffect(const TriggerEffect& effect);

// PlayStation VR2 Sense adaptive triggers through PSVR2Toolkit (psvr2_toolkit_capi.dll); does nothing without it.
// Calls into the toolkit can block, so they run on a worker thread; SetTrigger only records what each hand should feel.
namespace triggers
{
void Start(const std::wstring& capiPathOverride);
// hand 0 = left, 1 = right
void SetTrigger(int hand, const TriggerEffect& effect);
}
}
