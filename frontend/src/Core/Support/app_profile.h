#pragma once

#include <QString>

// A disposable application profile, for test runs (the Observatory UI Lab).
//
// When ARCHIVIST_PROFILE_DIR names a directory, everything Archivist keeps
// for the person lives under it instead: the database, QSettings (as an INI
// file), unsaved-edit recovery and imported themes. Without it nothing
// changes. Isolation never falls back to the real profile: an unusable
// directory is reported, and the UI Lab refuses to run.
namespace AppProfile
{
// The profile directory, or empty when no test profile was asked for.
[[nodiscard]] QString directory();
[[nodiscard]] bool active();
// Where per-user app data goes: the profile, else the platform's
// AppDataLocation.
[[nodiscard]] QString dataLocation();
// Points QSettings() at <profile>/settings. Call once, right after the
// application's name and organization are set and before any QSettings is
// made. Returns false when the profile directory can't be created.
bool isolateSettings();
// True when `path` is inside the profile directory.
[[nodiscard]] bool contains(const QString &path);
} // namespace AppProfile
