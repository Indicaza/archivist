#include <QCoreApplication>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <qqml.h>

#include "App/Domains/Agent/agent_store.h"
#include "App/Domains/Chat/chat_store.h"
#include "App/Domains/Collection/collection_store.h"
#include "App/Domains/Library/library_store.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    QCoreApplication::setApplicationName("Archivist");
    QCoreApplication::setApplicationVersion("0.1.0");
    QCoreApplication::setOrganizationName("Archivist");
    QCoreApplication::setOrganizationDomain("archivist.local");

    QQuickStyle::setStyle("Basic");

    AgentStore agentStore;
    CollectionStore collectionStore;
    LibraryStore libraryStore;
    ChatStore chatStore;
    qmlRegisterSingletonInstance("Archivist.Services", 1, 0, "AgentStore", &agentStore);
    qmlRegisterSingletonInstance(
        "Archivist.Services",
        1,
        0,
        "CollectionStore",
        &collectionStore
    );
    qmlRegisterSingletonInstance("Archivist.Services", 1, 0, "LibraryStore", &libraryStore);
    qmlRegisterSingletonInstance("Archivist.Services", 1, 0, "ChatStore", &chatStore);

    QQmlApplicationEngine engine;

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection
    );

    engine.loadFromModule("Archivist", "App");

    return app.exec();
}
