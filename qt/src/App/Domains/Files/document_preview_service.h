#pragma once

#include <QObject>
#include <QProcess>
#include <QString>
#include <QUrl>

class DocumentPreviewService : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QUrl previewUrl READ previewUrl NOTIFY previewUrlChanged)
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)
    Q_PROPERTY(QString converterLabel READ converterLabel NOTIFY converterLabelChanged)

public:
    explicit DocumentPreviewService(QObject *parent = nullptr);
    ~DocumentPreviewService() override;

    [[nodiscard]] QUrl previewUrl() const;
    [[nodiscard]] QString state() const;
    [[nodiscard]] QString errorMessage() const;
    [[nodiscard]] QString converterLabel() const;

    Q_INVOKABLE void openDocument(
        const QString &libraryRootPath,
        const QString &relativePath
    );
    Q_INVOKABLE void clear();

signals:
    void previewUrlChanged();
    void stateChanged();
    void errorMessageChanged();
    void converterLabelChanged();

private:
    void setPreviewUrl(const QUrl &url);
    void setState(const QString &state);
    void setErrorMessage(const QString &message);
    void setConverterLabel(const QString &label);
    void fail(const QString &message);

    QProcess m_process;
    QUrl m_previewUrl;
    QString m_state = QStringLiteral("idle");
    QString m_errorMessage;
    QString m_converterLabel;
    QString m_jobDirectory;
    QString m_cachePath;
    quint64 m_requestRevision = 0;
};
