#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ANDROID_DIR="$ROOT_DIR/android-cast"
GRADLE_VERSION="8.14.2"
CACHE_DIR="$ROOT_DIR/.tools/gradle-$GRADLE_VERSION"

if [[ -n "${GRADLE_BIN:-}" && -x "$GRADLE_BIN" ]]; then
    GRADLE=("$GRADLE_BIN")
elif [[ -x "$ANDROID_DIR/gradlew" ]]; then
    GRADLE=("$ANDROID_DIR/gradlew")
elif command -v gradle >/dev/null 2>&1; then
    GRADLE=("$(command -v gradle)")
else
    GRADLE_BIN="$CACHE_DIR/gradle-$GRADLE_VERSION/bin/gradle"
    if [[ ! -x "$GRADLE_BIN" ]]; then
        ARCHIVE="$CACHE_DIR/gradle-$GRADLE_VERSION-bin.zip"
        mkdir -p "$CACHE_DIR"
        echo "Downloading Gradle $GRADLE_VERSION..."
        curl --fail --location --retry 3 --output "$ARCHIVE" \
            "https://services.gradle.org/distributions/gradle-$GRADLE_VERSION-bin.zip"
        unzip -q "$ARCHIVE" -d "$CACHE_DIR"
        rm -f "$ARCHIVE"
    fi
    GRADLE=("$GRADLE_BIN")
fi

"${GRADLE[@]}" -p "$ANDROID_DIR" :app:assembleDebug

if [[ "${RUN_TESTS:-0}" == "1" ]]; then
    "${GRADLE[@]}" -p "$ANDROID_DIR" :app:testDebugUnitTest
fi

APK="$ANDROID_DIR/app/build/outputs/apk/debug/app-debug.apk"
if [[ ! -f "$APK" ]]; then
    echo "APK was not generated: $APK" >&2
    exit 1
fi

echo "APK built: $APK"
