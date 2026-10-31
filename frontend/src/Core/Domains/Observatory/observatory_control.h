#pragma once

#include "observatory_records.h"

#include <QByteArray>
#include <QString>

// The in-app half of the Observatory controls. The dashboard page inside
// Archivist posts control operations (run a UI test, check or cancel it,
// write a report or an evidence export) to the IDE host at
// /observatory/api/control/<operation>. This file only checks the request
// and hands it to the one implementation in scripts/observatory:
//
//   node scripts/observatory/cli.mjs control <operation>   (input on stdin)
//
// so the browser dashboard, the in-app panel and agents share the same
// controller, job records and limits. The operation name is checked here,
// and its input is validated again by the script. No path, program or
// argument from the request reaches the command line beyond the
// operation's name.
namespace Observatory
{
// A fresh random key for this app session. Only the Observatory panel gets
// it (through QML); a request without it is refused.
QByteArray newControlToken();

// Constant-time comparison of a request's key with the session's.
bool sameControlToken(const QByteArray &given, const QByteArray &expected);

// "observatory.tests.run" and the like: lowercase words joined by dots.
bool isControlOperationName(const QString &operation);

// observatory.live.status/start/stop/mark/wait: operations that reach the
// running app's recorder. The control bridge refuses them; the panel calls
// /observatory/api/live/<operation> instead.
bool reachesTheRunningApp(const QString &operation);

// scripts/observatory/cli.mjs in a development build (repository paths),
// else empty: a bundled release has no test runner to drive.
QString controlScriptPath();

// The node executable on PATH, or in the usual Homebrew and system
// locations when Archivist was opened from the Finder with a short PATH.
// Empty when there is none.
QString nodeExecutable();

// A refusal in the same { schema, ok: false, error: { code, message } }
// envelope the script answers with.
Response controlError(int status, const QString &operation, const QString &code, const QString &message);

// Runs one operation and returns the script's JSON response (always a
// { schema, ok, result | error } envelope), or an error envelope when the
// script can't be started or doesn't answer in time.
Response runControlOperation(const QString &root, const QString &operation, const QByteArray &input);
} // namespace Observatory
