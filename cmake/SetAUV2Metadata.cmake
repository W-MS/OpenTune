# ==============================================================================
# Set AUv2 metadata required by the ARA SDK 2.3.0 AU example and Logic refresh.
# ==============================================================================

if(NOT DEFINED AU_INFO_PLIST OR "${AU_INFO_PLIST}" STREQUAL "")
    message(FATAL_ERROR
        "SetAUV2Metadata.cmake requires AU_INFO_PLIST")
endif()

if(NOT IS_ABSOLUTE "${AU_INFO_PLIST}")
    message(FATAL_ERROR
        "SetAUV2Metadata.cmake requires an absolute AU_INFO_PLIST path: ${AU_INFO_PLIST}")
endif()

if(NOT EXISTS "${AU_INFO_PLIST}")
    message(FATAL_ERROR
        "AU Info.plist does not exist: ${AU_INFO_PLIST}")
endif()

if(IS_DIRECTORY "${AU_INFO_PLIST}")
    message(FATAL_ERROR
        "AU_INFO_PLIST is a directory, not a plist file: ${AU_INFO_PLIST}")
endif()

set(_opentune_plistbuddy "/usr/libexec/PlistBuddy")
if(NOT EXISTS "${_opentune_plistbuddy}")
    message(FATAL_ERROR
        "PlistBuddy is required to patch AU Info.plist: ${_opentune_plistbuddy}")
endif()

# JUCE's current AUv2 ARA generator creates AudioComponents[0].tags=[ARA].
# Replace only that existing key so the final value is exactly [Effect, ARA].
set(_opentune_plistbuddy_commands
    "Delete :AudioComponents:0:tags"
    "Add :AudioComponents:0:tags array"
    "Add :AudioComponents:0:tags: string Effect"
    "Add :AudioComponents:0:tags: string ARA"
)

foreach(_opentune_command IN LISTS _opentune_plistbuddy_commands)
    execute_process(
        COMMAND "${_opentune_plistbuddy}" -c "${_opentune_command}" "${AU_INFO_PLIST}"
        RESULT_VARIABLE _opentune_result
        OUTPUT_VARIABLE _opentune_output
        ERROR_VARIABLE _opentune_error
    )
    if(NOT _opentune_result EQUAL 0)
        message(FATAL_ERROR
            "Failed to patch AU Info.plist with '${_opentune_command}': ${_opentune_error}")
    endif()
endforeach()
