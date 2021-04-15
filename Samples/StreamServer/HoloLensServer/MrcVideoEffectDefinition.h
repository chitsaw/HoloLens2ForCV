#pragma once

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Effects.h>

// This class provides an IAudioEffectDefinition which can be used
// to configure and create a MixedRealityCaptureVideoEffect
// object. See https://developer.microsoft.com/en-us/windows/holographic/mixed_reality_capture_for_developers#creating_a_custom_mixed_reality_capture_.28mrc.29_recorder
// for more information about the effect definition properties.

#define RUNTIMECLASS_MIXEDREALITYCAPTURE_VIDEO_EFFECT L"Windows.Media.MixedRealityCapture.MixedRealityCaptureVideoEffect"

//
// StreamType: Describe which capture stream this effect is used for.
// Type: Windows::Media::Capture::MediaStreamType as UINT32
// Default: VideoRecord
//
#define PROPERTY_STREAMTYPE L"StreamType"

//
// HologramCompositionEnabled: Flag to enable or disable holograms in video capture.
// Type: bool
// Default: True
//
#define PROPERTY_HOLOGRAMCOMPOSITIONENABLED L"HologramCompositionEnabled"

//
// RecordingIndicatorEnabled: Flag to enable or disable recording indicator on screen during hologram capturing.
// Type: bool
// Default: False
//
#define PROPERTY_RECORDINGINDICATORENABLED L"RecordingIndicatorEnabled"


//
// VideoStabilizationEnabled: Flag to enable or disable video stabilization powered by the HoloLens tracker.
// Type : bool
// Default: False
//
#define PROPERTY_VIDEOSTABILIZATIONENABLED L"VideoStabilizationEnabled"

//
// VideoStabilizationBufferLength: Set how many historical frames are used for video stabilization.
// Type : UINT32 (Max num is 30)
// Default: 0
//
#define PROPERTY_VIDEOSTABILIZATIONBUFFERLENGTH L"VideoStabilizationBufferLength"

//
// GlobalOpacityCoefficient: Set global opacity coefficient of hologram.
// Type : float (0.0 to 1.0)
// Default: 0.9
//
#define PROPERTY_GLOBALOPACITYCOEFFICIENT L"GlobalOpacityCoefficient"

//
// BlankOnProtectedContent: Flag to enable or disable returning an empty frame if there is a 2d UWP app showing protected content.
//                          If this flag is false and a 2d UWP app is showing protected content, the 2d UWP app will be replaced by
//                          a protected content texture in the mixed reality capture.
// Type : bool
// Default: false
//
#define PROPERTY_BLANKONPROTECTEDCONTENT L"BlankOnProtectedContent"

//
// ShowHiddenMesh: Flag to enable or disable showing the holographic camera's hidden area mesh and neighboring content.
// Type : bool
// Default: false
//
#define PROPERTY_SHOWHIDDENMESH L"ShowHiddenMesh"

//
// OutputSize: Set the desired output size after cropping for video stabilization. A default crop size is chosen if 0 or an invalid output size is specified.
// Type : Windows.Foundation.Size
// Default: (0,0)
//
#define PROPERTY_OUTPUTSIZE L"OutputSize"

//
// PreferredHologramPerspective: Enum used to indicate which holographic camera view configuration should be captured
// Type: MixedRealityCapturePerspective as UINT32
// 0: App won't be asked to render from the photo/video camera
// 1: App is rendered from the photo/video camera
// Default: 1
//
#define PROPERTY_PREFERREDHOLOGRAMPERSPECTIVE L"PreferredHologramPerspective"

//
// Maximum value of VideoStabilizationBufferLength
// This number is defined and used in MixedRealityCaptureVideoEffect
//
#define PROPERTY_MAX_VSBUFFER 30U

template<typename T, typename U>
U GetValueFromPropertySet(winrt::Windows::Foundation::Collections::IPropertySet const& propertySet, winrt::hstring const& key, U defaultValue)
{
    try
    {
        return static_cast<U>(winrt::unbox_value<T>(propertySet.Lookup(key)));
    }
    catch (winrt::hresult_out_of_bounds const& /*e*/)
    {
        // The key is not present in the PropertySet. Return the default value.
        return defaultValue;
    }
}

enum class MixedRealityCapturePerspective
{
    Display = 0,
    PhotoVideoCamera = 1,
};

class MrcVideoEffectDefinition : public winrt::implements<MrcVideoEffectDefinition, winrt::Windows::Media::Effects::IVideoEffectDefinition>
{
public:
    MrcVideoEffectDefinition();

    //
    // IVideoEffectDefinition
    //
    winrt::hstring ActivatableClassId()
    {
        return m_activatableClassId;
    }

    winrt::Windows::Foundation::Collections::IPropertySet Properties()
    {
        return m_propertySet;
    }

    //
    // Mixed Reality Capture effect properties
    //
    winrt::Windows::Media::Capture::MediaStreamType StreamType()
    {
        return GetValueFromPropertySet<winrt::Windows::Media::Capture::MediaStreamType>(m_propertySet, PROPERTY_STREAMTYPE, DefaultStreamType);
    }

    void StreamType(winrt::Windows::Media::Capture::MediaStreamType newValue)
    {
        m_propertySet.Insert(PROPERTY_STREAMTYPE, winrt::box_value(newValue));
    }

