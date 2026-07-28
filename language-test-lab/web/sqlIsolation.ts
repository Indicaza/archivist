export const queryText = `
  SELECT id, display_name
  FROM users
  WHERE active = true
`;

export function normalizeQuery(value: string): string {
  return value.trim().replace(/\s+/g, " ");
}
