// frontend/src/domains/library/library.types.ts
export type LibraryStatus = "active" | "draft" | "offline";

export type LibraryListItem = {
  id: string;
  name: string;
  subtitle: string;
  status: LibraryStatus;
};
