export type CollectionItemType = "library" | "chat";

export type Collection = {
  id: string;
  parentCollectionId: string | null;
  name: string;
  path: string;
  position: number;
  libraryIds: string[];
  chatIds: string[];
  agentIds: string[];
  defaultAgentId: string | null;
  archivedAt: string | null;
  createdAt: string;
  updatedAt: string;
};

export type CollectionScope = {
  collectionIds: string[];
  libraryIds: string[];
  directChatIds: string[];
  chatIds: string[];
  agentIds: string[];
  defaultAgentId: string | null;
};

export type CreateCollectionInput = {
  name: string;
  parentCollectionId?: string | null;
  position?: number;
  libraryIds?: string[];
  chatIds?: string[];
  agentIds?: string[];
  defaultAgentId?: string | null;
};

export type UpdateCollectionInput = {
  name?: string;
  parentCollectionId?: string | null;
  position?: number;
  libraryIds?: string[];
  chatIds?: string[];
  agentIds?: string[];
  defaultAgentId?: string | null;
};

export type ArchiveCollectionResult = {
  collection: Collection;
  selectedCollectionId: string | null;
};
