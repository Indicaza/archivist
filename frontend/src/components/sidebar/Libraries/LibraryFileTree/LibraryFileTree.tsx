import { useEffect, useMemo, useState } from "react";
import {
  AlertTriangle,
  ChevronRight,
  FileCode2,
  FileText,
  Folder,
  FolderOpen,
  FolderTree,
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
    return <FileText size={14} strokeWidth={1.9} />;
  }

  return <FileCode2 size={14} strokeWidth={1.9} />;
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
                paddingLeft: `${8 + depth * 14}px`,
              }}
              type="button"
              onClick={() => onToggleFolder(directory.path)}
              title={directory.path}
            >
              <ChevronRight
                size={13}
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
              paddingLeft: `${27 + depth * 14}px`,
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
                size={12}
                strokeWidth={2}
              />
            ) : null}
          </div>
        );
      })}
    </>
  );
}

export function LibraryFileTree({ library }: LibraryFileTreeProps) {
  const [open, setOpen] = useState(true);
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

  const tree = useMemo(() => {
    return buildFileTree(files);
  }, [files]);

  const missingFileCount = useMemo(() => {
    return files.filter((file) => file.status === "missing").length;
  }, [files]);

  const unreadableFileCount = useMemo(() => {
    return files.filter((file) => file.status === "unreadable").length;
  }, [files]);

  function toggleFolder(folderPath: string) {
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
      <button
        className={styles.header}
        type="button"
        onClick={() => setOpen((value) => !value)}
      >
        <ChevronRight
          size={16}
          strokeWidth={2.25}
          className={`${styles.headerCaret} ${
            open ? styles.headerCaretOpen : ""
          }`}
        />

        <FolderTree size={16} strokeWidth={2.1} />

        <span className={styles.headerTitle}>Library Files</span>

        {latestScan ? (
          <span className={styles.fileCount}>{files.length}</span>
        ) : null}
      </button>

      <div
        className={`${styles.content} ${
          open ? styles.contentOpen : styles.contentClosed
        }`}
      >
        <div className={styles.inner}>
          <div className={styles.libraryName}>{library.name}</div>

          <button
            className={styles.scanButton}
            type="button"
            disabled={loading || scanning}
            onClick={handleScan}
          >
            <RefreshCw
              size={14}
              strokeWidth={2.1}
              className={scanning ? styles.spinning : ""}
            />

            {scanning
              ? "Scanning..."
              : latestScan
                ? "Rescan Library"
                : "Scan Library"}
          </button>

          {loading ? (
            <div className={styles.empty}>Loading file catalog...</div>
          ) : null}

          {!loading && error ? (
            <div className={styles.error}>
              <AlertTriangle size={14} strokeWidth={2.1} />
              <span>{error}</span>
            </div>
          ) : null}

          {!loading && !error && latestScan ? (
            <div className={styles.scanMeta}>
              <span>
                {latestScan.status === "complete"
                  ? "Scan complete"
                  : latestScan.status}
              </span>

              <span>{formatScanTime(latestScanTime)}</span>

              {missingFileCount > 0 ? (
                <span>{missingFileCount} missing</span>
              ) : null}

              {unreadableFileCount > 0 ? (
                <span>{unreadableFileCount} unreadable</span>
              ) : null}
            </div>
          ) : null}

          {!loading && !error && !latestScan ? (
            <div className={styles.empty}>
              This Library has not been scanned yet.
            </div>
          ) : null}

          {!loading && !error && latestScan && files.length === 0 ? (
            <div className={styles.empty}>No supported files were found.</div>
          ) : null}

          {!loading && !error && files.length > 0 ? (
            <div className={styles.treeViewport}>
              <TreeBranch
                node={tree}
                depth={0}
                openFolders={openFolders}
                onToggleFolder={toggleFolder}
              />
            </div>
          ) : null}

          {issues.length > 0 ? (
            <div className={styles.issueSummary}>
              {issues.length} scan issue{issues.length === 1 ? "" : "s"}
            </div>
          ) : null}
        </div>
      </div>
    </section>
  );
}
