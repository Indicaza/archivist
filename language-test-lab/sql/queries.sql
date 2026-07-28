SELECT
  users.id,
  users.display_name
FROM users
WHERE users.active = TRUE
ORDER BY users.display_name;

UPDATE users
SET active = FALSE
WHERE id = 'user-1';
