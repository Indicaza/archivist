import path from "node:path";

export function isQmlRuntimeFile(name) {
  if (typeof name !== "string" || !name.length) return false;
  return /\.(qml|js|mjs|svg|json|png|jpg|jpeg|webp|gif|ttf|otf|qsb)$/i.test(name);
}

export function sourceQmlDirectory(root) {
  return path.join(root, "frontend", "qml");
}

export function qtBinaryCandidates(root) {
  const build = path.join(root, "build", "frontend-debug");
  return [
    path.join(build, "archivist-qt.app", "Contents", "MacOS", "archivist-qt"),
    path.join(build, "archivist-qt"),
  ];
}
