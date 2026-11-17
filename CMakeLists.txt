cmake_minimum_required(VERSION 3.22)
project(VoiceSampler VERSION 1.0.0)

# Put the JUCE folder next to this file (git clone https://github.com/juce-framework/JUCE)
add_subdirectory(JUCE)

juce_add_plugin(VoiceSampler
    COMPANY_NAME "MyPlugins"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    PLUGIN_MANUFACTURER_CODE Mypl
    PLUGIN_CODE Vsmp
    FORMATS VST3 Standalone
    COPY_PLUGIN_AFTER_BUILD TRUE
    PRODUCT_NAME "Voice Sampler")

target_sources(VoiceSampler PRIVATE Source/VoiceSampler.cpp)

target_compile_definitions(VoiceSampler PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(VoiceSampler
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
