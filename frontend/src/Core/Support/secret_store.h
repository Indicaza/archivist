#pragma once

#include <QString>

// Small secrets such as API keys. On macOS they live in the login Keychain
// (service "Archivist"); elsewhere in the app's settings file, readable only
// by the user.
namespace SecretStore
{
[[nodiscard]] QString read(const QString &account);
// Returns false and sets `error` when the secret could not be saved.
bool write(const QString &account, const QString &secret, QString *error = nullptr);
void remove(const QString &account);
// "macOS Keychain" or "app settings", for messages.
[[nodiscard]] QString locationName();
}
