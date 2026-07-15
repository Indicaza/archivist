export type Library = {
  id: string;
  name: string;
  description: string | null;
  rootPath: string;
  archivedAt: string | null;
  createdAt: string;
  updatedAt: string;
};

export type LibraryStatus = "active" | "draft" | "offline";

export type LibraryListItem = Library & {
  subtitle: string;
  status: LibraryStatus;
};

export type AppState = {
  selectedLibraryId: string | null;
};
