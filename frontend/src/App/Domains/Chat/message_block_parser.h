#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

// Splits a chat message into the blocks RichMessageContent.qml renders:
// markdown prose, tables ("structuredMarkdown"), fenced code and dividers,
// each with revealStart/revealEnd offsets for the typing reveal.
//
// This is the C++ port of the parseBlocks() function that used to run in QML
// JavaScript on every streaming flush. Output is identical; see
// tests/message_block_parser_test.cpp and its fixtures.
class MessageBlockParser final : public QObject
{
    Q_OBJECT

public:
    explicit MessageBlockParser(QObject *parent = nullptr);

    Q_INVOKABLE QVariantList parse(const QString &content) const;

    [[nodiscard]] static QVariantList parseBlocks(const QString &content);
};
