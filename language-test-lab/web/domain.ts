export interface User {
  id: string;
  displayName: string;
  active: boolean;
}

export const sampleUser: User = {
  id: "user-1",
  displayName: "Mnemosyne",
  active: true,
};
