import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import vm from "node:vm";
import { fileURLToPath } from "node:url";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const policyPath = path.join(
  root,
  "frontend/qml/App/Workbench/Workspace/ChatViewportPolicy.js",
);
const workspacePath = path.join(
  root,
  "frontend/qml/App/Workbench/Workspace/Workspace.qml",
);
const messagePath = path.join(
  root,
  "frontend/qml/App/Workbench/Workspace/ChatMessage/ChatMessage.qml",
);
const launchRegistryPath = path.join(
  root,
  "frontend/qml/App/Workbench/Workspace/ChatMessage/ChatLaunchRegistry.js",
);
const codeFoldStatePath = path.join(
  root,
  "frontend/qml/App/Workbench/Workspace/ChatMessage/CodeFoldState.js",
);
const activityRowPath = path.join(
  root,
  "frontend/qml/App/Workbench/Workspace/ChatMessage/RunActivityRow.qml",
);
const cmakePath = path.join(root, "frontend/CMakeLists.txt");
const mainPath = path.join(root, "frontend/src/main.cpp");
const runtimeTraceHeaderPath = path.join(
  root,
  "frontend/src/App/Services/runtime_trace_store.h",
);
const runtimeTraceSourcePath = path.join(
  root,
  "frontend/src/App/Services/runtime_trace_store.cpp",
);
const diagnoseChatViewportPath = path.join(
  root,
  "scripts/diagnose-chat-viewport",
);

function read(filePath) {
  return fs.readFileSync(filePath, "utf8");
}

function loadPolicy() {
  const context = vm.createContext({
    Boolean,
    Math,
    Number,
  });
  const source = read(policyPath).replace(
    /^\s*\.pragma library\s*$/m,
    "",
  );
  vm.runInContext(source, context, {
    filename: policyPath,
  });
  return context;
}

function loadQmlLibrary(filePath) {
  const context = vm.createContext({
    Boolean,
    Date,
    Math,
    Number,
    String,
  });
  const source = read(filePath).replace(
    /^\s*\.pragma library\s*$/m,
    "",
  );
  vm.runInContext(source, context, {
    filename: filePath,
  });
  return context;
}

function approximately(actual, expected, tolerance = 0.001) {
  assert.ok(
    Math.abs(actual - expected) <= tolerance,
    `Expected ${actual} to be within ${tolerance} of ${expected}.`,
  );
}

const policy = loadPolicy();
const launchRegistry = loadQmlLibrary(launchRegistryPath);
const codeFoldState = loadQmlLibrary(codeFoldStatePath);

{
  const now = Date.now();
  const fresh = {
    id: "user-1",
    role: "user",
    chatId: "chat-a",
    content: "hello",
    createdAt: new Date(now).toISOString(),
  };
  assert.equal(
    launchRegistry.claim(fresh, true),
    true,
    "A message that was just sent at the end of the transcript must play its launch once.",
  );
  assert.equal(
    launchRegistry.claim({ ...fresh, id: "user-1-saved" }, true),
    false,
    "The same message must not launch again when the store swaps its temporary id.",
  );
  assert.equal(
    launchRegistry.claim(
      {
        ...fresh,
        id: "user-2",
        content: "older",
        createdAt: new Date(now - 60000).toISOString(),
      },
      true,
    ),
    false,
    "Old messages scrolled back into view must never launch.",
  );
  assert.equal(
    launchRegistry.claim({ ...fresh, id: "user-3", content: "mid" }, false),
    false,
    "Only the last row of the transcript may launch.",
  );
}

codeFoldState.setExpanded("block-a", true);
assert.equal(
  codeFoldState.isExpanded("block-a"),
  true,
  "An expanded code block must stay expanded when its delegate is recycled.",
);
assert.equal(codeFoldState.isExpanded("block-b"), false);

