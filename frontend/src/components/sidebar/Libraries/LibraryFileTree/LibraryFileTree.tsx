import { useEffect, useMemo, useState } from "react";
import {
  AlertTriangle,
  ChevronRight,
  FileCode2,
  FileText,
  Folder,
  FolderOpen,
  RefreshCw,
} from "lucide-react";
import {
  fetchLibraryFiles,
  scanLibraryFiles,
} from "../../../../domains/library/library.api";
import type {
  LibraryFile,
  LibraryFileCatalog,
  LibraryListItem,
  LibraryScanIssue,
} from "../../../../domains/library/library.types";
import styles from "./LibraryFileTree.module.css";

type LibraryFileTreeProps = {
  library: LibraryListItem;
  searchQuery?: string;
};

type DirectoryNode = {
  name: string;
  path: string;
  directories: Map<string, DirectoryNode>;
  files: LibraryFile[];
};

type TreeBranchProps = {
  node: DirectoryNode;
  depth: number;
  openFolders: Set<string>;
  onToggleFolder: (folderPath: string) => void;
};

function buildFileTree(files: LibraryFile[]): DirectoryNode {
  const root: DirectoryNode = {
    name: "",
    path: "",
    directories: new Map(),
    files: [],
  };

  for (const file of files) {
    const segments = file.relativePath.split("/").filter(Boolean);
    segments.pop();

    let currentNode = root;

    for (const segment of segments) {
      const directoryPath = currentNode.path
        ? `${currentNode.path}/${segment}`
        : segment;

      let childNode = currentNode.directories.get(segment);

      if (!childNode) {
        childNode = {
          name: segment,
          path: directoryPath,
          directories: new Map(),
          files: [],
        };

        currentNode.directories.set(segment, childNode);
      }

      currentNode = childNode;
    }

    currentNode.files.push(file);
  }

  return root;
}

function getInitialOpenFolders(files: LibraryFile[]): Set<string> {
  const folders = new Set<string>();

  for (const file of files) {
    const [topLevelFolder] = file.relativePath.split("/");

    if (file.relativePath.includes("/") && topLevelFolder) {
      folders.add(topLevelFolder);
    }
  }

  return folders;
}

function getParentFolders(files: LibraryFile[]): Set<string> {
  const folders = new Set<string>();

  for (const file of files) {
    const segments = file.relativePath.split("/").filter(Boolean);
    segments.pop();

    let currentPath = "";

    for (const segment of segments) {
      currentPath = currentPath ? `${currentPath}/${segment}` : segment;
      folders.add(currentPath);
    }
  }

  return folders;
}

function formatScanTime(value: string | null): string {
  if (!value) {
    return "Unknown time";
  }

  const date = new Date(value);

  if (Number.isNaN(date.getTime())) {
    return value;
  }

  return date.toLocaleString([], {
    month: "short",
    day: "numeric",
    hour: "numeric",
    minute: "2-digit",
  });
}

function formatFileSize(sizeBytes: number): string {
  if (sizeBytes < 1024) {
    return `${sizeBytes} B`;
  }

  if (sizeBytes < 1024 * 1024) {
    return `${(sizeBytes / 1024).toFixed(1)} KB`;
  }

  return `${(sizeBytes / (1024 * 1024)).toFixed(1)} MB`;
}

function FileIcon({ extension }: { extension: string }) {
  if (extension === ".md" || extension === ".txt") {
    return <FileText size={13} strokeWidth={1.9} />;
  }

  return <FileCode2 size={13} strokeWidth={1.9} />;
}

