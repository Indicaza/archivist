import type * as Monaco from "monaco-editor/editor/editor.api";

type MonacoApi = typeof Monaco;

interface SqlCompletionDefinition {
  label: string;
  detail: string;
  kind: "keyword" | "function" | "type";
  insertText?: string;
}

const sqlCompletions: readonly SqlCompletionDefinition[] = [
  { label: "SELECT", detail: "Query rows", kind: "keyword" },
  { label: "FROM", detail: "Select a source table", kind: "keyword" },
  { label: "WHERE", detail: "Filter rows", kind: "keyword" },
  { label: "INSERT INTO", detail: "Insert rows", kind: "keyword" },
  { label: "VALUES", detail: "Provide inserted values", kind: "keyword" },
  { label: "UPDATE", detail: "Update rows", kind: "keyword" },
  { label: "SET", detail: "Assign updated values", kind: "keyword" },
  { label: "DELETE FROM", detail: "Delete rows", kind: "keyword" },
  { label: "CREATE TABLE", detail: "Create a table", kind: "keyword" },
  { label: "ALTER TABLE", detail: "Alter a table", kind: "keyword" },
  { label: "DROP TABLE", detail: "Drop a table", kind: "keyword" },
  { label: "JOIN", detail: "Join another source", kind: "keyword" },
  { label: "LEFT JOIN", detail: "Left outer join", kind: "keyword" },
  { label: "RIGHT JOIN", detail: "Right outer join", kind: "keyword" },
  { label: "INNER JOIN", detail: "Inner join", kind: "keyword" },
  { label: "ON", detail: "Join condition", kind: "keyword" },
  { label: "GROUP BY", detail: "Group result rows", kind: "keyword" },
  { label: "ORDER BY", detail: "Sort result rows", kind: "keyword" },
  { label: "HAVING", detail: "Filter grouped rows", kind: "keyword" },
  { label: "LIMIT", detail: "Limit returned rows", kind: "keyword" },
  { label: "OFFSET", detail: "Skip returned rows", kind: "keyword" },
  { label: "RETURNING", detail: "Return mutated rows", kind: "keyword" },
  { label: "WITH", detail: "Common table expression", kind: "keyword" },
  { label: "AS", detail: "Alias a value or source", kind: "keyword" },
  { label: "DISTINCT", detail: "Remove duplicate rows", kind: "keyword" },
  { label: "UNION", detail: "Combine result sets", kind: "keyword" },
  { label: "CASE", detail: "Conditional expression", kind: "keyword" },
  { label: "WHEN", detail: "Conditional branch", kind: "keyword" },
  { label: "THEN", detail: "Conditional result", kind: "keyword" },
  { label: "ELSE", detail: "Fallback result", kind: "keyword" },
  { label: "END", detail: "End a conditional expression", kind: "keyword" },
  { label: "AND", detail: "Boolean conjunction", kind: "keyword" },
  { label: "OR", detail: "Boolean disjunction", kind: "keyword" },
  { label: "NOT", detail: "Boolean negation", kind: "keyword" },
  { label: "NULL", detail: "Null value", kind: "keyword" },
  { label: "IS NULL", detail: "Test for null", kind: "keyword" },
  { label: "IS NOT NULL", detail: "Test for non-null", kind: "keyword" },
  { label: "IN", detail: "Set membership", kind: "keyword" },
  { label: "EXISTS", detail: "Test a subquery", kind: "keyword" },
  {
    label: "COUNT",
    detail: "Count rows or values",
    kind: "function",
    insertText: "COUNT(${1:*})",
  },
  {
    label: "SUM",
    detail: "Sum numeric values",
    kind: "function",
    insertText: "SUM(${1:column})",
  },
  {
    label: "AVG",
    detail: "Average numeric values",
    kind: "function",
    insertText: "AVG(${1:column})",
  },
  {
    label: "MIN",
    detail: "Minimum value",
    kind: "function",
    insertText: "MIN(${1:column})",
  },
  {
    label: "MAX",
    detail: "Maximum value",
    kind: "function",
    insertText: "MAX(${1:column})",
  },
  {
    label: "COALESCE",
    detail: "First non-null value",
    kind: "function",
    insertText: "COALESCE(${1:value}, ${2:fallback})",
  },
  {
    label: "NULLIF",
    detail: "Return null when values match",
    kind: "function",
    insertText: "NULLIF(${1:left}, ${2:right})",
  },
  {
    label: "LOWER",
    detail: "Lowercase text",
    kind: "function",
    insertText: "LOWER(${1:value})",
  },
  {
    label: "UPPER",
    detail: "Uppercase text",
    kind: "function",
    insertText: "UPPER(${1:value})",
  },
  {
    label: "LENGTH",
    detail: "Value length",
    kind: "function",
    insertText: "LENGTH(${1:value})",
  },
  {
    label: "CURRENT_TIMESTAMP",
    detail: "Current timestamp",
    kind: "function",
  },
  { label: "INTEGER", detail: "Integer type", kind: "type" },
  { label: "BIGINT", detail: "Large integer type", kind: "type" },
  { label: "REAL", detail: "Floating-point type", kind: "type" },
  { label: "NUMERIC", detail: "Exact numeric type", kind: "type" },
  { label: "DECIMAL", detail: "Exact decimal type", kind: "type" },
  { label: "TEXT", detail: "Text type", kind: "type" },
  { label: "VARCHAR", detail: "Variable-length text type", kind: "type" },
  { label: "BOOLEAN", detail: "Boolean type", kind: "type" },
  { label: "DATE", detail: "Date type", kind: "type" },
  { label: "TIMESTAMP", detail: "Timestamp type", kind: "type" },
  { label: "JSON", detail: "JSON type", kind: "type" },
  { label: "JSONB", detail: "Binary JSON type", kind: "type" },
  { label: "UUID", detail: "UUID type", kind: "type" },
  { label: "BLOB", detail: "Binary large object type", kind: "type" },
];

function completionKind(
  monaco: MonacoApi,
  kind: SqlCompletionDefinition["kind"],
): Monaco.languages.CompletionItemKind {
  switch (kind) {
    case "function":
      return monaco.languages.CompletionItemKind.Function;
    case "type":
      return monaco.languages.CompletionItemKind.Struct;
    default:
      return monaco.languages.CompletionItemKind.Keyword;
  }
}

export function registerSqlLanguageSupport(
  monaco: MonacoApi,
): Monaco.IDisposable {
  return monaco.languages.registerCompletionItemProvider(
    "sql",
    {
      triggerCharacters: [" ", ".", ",", "("],
      provideCompletionItems(model, position) {
        const word = model.getWordUntilPosition(position);
        const range = new monaco.Range(
          position.lineNumber,
          word.startColumn,
          position.lineNumber,
          word.endColumn,
        );

        return {
          suggestions: sqlCompletions.map((completion) => ({
            label: completion.label,
            detail: completion.detail,
            kind: completionKind(monaco, completion.kind),
            insertText: completion.insertText ?? completion.label,
            insertTextRules: completion.insertText
              ? monaco.languages.CompletionItemInsertTextRule.InsertAsSnippet
              : undefined,
            range,
          })),
        };
      },
    },
  );
}
