import type { User } from "./domain.js";
import { sampleUser } from "./domain.js";

export function describeUser(user: User): string {
  return `${user.displayName} (${user.active ? "active" : "inactive"})`;
}

export async function loadUsers(): Promise<User[]> {
  return [sampleUser];
}