function TreeBranch({
  node,
  depth,
  openFolders,
  onToggleFolder,
}: TreeBranchProps) {
  const directories = [...node.directories.values()].sort((left, right) =>
    left.name.localeCompare(right.name),
  );

  const files = [...node.files].sort((left, right) =>
    left.name.localeCompare(right.name),
  );

  return (
    <>
      {directories.map((directory) => {
        const open = openFolders.has(directory.path);

        return (
          <div key={directory.path}>
            <button
              className={styles.treeRow}
              style={{
                paddingLeft: `${5 + depth * 13}px`,
              }}
              type="button"
              onClick={() => onToggleFolder(directory.path)}
              title={directory.path}
            >
              <ChevronRight
                size={12}
                strokeWidth={2.2}
                className={`${styles.treeCaret} ${
                  open ? styles.treeCaretOpen : ""
                }`}
              />

              {open ? (
                <FolderOpen size={14} strokeWidth={1.9} />
              ) : (
                <Folder size={14} strokeWidth={1.9} />
              )}

              <span className={styles.treeName}>{directory.name}</span>
            </button>

            {open ? (
              <TreeBranch
                node={directory}
                depth={depth + 1}
                openFolders={openFolders}
                onToggleFolder={onToggleFolder}
              />
            ) : null}
          </div>
        );
      })}

      {files.map((file) => {
        const statusClassName =
          file.status === "missing"
            ? styles.missing
            : file.status === "unreadable"
              ? styles.unreadable
              : "";

        return (
          <div
            key={file.id}
            className={`${styles.fileRow} ${statusClassName}`}
            style={{
              paddingLeft: `${24 + depth * 13}px`,
            }}
            title={`${file.relativePath} • ${file.status} • ${formatFileSize(
              file.sizeBytes,
            )}`}
          >
            <FileIcon extension={file.extension} />

            <span className={styles.treeName}>{file.name}</span>

            {file.status !== "available" ? (
              <AlertTriangle
                className={styles.fileWarning}
                size={11}
                strokeWidth={2}
              />
            ) : null}
          </div>
        );
      })}
    </>
  );
}

