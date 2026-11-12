#pragma once

namespace pd
{
// The PLAY groove: two bars of 16ths, a velocity per slot and step (0 = rest). Slots in the default layout:
// 0 kick, 1 rim, 2 snare, 3 clap, 4 rumble, 5 tom, 6 closed hat, 7 perc, 8 hat 2, 9 perc, 10 open hat, 11 perc,
// 12 fx, 13 crash, 14 fx 2, 15 ride. Whatever a kit puts in a slot is played there.
inline constexpr int grooveSteps = 32;
inline float grooveVelocity (int slot, int step)
{
    const int s = step % 16, bar = step / 16;
    switch (slot)
    {
        case 0:  return s % 4 == 0 ? 1.0f : 0.0f;                                     // four on the floor
        case 4:  return (s % 4 == 2) ? 0.85f : ((s % 4 == 3) ? 0.6f : 0.0f);           // rumble after each kick
        case 3:  return (s == 4 || s == 12) ? 0.95f : 0.0f;                            // clap on 2 and 4
        case 2:  return (bar == 1 && (s == 14 || s == 15)) ? 0.7f : 0.0f;              // a snare fill at the end
        case 6:  return (s % 4 == 2) ? 0.0f : (s % 2 == 0 ? 0.85f : 0.55f);            // closed hats
        case 10: return s % 4 == 2 ? 0.9f : 0.0f;                                      // open hat off-beats
        case 1:  return (s == 3 || s == 11) ? 0.7f : 0.0f;
        case 7:  return (s == 7 || (bar == 1 && s == 13)) ? 0.75f : 0.0f;
        case 9:  return (s == 10 && bar == 0) || s == 15 ? 0.7f : 0.0f;
        case 11: return s == 9 ? 0.7f : 0.0f;
        case 15: return (bar == 1 && s % 4 == 2) ? 0.6f : 0.0f;                        // ride in the second bar
        case 13: return step == 0 ? 0.9f : 0.0f;                                      // crash at the top
        case 12: return (bar == 1 && s == 12) ? 0.8f : 0.0f;
        default: return 0.0f;
    }
}
}