{
  const short = policy.jumpPlan(900, 800);
  assert.equal(short.launch, 0, "A short trip to the bottom scrolls the whole way.");
  approximately(short.travel, 900);
  assert.ok(short.duration >= 240 && short.duration <= 440);

  const long = policy.jumpPlan(120000, 800);
  assert.ok(long.launch > 0, "A long trip launches, then cuts close to the end.");
  assert.ok(
    long.launch + long.travel < 4 * 800,
    "A long trip must never animate through the whole transcript.",
  );
  assert.equal(long.duration, 480);
}

{
  const zone = policy.contentZone(1200, 0, 24, 760);
  approximately(zone.x, 220);
  approximately(zone.width, 760);
}

{
  const zone = policy.contentZone(1200, 48, 24, 760);
  approximately(zone.x, 220);
  approximately(zone.width, 760);
  approximately(zone.x + zone.width / 2, 600);
}

{
  const zone = policy.contentZone(1200, 280, 24, 760);
  approximately(zone.x, 304);
  approximately(zone.width, 760);
  approximately(zone.x + zone.width / 2, 684);
}

{
  const zone = policy.contentZone(1200, 700, 24, 760);
  approximately(zone.x, 724);
  approximately(zone.width, 452);
  approximately(zone.x + zone.width / 2, 950);
}

{
  const zone = policy.contentZone(320, 400, 24, 760);
  approximately(zone.x, 320);
  approximately(zone.width, 0);
}

approximately(policy.clampContentY(0, 2000, 800, 42, 44, -500), -42);
approximately(policy.clampContentY(0, 2000, 800, 42, 44, 5000), 1244);
approximately(policy.clampContentY(0, 2000, 800, 42, 44, 600), 600);

assert.equal(
  policy.isNearEnd(0, 2000, 800, 42, 44, 1244, 96),
  true,
  "A transcript inside the follow threshold must count as near the end.",
);
assert.equal(
  policy.isNearEnd(0, 2000, 800, 42, 44, 1000, 96),
  false,
  "A manually scrolled transcript must not count as near the end.",
);

assert.equal(policy.scrollDuration(0, true), 0);
assert.equal(policy.scrollDuration(1, false), 0);
assert.ok(
  policy.scrollDuration(120, false) >= 90
    && policy.scrollDuration(120, false) <= 180,
  "Viewport restoration should use a short bounded glide.",
);
assert.ok(
  policy.scrollDuration(5000, true) >= 220
    && policy.scrollDuration(5000, true) <= 520,
  "Explicit bottom jumps should stay smooth without becoming sluggish.",
);
assert.ok(
  policy.scrollDuration(5000, true) > policy.scrollDuration(120, false),
  "Long explicit jumps should ease longer than short restoration adjustments.",
);

assert.equal(
  typeof policy.shouldFollow,
  "undefined",
  "Generation-time auto-follow must not exist in the viewport policy.",
);
assert.equal(
  typeof policy.shouldEnableFollow,
  "undefined",
  "Finishing a manual scroll must never re-enable generation-time follow.",
);

assert.equal(
  policy.shouldPrefetchHistory({
    visible: true,
    hasOlderMessages: true,
    nearBeginning: true,
    loadingMessages: false,
    loadingOlderMessages: false,
    historyLoadPending: false,
    restoringViewport: false,
    responding: true,
    autoFollow: true,
    scrollToEndPending: false,
  }),
  false,
  "Streaming at the bottom must never trigger an older-history prepend.",
);
assert.equal(
  policy.shouldPrefetchHistory({
    visible: true,
    hasOlderMessages: true,
    nearBeginning: true,
    loadingMessages: false,
    loadingOlderMessages: false,
    historyLoadPending: false,
    restoringViewport: false,
    responding: false,
    autoFollow: false,
    scrollToEndPending: false,
  }),
  true,
  "A manually scrolled transcript near the beginning should prefetch older history.",
);
assert.equal(
  policy.shouldPrefetchHistory({
    visible: true,
    hasOlderMessages: true,
    nearBeginning: true,
    loadingMessages: false,
    loadingOlderMessages: false,
    historyLoadPending: false,
    restoringViewport: true,
    responding: false,
    autoFollow: false,
    scrollToEndPending: false,
  }),
  false,
  "Chat viewport restoration must finish before history prefetch can begin.",
);
assert.equal(
  policy.shouldPrefetchHistory({
    visible: true,
    hasOlderMessages: true,
    nearBeginning: true,
    loadingMessages: false,
    loadingOlderMessages: false,
    historyLoadPending: false,
    restoringViewport: false,
    responding: false,
    autoFollow: false,
    scrollToEndPending: true,
  }),
  false,
  "A pending jump to the end must not race an older-history prepend.",
);

