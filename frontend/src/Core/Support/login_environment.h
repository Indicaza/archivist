#pragma once

#include <QString>

// Apps opened from Finder get launchd's bare PATH (/usr/bin:/bin:...), so the
// IDE terminal and language servers would not find Homebrew, nvm or cargo
// tools. This asks the user's login shell for its PATH once at startup.
namespace LoginEnvironment
{
// True when PATH looks like launchd's default rather than a shell's.
[[nodiscard]] bool hasBarePath(const QString &path);
// `login` entries first, then any `current` entries it lacks.
[[nodiscard]] QString mergedPath(const QString &login, const QString &current);
// macOS only, and only when PATH is bare. Gives up after a few seconds.
// ARCHIVIST_SKIP_LOGIN_PATH=1 turns it off.
void adoptLoginShellPath();
}
