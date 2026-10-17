#!/bin/bash
# Remove the temporary keychain and decoded signing material from the runner.
set -u

KEYCHAIN_PATH="${RUNNER_TEMP}/opentune-signing.keychain-db"
if [ -f "${KEYCHAIN_PATH}" ]; then
    security delete-keychain "${KEYCHAIN_PATH}" || true
fi
rm -f \
    "${RUNNER_TEMP}/application.p12" \
    "${RUNNER_TEMP}/installer.p12" \
    "${RUNNER_TEMP}/AuthKey.p8" \
    "${RUNNER_TEMP}/notarize.json" \
    "${RUNNER_TEMP}/notarize.err"
