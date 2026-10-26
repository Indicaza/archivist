#include "secret_store.h"

#include <QSettings>

#if defined(Q_OS_MACOS)
#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>

namespace
{
const QString service = QStringLiteral("Archivist");

// Owns a CoreFoundation reference.
template<typename T>
struct CFHandle
{
    T value = nullptr;
    explicit CFHandle(T created = nullptr) : value(created) {}
    ~CFHandle() { if (value) CFRelease(value); }
    CFHandle(const CFHandle &) = delete;
    CFHandle &operator=(const CFHandle &) = delete;
};

CFStringRef createString(const QString &value)
{
    return CFStringCreateWithCharacters(
        kCFAllocatorDefault,
        reinterpret_cast<const UniChar *>(value.utf16()),
        value.size()
    );
}

CFMutableDictionaryRef createQuery(const QString &account)
{
    CFMutableDictionaryRef query = CFDictionaryCreateMutable(
        kCFAllocatorDefault,
        0,
        &kCFTypeDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks
    );
    CFHandle<CFStringRef> serviceName(createString(service));
    CFHandle<CFStringRef> accountName(createString(account));
    CFDictionarySetValue(query, kSecClass, kSecClassGenericPassword);
    CFDictionarySetValue(query, kSecAttrService, serviceName.value);
    CFDictionarySetValue(query, kSecAttrAccount, accountName.value);
    return query;
}

QString statusMessage(OSStatus status)
{
    CFHandle<CFStringRef> message(SecCopyErrorMessageString(status, nullptr));

    if (!message.value) {
        return QStringLiteral("Keychain error %1.").arg(status);
    }

    const CFIndex length = CFStringGetLength(message.value);
    QString text(length, Qt::Uninitialized);
    CFStringGetCharacters(message.value, CFRangeMake(0, length), reinterpret_cast<UniChar *>(text.data()));
    return text;
}
}

namespace SecretStore
{
QString read(const QString &account)
{
    CFHandle<CFMutableDictionaryRef> query(createQuery(account));
    CFDictionarySetValue(query.value, kSecReturnData, kCFBooleanTrue);
    CFDictionarySetValue(query.value, kSecMatchLimit, kSecMatchLimitOne);
    CFTypeRef result = nullptr;

    if (SecItemCopyMatching(query.value, &result) != errSecSuccess || !result) {
        return {};
    }

    CFHandle<CFDataRef> data(static_cast<CFDataRef>(result));
    return QString::fromUtf8(reinterpret_cast<const char *>(CFDataGetBytePtr(data.value)), CFDataGetLength(data.value));
}

bool write(const QString &account, const QString &secret, QString *error)
{
    const QByteArray bytes = secret.toUtf8();
    CFHandle<CFDataRef> data(CFDataCreate(kCFAllocatorDefault, reinterpret_cast<const UInt8 *>(bytes.constData()), bytes.size()));
    CFHandle<CFMutableDictionaryRef> query(createQuery(account));
    CFHandle<CFMutableDictionaryRef> changes(CFDictionaryCreateMutable(
        kCFAllocatorDefault,
        0,
        &kCFTypeDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks
    ));
    CFDictionarySetValue(changes.value, kSecValueData, data.value);
    OSStatus status = SecItemUpdate(query.value, changes.value);

    if (status == errSecItemNotFound) {
        CFDictionarySetValue(query.value, kSecValueData, data.value);
        CFDictionarySetValue(query.value, kSecAttrAccessible, kSecAttrAccessibleWhenUnlocked);
        status = SecItemAdd(query.value, nullptr);
    }

    if (status != errSecSuccess && error) {
        *error = statusMessage(status);
    }

    return status == errSecSuccess;
}

void remove(const QString &account)
{
    CFHandle<CFMutableDictionaryRef> query(createQuery(account));
    SecItemDelete(query.value);
}

QString locationName()
{
    return QStringLiteral("macOS Keychain");
}
}

#else

namespace SecretStore
{
QString read(const QString &account)
{
    return QSettings().value(QStringLiteral("secrets/") + account).toString();
}

bool write(const QString &account, const QString &secret, QString *error)
{
    QSettings settings;
    settings.setValue(QStringLiteral("secrets/") + account, secret);
    settings.sync();

    if (settings.status() != QSettings::NoError) {
        if (error) {
            *error = QStringLiteral("The app settings file could not be written.");
        }

        return false;
    }

    return true;
}

void remove(const QString &account)
{
    QSettings settings;
    settings.remove(QStringLiteral("secrets/") + account);
}

QString locationName()
{
    return QStringLiteral("app settings");
}
}

#endif
