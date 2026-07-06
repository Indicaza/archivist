const major = Number(process.versions.node.split(".")[0]);

if (major !== 24) {
  console.error(`Archivist requires Node 24. Current Node version is ${process.version}.`);
  console.error("Run: nvm use");
  process.exit(1);
}