    bool HologramCompositionEnabled()
    {
        return GetValueFromPropertySet<bool>(m_propertySet, PROPERTY_HOLOGRAMCOMPOSITIONENABLED, DefaultHologramCompositionEnabled);
    }

    void HologramCompositionEnabled(bool newValue)
    {
        m_propertySet.Insert(PROPERTY_HOLOGRAMCOMPOSITIONENABLED, winrt::box_value(newValue));
    }

    bool RecordingIndicatorEnabled()
    {
        return GetValueFromPropertySet<bool>(m_propertySet, PROPERTY_RECORDINGINDICATORENABLED, DefaultRecordingIndicatorEnabled);
    }

    void RecordingIndicatorEnabled(bool newValue)
    {
        m_propertySet.Insert(PROPERTY_RECORDINGINDICATORENABLED, winrt::box_value(newValue));
    }

    bool VideoStabilizationEnabled()
    {
        return GetValueFromPropertySet<bool>(m_propertySet, PROPERTY_VIDEOSTABILIZATIONENABLED, DefaultVideoStabilizationEnabled);
    }

    void VideoStabilizationEnabled(bool newValue)
    {
        m_propertySet.Insert(PROPERTY_VIDEOSTABILIZATIONENABLED, winrt::box_value(newValue));
    }

    uint32_t VideoStabilizationBufferLength()
    {
        return GetValueFromPropertySet<uint32_t>(m_propertySet, PROPERTY_VIDEOSTABILIZATIONBUFFERLENGTH, DefaultVideoStabilizationBufferLength);
    }

    void VideoStabilizationBufferLength(uint32_t newValue)
    {
        m_propertySet.Insert(PROPERTY_VIDEOSTABILIZATIONBUFFERLENGTH, winrt::box_value((std::min)(newValue, PROPERTY_MAX_VSBUFFER)));
    }

    float GlobalOpacityCoefficient()
    {
        return GetValueFromPropertySet<float>(m_propertySet, PROPERTY_GLOBALOPACITYCOEFFICIENT, DefaultGlobalOpacityCoefficient);
    }

    void GlobalOpacityCoefficient(float newValue)
    {
        m_propertySet.Insert(PROPERTY_GLOBALOPACITYCOEFFICIENT, winrt::box_value(newValue));
    }

    uint32_t VideoStabilizationMaximumBufferLength()
    {
        return PROPERTY_MAX_VSBUFFER;
    }

    bool BlankOnProtectedContent()
    {
        return GetValueFromPropertySet<bool>(m_propertySet, PROPERTY_BLANKONPROTECTEDCONTENT, DefaultBlankOnProtectedContent);
    }

    void BlankOnProtectedContent(bool newValue)
    {
        m_propertySet.Insert(PROPERTY_BLANKONPROTECTEDCONTENT, winrt::box_value(newValue));
    }

    bool ShowHiddenMesh()
    {
        return GetValueFromPropertySet<bool>(m_propertySet, PROPERTY_SHOWHIDDENMESH, DefaultShowHiddenMesh);
    }

    void ShowHiddenMesh(bool newValue)
    {
        m_propertySet.Insert(PROPERTY_SHOWHIDDENMESH, winrt::box_value(newValue));
    }

    winrt::Windows::Foundation::Size OutputSize()
    {
        return GetValueFromPropertySet<winrt::Windows::Foundation::Size>(m_propertySet, PROPERTY_OUTPUTSIZE, DefaultOutputSize);
    }

    void OutputSize(winrt::Windows::Foundation::Size newValue)
    {
        m_propertySet.Insert(PROPERTY_OUTPUTSIZE, winrt::box_value(newValue));
    }

    MixedRealityCapturePerspective PreferredHologramPerspective()
    {
        return GetValueFromPropertySet<uint32_t>(m_propertySet, PROPERTY_PREFERREDHOLOGRAMPERSPECTIVE, DefaultPreferredHologramPerspective);
    }

    void PreferredHologramPerspective(MixedRealityCapturePerspective newValue)
    {
        m_propertySet.Insert(PROPERTY_PREFERREDHOLOGRAMPERSPECTIVE, winrt::box_value(static_cast<uint32_t>(newValue)));
    }

private:
    static constexpr winrt::Windows::Media::Capture::MediaStreamType DefaultStreamType = winrt::Windows::Media::Capture::MediaStreamType::VideoRecord;
    static constexpr bool DefaultHologramCompositionEnabled = true;
    static constexpr bool DefaultRecordingIndicatorEnabled = false;
    static constexpr bool DefaultVideoStabilizationEnabled = false;
    static constexpr uint32_t DefaultVideoStabilizationBufferLength = 0U;
    static constexpr float DefaultGlobalOpacityCoefficient = 0.9f;
    static constexpr bool DefaultBlankOnProtectedContent = false;
    static constexpr bool DefaultShowHiddenMesh = false;
    static constexpr MixedRealityCapturePerspective DefaultPreferredHologramPerspective = MixedRealityCapturePerspective::PhotoVideoCamera;
private:
    winrt::hstring m_activatableClassId = RUNTIMECLASS_MIXEDREALITYCAPTURE_VIDEO_EFFECT;
    winrt::Windows::Foundation::Collections::PropertySet m_propertySet;
    winrt::Windows::Foundation::Size DefaultOutputSize = { 0,0 };
};
