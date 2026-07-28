# Archivist Language Test Lab

Add this folder as an Archivist Library, then open the files below from the
Library tree. The examples are intentionally tiny so completion, hover,
diagnostics, references, and Go to Definition are easy to verify.

## Fast smoke test

1. Open `web/userService.ts`.
2. Right-click `User` and choose **Go to Definition**.
3. Type `user.` inside `describeUser` and check completion.
4. Open `web/Dashboard.tsx` and jump to `loadUsers`.
5. Open `web/LegacyDashboard.jsx` and confirm JSX parses without errors.
6. Open `python/service.py` and jump to `User`.
7. Open `cpp/src/main.cpp` and jump to `makeGreeting`.
8. Open `rust/src/main.rs` and jump to `greeting`.
9. Open `sql/queries.sql` and confirm SQL completions appear.
10. Open `intentional-errors/` files and confirm diagnostics appear.

The backend log should show a session request and connection for each opened
language. Missing optional servers should leave syntax highlighting and normal
editing available instead of breaking the editor.

## Important SQL isolation check

`sql/queries.sql` should use SQL highlighting and SQL completion.

`web/sqlIsolation.ts` contains a SQL-looking string, but the entire file must
remain TypeScript. Archivist does not attach SQL providers to TypeScript models.

## Included languages

- TypeScript, TSX, HTML, CSS, SCSS, JSON, and YAML
- Python
- C and C++
- Rust
- Go
- Bash
- SQL
- Markdown
- QML

The files under `intentional-errors/` are supposed to be invalid. SQL is
currently tested for isolated syntax highlighting and completion rather than
server diagnostics. All other files should remain valid enough for navigation
and completion testing.
