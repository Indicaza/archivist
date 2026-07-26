export const supportedLibraryTextExtensions = new Set([
  ".md",
  ".markdown",
  ".txt",
  ".log",
  ".json",
  ".yaml",
  ".yml",
  ".toml",
  ".xml",
  ".ts",
  ".tsx",
  ".js",
  ".jsx",
  ".css",
  ".scss",
  ".sass",
  ".html",
  ".qml",
  ".py",
  ".rs",
  ".go",
  ".c",
  ".cc",
  ".cxx",
  ".cpp",
  ".h",
  ".hpp",
  ".sh",
  ".bash",
  ".zsh",
  ".sql",
]);

export const supportedLibraryTextFileNames = new Set([
  ".editorconfig",
  ".gitattributes",
  ".gitignore",
  "cmakelists.txt",
  "dockerfile",
  "license",
  "makefile",
  "readme",
]);

const dangerousLibraryFileExtensions = new Set([
  ".apk",
  ".appx",
  ".appxbundle",
  ".com",
  ".cpl",
  ".deb",
  ".dll",
  ".dmg",
  ".drv",
  ".dylib",
  ".ear",
  ".exe",
  ".ipa",
  ".iso",
  ".jar",
  ".lnk",
  ".msi",
  ".msp",
  ".msix",
  ".ocx",
  ".pif",
  ".pkg",
  ".rpm",
  ".scr",
  ".so",
  ".sys",
  ".vhd",
  ".vhdx",
  ".war",
]);

const managedPhotoLibraryDirectoryExtensions = new Set([
  ".aplibrary",
  ".migratedphotolibrary",
  ".photolibrary",
  ".photoslibrary",
]);

const excludedLibraryDirectoryExtensions = new Set([
  ".app",
  ".bundle",
  ".captureone",
  ".framework",
  ".kext",
  ".lrcat-data",
  ".lrdata",
  ".plugin",
  ".xpc",
]);

const dangerousLibraryFileNames = new Set(["autorun.inf"]);

export function isCatalogableLibraryFile(fileName: string): boolean {
  const normalizedName = fileName.trim().toLowerCase();
  const extensionIndex = normalizedName.lastIndexOf(".");
  const extension = extensionIndex > 0 ? normalizedName.slice(extensionIndex) : "";

  return (
    normalizedName.length > 0 &&
    !dangerousLibraryFileNames.has(normalizedName) &&
    !dangerousLibraryFileExtensions.has(extension)
  );
}

export function isManagedPhotoLibraryDirectory(directoryName: string): boolean {
  const normalizedName = directoryName.trim().toLowerCase();

  for (const extension of managedPhotoLibraryDirectoryExtensions) {
    if (normalizedName.endsWith(extension)) {
      return true;
    }
  }

  return false;
}

export function isCatalogableLibraryDirectory(directoryName: string): boolean {
  const normalizedName = directoryName.trim().toLowerCase();

  if (isManagedPhotoLibraryDirectory(normalizedName)) {
    return false;
  }

  for (const extension of excludedLibraryDirectoryExtensions) {
    if (normalizedName.endsWith(extension)) {
      return false;
    }
  }

  return normalizedName.length > 0;
}

export function isSupportedLibraryTextFile(
  fileName: string,
  extension: string,
): boolean {
  return (
    supportedLibraryTextExtensions.has(extension.trim().toLowerCase()) ||
    supportedLibraryTextFileNames.has(fileName.trim().toLowerCase())
  );
}

export const maxLibraryTextPreviewBytes = 512 * 1024;
