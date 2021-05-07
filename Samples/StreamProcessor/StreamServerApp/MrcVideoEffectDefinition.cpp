#include "MrcVideoEffectDefinition.h"

MrcVideoEffectDefinition::MrcVideoEffectDefinition()
{
    StreamType(DefaultStreamType);
    HologramCompositionEnabled(DefaultHologramCompositionEnabled);
    RecordingIndicatorEnabled(DefaultRecordingIndicatorEnabled);
    VideoStabilizationEnabled(DefaultVideoStabilizationEnabled);
    VideoStabilizationBufferLength(DefaultVideoStabilizationBufferLength);
    GlobalOpacityCoefficient(DefaultGlobalOpacityCoefficient);
    BlankOnProtectedContent(DefaultBlankOnProtectedContent);
    ShowHiddenMesh(DefaultShowHiddenMesh);
    OutputSize(DefaultOutputSize);
    PreferredHologramPerspective(DefaultPreferredHologramPerspective);
}
