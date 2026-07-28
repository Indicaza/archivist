CREATE TABLE users (
  id TEXT PRIMARY KEY,
  display_name TEXT NOT NULL,
  active BOOLEAN NOT NULL DEFAULT TRUE
);

CREATE INDEX users_active_idx
  ON users (active);
