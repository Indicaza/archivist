// @ts-check

import { loadUsers } from "./userService.js";

export async function LegacyDashboard() {
  const users = await loadUsers();

  return (
    <main className="dashboard">
      <h1>{users[0]?.displayName ?? "No users"}</h1>
      <p>JavaScript JSX uses the same TypeScript language server.</p>
    </main>
  );
}
