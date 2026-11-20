#pragma once

#include "Pattern.h"

namespace gf
{

//==============================================================================
/**
    Metrical analysis of a time signature.

    Everything is measured in 16th-note "steps". A bar is divided into
    *tactus* beats (the felt pulse) and tactus beats are collected into
    *super-groups* (the accent cycle). The generator writes every genre in terms
    of these structures, so 7/8 is played as 2+2+3, 6/8 as two dotted quarters,
    5/4 as 3+2 and so on - not as a truncated 4/4 bar.

    Grouping spec ("2+2+3"):
      - denominator 2 or 4 (simple meters): counts are the beats and the spec says
        how the beats are grouped into accent groups (4/4 = "2+2", 5/4 = "3+2").
      - denominator 8 or 16 (compound / additive meters): the spec gives the
        length of each tactus beat in 8ths/16ths (6/8 = "3+3", 7/8 = "2+2+3").
*/
struct Meter
{
    struct Step
    {
        int index = 0;          // step within bar
        int tactus = 0;         // tactus beat index
        int posInTactus = 0;    // in steps
        int tactusLen = 4;      // in steps
        bool barStart = false;
        bool superStart = false;
        bool tactusStart = false;
        bool backbeat = false;  // start of a backbeat tactus
        int weight = 0;         // 5 bar, 4 super-group, 3 tactus, 2 eighth, 1 sixteenth
    };

    int numerator = 4, denominator = 4;
    int stepsPerCount = 4;
    int stepsPerBar = 16;
    bool compound = false;
    juce::String grouping;               // normalised spec actually used

    std::vector<int> tactusStart;        // in steps
    std::vector<int> tactusLen;          // in steps
    std::vector<int> superGroupSizes;    // in tactus beats
    std::vector<int> superGroupFirstTactus;
    std::vector<int> backbeatSteps;
    int halfTimeStep = 8;                // the single "big" backbeat for half-time feels
    std::vector<Step> steps;

    static Meter create (int numerator, int denominator, const juce::String& groupingSpec = {});
    static juce::String defaultGrouping (int numerator, int denominator);
    static std::vector<int> parseGrouping (const juce::String&);

    int numTactus() const { return (int) tactusStart.size(); }
    int superGroupOfTactus (int t) const;
    bool isLastTactusOfSuperGroup (int t) const;
    bool isFirstTactusOfSuperGroup (int t) const;

    /** Splits n beats into accent groups of 2s and 3s. */
    static std::vector<int> splitIntoGroups (int n);
};

} // namespace gf