export function LibraryFileTree({
  library,
  searchQuery = "",
}: LibraryFileTreeProps) {
  const [catalog, setCatalog] = useState<LibraryFileCatalog | null>(null);
  const [openFolders, setOpenFolders] = useState<Set<string>>(new Set());
  const [issues, setIssues] = useState<LibraryScanIssue[]>([]);
  const [loading, setLoading] = useState(true);
  const [scanning, setScanning] = useState(false);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    let cancelled = false;

    void fetchLibraryFiles(library.id)
      .then((result) => {
        if (cancelled) {
          return;
        }

        setCatalog(result);
        setOpenFolders(getInitialOpenFolders(result.files));
      })
      .catch((loadError: unknown) => {
        if (cancelled) {
          return;
        }

        setError(
          loadError instanceof Error
            ? loadError.message
            : "Archivist could not load the file catalog.",
        );
      })
      .finally(() => {
        if (!cancelled) {
          setLoading(false);
        }
      });

    return () => {
      cancelled = true;
    };
  }, [library.id]);

  const files = useMemo(() => catalog?.files ?? [], [catalog?.files]);
  const normalizedSearchQuery = searchQuery.trim().toLowerCase();

  const matchingFiles = useMemo(() => {
    if (!normalizedSearchQuery) {
      return files;
    }

    return files.filter((file) => {
      return `${file.name} ${file.relativePath} ${file.extension}`
        .toLowerCase()
        .includes(normalizedSearchQuery);
    });
  }, [files, normalizedSearchQuery]);

  const tree = useMemo(() => {
    return buildFileTree(matchingFiles);
  }, [matchingFiles]);

  const searchOpenFolders = useMemo(() => {
    return normalizedSearchQuery ? getParentFolders(matchingFiles) : null;
  }, [matchingFiles, normalizedSearchQuery]);

  const visibleOpenFolders = searchOpenFolders ?? openFolders;

  const missingFileCount = useMemo(() => {
    return files.filter((file) => file.status === "missing").length;
  }, [files]);

  const unreadableFileCount = useMemo(() => {
    return files.filter((file) => file.status === "unreadable").length;
  }, [files]);

  function toggleFolder(folderPath: string) {
    if (normalizedSearchQuery) {
      return;
    }

    setOpenFolders((current) => {
      const next = new Set(current);

      if (next.has(folderPath)) {
        next.delete(folderPath);
      } else {
        next.add(folderPath);
      }

      return next;
    });
  }

  async function handleScan() {
    if (scanning) {
      return;
    }

    setScanning(true);
    setError(null);
    setIssues([]);

    try {
      const result = await scanLibraryFiles(library.id);

      setCatalog({
        files: result.files,
        latestScan: result.scan,
      });

      setIssues(result.issues);

      setOpenFolders((current) => {
        if (current.size > 0) {
          return current;
        }

        return getInitialOpenFolders(result.files);
      });
    } catch (scanError) {
      setError(
        scanError instanceof Error
          ? scanError.message
          : "Archivist could not scan the Library.",
      );
    } finally {
      setScanning(false);
    }
  }

  const latestScan = catalog?.latestScan ?? null;
  const latestScanTime =
    latestScan?.completedAt ?? latestScan?.startedAt ?? null;

  return (
    <section className={styles.section}>
      <div className={styles.catalogToolbar}>
        <div className={styles.catalogIdentity} title={library.rootPath}>
          <span className={styles.libraryName}>{library.name}</span>
          <span className={styles.fileCount}>{files.length}</span>
        </div>

        <button
          className={styles.scanButton}
          type="button"
          disabled={loading || scanning}
          onClick={handleScan}
          aria-label={latestScan ? "Rescan Library" : "Scan Library"}
          title={latestScan ? "Rescan Library" : "Scan Library"}
        >
          <RefreshCw
            size={14}
            strokeWidth={2.1}
            className={scanning ? styles.spinning : ""}
          />
        </button>
      </div>

      {loading ? (
        <div className={styles.empty}>Loading file catalog...</div>
      ) : error ? (
        <div className={styles.error}>
          <AlertTriangle size={14} strokeWidth={2.1} />
          <span>{error}</span>
        </div>
      ) : !latestScan ? (
        <div className={styles.empty}>
          <span>This Library has not been scanned yet.</span>
          <button type="button" onClick={handleScan} disabled={scanning}>
            {scanning ? "Scanning..." : "Scan Library"}
          </button>
        </div>
      ) : files.length === 0 ? (
        <div className={styles.empty}>No supported files were found.</div>
      ) : (
        <>
          <div className={styles.treeViewport}>
            {matchingFiles.length > 0 ? (
              <TreeBranch
                node={tree}
                depth={0}
                openFolders={visibleOpenFolders}
                onToggleFolder={toggleFolder}
              />
            ) : (
              <div className={styles.noMatches}>
                No file names or paths match “{searchQuery.trim()}”.
              </div>
            )}
          </div>

          {normalizedSearchQuery ? (
            <div className={styles.searchResults}>
              <div className={styles.searchResultsHeader}>
                <span>Catalog Matches</span>
                <span>{matchingFiles.length}</span>
              </div>

              <div className={styles.searchResultsList}>
                {matchingFiles.slice(0, 12).map((file) => (
                  <div
                    key={file.id}
                    className={styles.searchResultRow}
                    title={file.relativePath}
                  >
                    <FileIcon extension={file.extension} />
                    <span>{file.relativePath}</span>
                  </div>
                ))}

                {matchingFiles.length > 12 ? (
                  <div className={styles.moreResults}>
                    +{matchingFiles.length - 12} more matches
                  </div>
                ) : null}
              </div>
            </div>
          ) : null}
        </>
      )}

      {latestScan ? (
        <footer className={styles.statusBar}>
          <span>
            {latestScan.status === "complete" ? "Ready" : latestScan.status}
          </span>
          <span>{formatScanTime(latestScanTime)}</span>
          {missingFileCount > 0 ? (
            <span>{missingFileCount} missing</span>
          ) : null}
          {unreadableFileCount > 0 ? (
            <span>{unreadableFileCount} unreadable</span>
          ) : null}
          {issues.length > 0 ? <span>{issues.length} issues</span> : null}
        </footer>
      ) : null}
    </section>
  );
}
