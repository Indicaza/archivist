export type Library = {
  id: string;
  name: string;
  description: string | null;
  rootPath: string;
  archivedAt: string | null;
  createdAt: string;
  updatedAt: string;
};

export type CreateLibraryInput = {
  rootPath: string;
  name?: string;
  description?: string;
};

export type UpdateLibraryInput = {
  name?: string;
  description?: string | null;
};

export type CreateLibraryResult = {
  library: Library;
  selectedLibraryId: string | null;
};

export type ArchiveLibraryResult = {
  library: Library;
  selectedLibraryId: string | null;
};
