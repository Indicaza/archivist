import { loadUsers } from "./userService.js";

export async function Dashboard() {
  const users = await loadUsers();

  return (
    <main className="dashboard">
      <h1>{users[0]?.displayName ?? "No users"}</h1>
    </main>
  );
}