assert.equal(
  policy.shouldPrefetchHistory({
    visible: true,
    hasOlderMessages: true,
    nearBeginning: true,
    loadingMessages: false,
    loadingOlderMessages: false,
    historyLoadPending: false,
    restoringViewport: false,
    responding: false,
    autoFollow: false,
    scrollToEndPending: false,
    interacting: true,
  }),
  false,
  "Dragging, wheel movement, or flicking must not start a history prepend.",
);

const workspace = read(workspacePath);
const message = read(messagePath);
const activityRow = read(activityRowPath);
const cmake = read(cmakePath);
const main = read(mainPath);
const runtimeTraceHeader = read(runtimeTraceHeaderPath);
const runtimeTraceSource = read(runtimeTraceSourcePath);
const diagnoseChatViewport = read(diagnoseChatViewportPath);

assert.match(workspace, /import "ChatViewportPolicy\.js" as ChatViewportPolicy/);
assert.doesNotMatch(
  workspace,
  /Diagnostics\/RuntimeTrace\.js/,
  "Runtime tracing must use the native durable trace sink instead of console scraping.",
);
assert.match(workspace, /reuseItems:\s*true/);
assert.match(workspace, /ChatViewportPolicy\.shouldPrefetchHistory/);
assert.match(
  workspace,
  /shouldRestoreEnd[\s\S]*scheduleScrollToEnd\(false\)/,
  "Restoring a viewport saved at the end must use the explicit navigation controller.",
);
assert.match(workspace, /readonly property real distanceFromEnd:/);
assert.match(
  workspace,
  /historyPrefetchDistance:\s*Math\.max\(\s*48,\s*transcript\.height \* 0\.06/,
  "History prefetch must stay almost at the real beginning of the transcript.",
);
assert.match(
  read(policyPath),
  /^\.pragma library/m,
  "The shared QML policy must be loaded once without the CMake re-evaluation warning.",
);
assert.doesNotMatch(
  workspace,
  /SmoothedAnimation\s*\{\s*id:\s*scrollToEndAnimation/,
  "Jump to latest must not animate toward a stale estimated contentY target.",
);
assert.match(
  workspace,
  /function placeTranscriptAtEnd\(\)[\s\S]*positionViewAtIndex\([\s\S]*transcript\.count - 1,[\s\S]*ListView\.End[\s\S]*transcript\.contentY = targetY/,
  "Jump to latest must place the last message directly and clamp to the current exact end.",
);
assert.match(
  workspace,
  /id:\s*scrollToEndSettleTimer[\s\S]*interval:\s*24[\s\S]*finishScrollToEnd\(\)/,
  "Bottom navigation must use a quiet settle pass instead of stale-target momentum.",
);
assert.match(
  workspace,
  /function jumpToLatest\(\)[\s\S]*clearHistoryAnchor\(ChatStore\.loadingOlderMessages\)[\s\S]*startAnimatedJump\(\)/,
  "Jump to latest must override history restoration before it starts the fast scroll.",
);
assert.match(
  workspace,
  /id:\s*jumpAnimation[\s\S]*onFinished:\s*root\.scheduleScrollToEnd\(true\)/,
  "The fast scroll to the bottom must hand over to deterministic end placement.",
);
assert.match(
  workspace,
  /function startAnimatedJump\(\)[\s\S]*reducedMotion[\s\S]*scheduleScrollToEnd\(true\)/,
  "With reduced motion, jump to latest must place the end directly.",
);
assert.match(
  workspace,
  /function takeManualScrollOwnership\(\)[\s\S]*jumpAnimation\.stop\(\)/,
  "Any real user movement must stop the fast scroll at once.",
);
assert.match(
  workspace,
  /function finishScrollToEnd\(\)[\s\S]*remaining > 0\.75[\s\S]*scrollToEndSettlePass < 4[\s\S]*placeTranscriptAtEnd\(\)/,
  "Bottom navigation must re-place only when late layout changes move the real end.",
);
assert.match(
  workspace,
  /boundsBehavior:\s*Flickable\.StopAtBounds[\s\S]*boundsMovement:\s*Flickable\.StopAtBounds/,
  "Manual transcript movement must never intentionally overshoot its current bounds.",
);
assert.match(
  workspace,
  /function takeManualScrollOwnership\(\)[\s\S]*cancelScrollToEnd\(\)[\s\S]*cancelChatViewportRestore\(\)/,
  "Any real user movement must immediately cancel programmatic navigation.",
);
assert.doesNotMatch(
  workspace.slice(
    workspace.indexOf("function takeManualScrollOwnership"),
    workspace.indexOf("function placeTranscriptAtEnd"),
  ),
  /programmaticScrollWrite/,
  "A stale programmatic-write grace window must never block real manual scrolling.",
);
assert.doesNotMatch(
  workspace,
  /revealFollowEnabled|shouldFollowTranscript|followRevealSmoothly|stopRevealFollow/,
  "Generation-time auto-follow state and helpers must not return.",
);
assert.doesNotMatch(
  workspace,
  /runPhase === "run\.started"/,
  "Starting a Run must not write transcript scroll position.",
);

assert.match(
  workspace,
  /cacheBuffer:\s*Math\.max\(1200,\s*Math\.ceil\(height \* 2\)\)/,
  "Variable-height chat delegates should keep a bounded measured buffer around the viewport.",
);
assert.doesNotMatch(
  message,
  /ChatMessageHeightCache/,
  "Row heights come from the native text layout cache, not a QML height cache.",
);
assert.match(
  message,
  /required property var message/,
  "A recycled delegate must receive one model object so message identity and content rebind together.",
);
assert.match(
  workspace,
  /message:\s*messageData \|\| \(\{\}\)/,
  "Workspace must pass one atomic model object into each ChatMessage delegate.",
);
assert.match(
  workspace,
  /model:\s*ChatStore\.messageModel/,
  "The transcript must bind to the row-level ChatMessageModel, never the whole messages list.",
);
assert.doesNotMatch(
  workspace,
  /model:\s*ChatStore\.messages\b/,
  "Binding the transcript to the QVariantList rebuilds every delegate on each change.",
);
assert.match(
  workspace,
  /RuntimeTrace\.emitEvent\("chat\.viewport", eventName, payload\)/,
  "Chat viewport diagnostics must emit structured events through the native trace sink.",
);
assert.match(
  runtimeTraceHeader,
  /Q_INVOKABLE void emitEvent/,
  "The runtime trace service must expose a generic QML event boundary.",
);
assert.match(
  runtimeTraceSource,
  /runtime-trace\.jsonl/,
  "Runtime diagnostics must persist to a deterministic local JSONL trace file.",
);
assert.match(
  runtimeTraceSource,
  /MaxTraceBytes/,
  "The runtime trace file must remain bounded during long diagnostic sessions.",
);
assert.match(
  runtimeTraceSource,
  /QJsonDocument::Compact/,
  "Runtime diagnostics must use structured compact JSON records.",
);
assert.match(
  main,
  /qmlRegisterSingletonInstance\([\s\S]*"RuntimeTrace"[\s\S]*&runtimeTraceStore/,
  "The native runtime trace service must be registered for QML before the app loads.",
);
assert.match(
  workspace,
  /ArchivistDiagnostics[\s\S]*chatViewportTraceEnabled/,
  "Chat viewport tracing must be gated by explicit diagnostic flags.",
);
assert.doesNotMatch(
  diagnoseChatViewport,
  /\\n\s*\|/,
  "Diagnostic pipelines must use real shell line continuations, not literal \\n arguments.",
);
assert.match(
  diagnoseChatViewport,
  /===== GEOMETRY SUMMARY =====/,
  "The Chat report must summarize geometry failures before dumping raw trace events.",
);
assert.match(
  diagnoseChatViewport,
  /Suspicious message-height changes:/,
  "The Chat report must surface large per-message height changes directly.",
);
assert.match(
  diagnoseChatViewport,
  /Out-of-bounds viewport samples:/,
  "The Chat report must report camera positions outside the current transcript bounds.",
);
assert.match(
  workspace,
  /movement\.ended[\s\S]*settleTraceTimer\.restart\(\)/,
  "A completed flick must record delayed settle samples so post-scroll jumps are observable.",
);
assert.match(
  workspace,
  /contentY\.idle\.changed/,
  "Unexpected contentY changes after movement settles must be traced.",
);
assert.match(
  workspace,
  /history\.willPrepend[\s\S]*history\.prepended/,
  "History pagination must emit trace events around model insertion.",
);
assert.match(
  workspace,
  /viewport\.save\.begin[\s\S]*viewport\.save\.end/,
  "Viewport persistence must emit trace boundaries around passive anchor capture.",
);
assert.match(
  workspace,
  /chatDisableHistoryPrefetch[\s\S]*function canPrefetchHistory\(\)[\s\S]*return false/,
  "The trace harness must support isolating history prefetch without another code patch.",
);
assert.match(
  workspace,
  /chatDisableViewportSave[\s\S]*function scheduleChatViewportSave\(\)[\s\S]*return/,
  "The trace harness must support isolating viewport persistence without another code patch.",
);
assert.match(
  message,
  /diagnosticPlainContent[\s\S]*richContent\.implicitHeight/,
  "The trace harness must support a cheap plain-message renderer for rich-content isolation.",
);
assert.match(
  message,
  /delegate\.height/,
  "Recycled message geometry changes must be observable by message ID.",
);
assert.match(
  cmake,
  /src\/App\/Services\/runtime_trace_store\.cpp/,
  "The generic runtime trace service must be compiled into the native application.",
);
assert.doesNotMatch(
  cmake,
  /qml\/App\/Diagnostics\/RuntimeTrace\.js/,
  "The failed console-based trace helper must not remain packaged.",
);

{
  const tempDir = fs.mkdtempSync(
    path.join(os.tmpdir(), "archivist-chat-diagnostic-"),
  );
  const traceFile = path.join(tempDir, "runtime-trace.jsonl");
  const frontendLog = path.join(tempDir, "frontend.log");
  const reportFile = path.join(tempDir, "report.txt");

  try {
    fs.writeFileSync(
      traceFile,
      [
        JSON.stringify({ domain: "runtime", event: "ignored" }),
        JSON.stringify({
          domain: "chat.viewport",
          event: "movement.ended",
          contentY: 1234,
          contentHeight: 5000,
          originY: 0,
          minimumY: -42,
          maximumY: 4244,
        }),
        JSON.stringify({
          domain: "chat.viewport",
          event: "delegate.height",
          messageId: "message-a",
          previousHeight: 3000,
          height: 52,
          contentY: 1234,
          contentHeight: 2052,
          originY: 500,
          minimumY: 458,
          maximumY: 1812,
        }),
      ].join("\n") + "\n",
    );
    fs.writeFileSync(frontendLog, "synthetic warning\n");

    execFileSync("bash", [diagnoseChatViewportPath], {
      cwd: root,
      env: {
        ...process.env,
        ARCHIVIST_CHAT_TRACE_FILE: traceFile,
        ARCHIVIST_FRONTEND_LOG_FILE: frontendLog,
        ARCHIVIST_CHAT_REPORT_FILE: reportFile,
      },
      stdio: "pipe",
    });

    const report = read(reportFile);
    assert.match(
      report,
      /TRACE EVENT COUNT[\s\S]*\n2\n/,
      "The Chat diagnostic script must count filtered viewport events correctly.",
    );
    assert.match(
      report,
      /movement\.ended/,
      "The Chat diagnostic script must include captured viewport events.",
    );
    assert.match(
      report,
      /Suspicious message-height changes:[\s\S]*message-a:[\s\S]*3000 -> 52/,
      "The Chat diagnostic report must summarize catastrophic message-height collapses.",
    );
    assert.doesNotMatch(
      report,
      /\"event\":\"ignored\"/,
      "The Chat diagnostic report must exclude unrelated runtime domains.",
    );
  } finally {
    fs.rmSync(tempDir, { recursive: true, force: true });
  }
}

const messagesChangedStart = workspace.indexOf("function onMessagesChanged");
const selectedChatChangedStart = workspace.indexOf(
  "function onSelectedChatIdChanged",
  messagesChangedStart,
);
assert.ok(messagesChangedStart >= 0 && selectedChatChangedStart > messagesChangedStart);
assert.doesNotMatch(
  workspace.slice(messagesChangedStart, selectedChatChangedStart),
  /scheduleScrollToEnd/,
  "Message updates must not move the viewport during generation.",
);

const countChangedStart = workspace.indexOf("onCountChanged:");
const contentHeightStart = workspace.indexOf(
  "onContentHeightChanged:",
  countChangedStart,
);
assert.ok(countChangedStart >= 0 && contentHeightStart > countChangedStart);
assert.match(
  workspace.slice(countChangedStart, contentHeightStart),
  /ChatStore\.responding[\s\S]*positionViewAtIndex\([\s\S]*count - 1,[\s\S]*ListView\.Contain/,
  "Appending a new response may reveal that row once without enabling follow.",
);

const contentYStart = workspace.indexOf("onContentYChanged:", contentHeightStart);
assert.ok(contentHeightStart >= 0 && contentYStart > contentHeightStart);
assert.doesNotMatch(
  workspace.slice(contentHeightStart, contentYStart),
  /scheduleScrollToEnd/,
  "Growing response height must never start viewport movement.",
);
assert.doesNotMatch(
  workspace,
  /onRevealProgressed:\s*\{/,
  "Response reveal progress must not drive transcript scrolling.",
);

const placeEndStart = workspace.indexOf("function placeTranscriptAtEnd");
const scheduleStart = workspace.indexOf(
  "function scheduleScrollToEnd",
  placeEndStart,
);
const cancelStart = workspace.indexOf("function cancelScrollToEnd", scheduleStart);
const finishStart = workspace.indexOf("function finishScrollToEnd", cancelStart);
const jumpStart = workspace.indexOf("function jumpToLatest", finishStart);
const captureStart = workspace.indexOf("function captureHistoryAnchor");
const restoreHistoryStart = workspace.indexOf(
  "function restoreHistoryAnchor",
  captureStart,
);
const clearHistoryStart = workspace.indexOf(
  "function clearHistoryAnchor",
  restoreHistoryStart,
);
assert.ok(
  placeEndStart >= 0
    && scheduleStart > placeEndStart
    && cancelStart > scheduleStart
    && finishStart > cancelStart
    && jumpStart > finishStart,
);
assert.match(
  workspace.slice(scheduleStart, cancelStart),
  /explicitJump === true[\s\S]*jumpToEndPending = true[\s\S]*scrollToEndPending = true[\s\S]*placeTranscriptAtEnd\(\)/,
  "Explicit bottom jumps must enter deterministic end placement without any generation-time follow state.",
);
assert.ok(
  captureStart >= 0
    && restoreHistoryStart > captureStart
    && clearHistoryStart > restoreHistoryStart,
);
assert.doesNotMatch(
  workspace.slice(captureStart, restoreHistoryStart),
  /cancelFlick\(\)/,
  "History anchoring must never stop an active user flick.",
);
assert.match(
  workspace.slice(restoreHistoryStart, clearHistoryStart),
  /if \(anchorPlaced\)[\s\S]*clearHistoryAnchor\(\)[\s\S]*return/,
  "A successful history-anchor restoration must finish after one placement instead of bouncing through repeated passes.",
);
assert.match(
  workspace,
  /id:\s*historyPrefetchTimer[\s\S]*interval:\s*280/,
  "History prefetch must wait until manual scrolling has clearly settled.",
);

assert.match(message, /ChatViewportPolicy\.contentZone/);
assert.doesNotMatch(
  workspace,
  /section\.property/,
  "ListView sections resize rows already on screen when history loads; dividers belong to the row before a pause.",
);
assert.match(
  message,
  /height:\s*Math\.max\(1, Math\.ceil\(frame\.height\)\)[\s\S]*breakAfter\.length > 0/,
  "A time divider must be part of its row's height from the first frame.",
);
assert.doesNotMatch(
  message,
  /zoneScale|zoneOpacity/,
  "Rows must not scale or fade with their position in the viewport.",
);
const frameStart = message.indexOf("id: frame");
const columnStart = message.indexOf("id: messageColumn", frameStart);
assert.ok(frameStart >= 0 && columnStart > frameStart);
assert.match(
  message.slice(frameStart, columnStart),
  /Behavior on x \{\s*enabled: !root\.recycling/,
  "Messages may spring sideways on resize, but never while a recycled row takes a new message.",
);
// A Column lays out at polish time, after the ListView has placed a recycled
// row, so the row would resize after placement and push the rows below it.
assert.doesNotMatch(
  message,
  /Column \{\s*id: messageColumn/,
  "A message row must stack its parts with bindings, not a Column.",
);
assert.doesNotMatch(
  message,
  /layoutSuspended: root\./,
  "Message text must lay out while a recycled row takes its message, not after onReused.",
);
const richContent = read(path.join(path.dirname(messagePath), "RichMessageContent.qml"));
assert.doesNotMatch(
  richContent,
  /Column \{\s*id: contentColumn/,
  "Message blocks must stack with bindings, not a Column.",
);
assert.match(
  richContent,
  /root\.syncing = true[\s\S]*root\.applyBlocks[\s\S]*root\.syncing = false/,
  "A block rebind must lay its text out once, when every property has arrived.",
);
const activityStart = message.indexOf("id: activityCard");
const surfaceStart = message.indexOf("id: surfaceFrame", activityStart);
assert.ok(activityStart >= 0 && surfaceStart > activityStart);
assert.doesNotMatch(
  message.slice(activityStart, surfaceStart),
  /Behavior on height/,
  "The waiting card must not animate the ListView delegate height.",
);

assert.doesNotMatch(
  activityRow,
  /Qt\.callLater\([\s\S]*root\.reveal/,
  "Destroyed or recycled activity delegates must not retain delayed reveal callbacks.",
);
assert.match(
  cmake,
  /qml\/App\/Workbench\/Workspace\/ChatViewportPolicy\.js/,
  "The QML policy module must be packaged into the native application.",
);

console.log("Chat workspace stability test: PASS");
console.log("  full-window centering with obstruction collision avoidance");
console.log("  manual-scroll ownership with immediate animation cancellation");
console.log("  near-top idle history prefetch and one-pass anchor restoration");
console.log("  fast bounded jump-to-latest with exact final settlement");
console.log("  one-time send launch and recycled code-fold state");
console.log("  one shared transcript contentY animation");
console.log("  recycled transcript delegates and stable activity-card height");
