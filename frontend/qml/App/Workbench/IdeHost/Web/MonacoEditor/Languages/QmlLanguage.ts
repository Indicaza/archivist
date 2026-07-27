import type * as Monaco from "monaco-editor/editor/editor.api";

type MonacoApi = typeof Monaco;

const qmlKeywords = [
  "alias",
  "as",
  "component",
  "default",
  "enum",
  "function",
  "id",
  "import",
  "on",
  "pragma",
  "property",
  "readonly",
  "required",
  "signal",
];

const javascriptKeywords = [
  "async",
  "await",
  "break",
  "case",
  "catch",
  "class",
  "const",
  "continue",
  "debugger",
  "default",
  "delete",
  "do",
  "else",
  "export",
  "extends",
  "false",
  "finally",
  "for",
  "from",
  "function",
  "get",
  "if",
  "import",
  "in",
  "instanceof",
  "let",
  "new",
  "null",
  "of",
  "return",
  "set",
  "static",
  "super",
  "switch",
  "this",
  "throw",
  "true",
  "try",
  "typeof",
  "undefined",
  "var",
  "void",
  "while",
  "with",
  "yield",
];

export function registerQmlLanguage(
  monaco: MonacoApi,
): void {
  if (
    monaco.languages
      .getLanguages()
      .some((language) => language.id === "qml")
  ) {
    return;
  }

  monaco.languages.register({
    id: "qml",
    aliases: ["QML", "Qt QML"],
    extensions: [".qml"],
    mimetypes: ["text/x-qml"],
  });

  monaco.languages.setLanguageConfiguration("qml", {
    comments: {
      lineComment: "//",
      blockComment: ["/*", "*/"],
    },
    brackets: [
      ["{", "}"],
      ["[", "]"],
      ["(", ")"],
    ],
    autoClosingPairs: [
      {
        open: "{",
        close: "}",
      },
      {
        open: "[",
        close: "]",
      },
      {
        open: "(",
        close: ")",
      },
      {
        open: '"',
        close: '"',
        notIn: ["string", "comment"],
      },
      {
        open: "'",
        close: "'",
        notIn: ["string", "comment"],
      },
    ],
    surroundingPairs: [
      {
        open: "{",
        close: "}",
      },
      {
        open: "[",
        close: "]",
      },
      {
        open: "(",
        close: ")",
      },
      {
        open: '"',
        close: '"',
      },
      {
        open: "'",
        close: "'",
      },
    ],
    folding: {
      markers: {
        start: /^\s*\/\/\s*#?region\b/,
        end: /^\s*\/\/\s*#?endregion\b/,
      },
    },
    indentationRules: {
      increaseIndentPattern:
        /^((?!\/\/).)*(\{[^}"']*|\([^)"']*|\[[^\]"']*)$/,
      decreaseIndentPattern:
        /^\s*(\}|\)|\])\s*[,;]?\s*$/,
    },
  });

  monaco.languages.setMonarchTokensProvider("qml", {
    defaultToken: "",
    tokenPostfix: ".qml",
    qmlKeywords,
    javascriptKeywords,
    operators: [
      "=",
      ">",
      "<",
      "!",
      "~",
      "?",
      ":",
      "==",
      "<=",
      ">=",
      "!=",
      "&&",
      "||",
      "++",
      "--",
      "+",
      "-",
      "*",
      "/",
      "&",
      "|",
      "^",
      "%",
      "<<",
      ">>",
      ">>>",
      "+=",
      "-=",
      "*=",
      "/=",
      "&=",
      "|=",
      "^=",
      "%=",
      "<<=",
      ">>=",
      ">>>=",
      "=>",
    ],
    symbols: /[=><!~?:&|+\-*\/\^%]+/,
    escapes:
      /\\(?:[abfnrtv\\"'0-9]|x[0-9A-Fa-f]{2}|u\{[0-9A-Fa-f]+\}|u[0-9A-Fa-f]{4})/,
    tokenizer: {
      root: [
        [
          /^\s*(import|pragma)\b/,
          "keyword.directive",
        ],
        [
          /\b(on[A-Z][A-Za-z0-9_]*)\b/,
          "keyword",
        ],
        [
          /\b[A-Z][A-Za-z0-9_]*(?=\s*\{)/,
          "type.identifier",
        ],
        [
          /\b[A-Za-z_][A-Za-z0-9_]*(?=\s*:)/,
          "variable.other.property",
        ],
        [
          /[A-Za-z_$][\w$]*/,
          {
            cases: {
              "@qmlKeywords": "keyword",
              "@javascriptKeywords": "keyword",
              "@default": "identifier",
            },
          },
        ],
        [
          /\b[A-Z][A-Za-z0-9_]*\b/,
          "type.identifier",
        ],
        {
          include: "@whitespace",
        },
        [
          /[{}()\[\]]/,
          "@brackets",
        ],
        [
          /@symbols/,
          {
            cases: {
              "@operators": "operator",
              "@default": "",
            },
          },
        ],
        [
          /\d*\.\d+([eE][\-+]?\d+)?/,
          "number.float",
        ],
        [
          /0[xX][0-9a-fA-F]+/,
          "number.hex",
        ],
        [
          /\d+/,
          "number",
        ],
        [
          /[;,.]/,
          "delimiter",
        ],
        [
          /"/,
          {
            token: "string.quote",
            bracket: "@open",
            next: "@stringDouble",
          },
        ],
        [
          /'/,
          {
            token: "string.quote",
            bracket: "@open",
            next: "@stringSingle",
          },
        ],
      ],
      whitespace: [
        [
          /[ \t\r\n]+/,
          "white",
        ],
        [
          /\/\*/,
          "comment",
          "@comment",
        ],
        [
          /\/\/.*$/,
          "comment",
        ],
      ],
      comment: [
        [
          /[^/*]+/,
          "comment",
        ],
        [
          /\*\//,
          "comment",
          "@pop",
        ],
        [
          /[/*]/,
          "comment",
        ],
      ],
      stringDouble: [
        [
          /[^\\"]+/,
          "string",
        ],
        [
          /@escapes/,
          "string.escape",
        ],
        [
          /\\./,
          "string.escape.invalid",
        ],
        [
          /"/,
          {
            token: "string.quote",
            bracket: "@close",
            next: "@pop",
          },
        ],
      ],
      stringSingle: [
        [
          /[^\\']+/,
          "string",
        ],
        [
          /@escapes/,
          "string.escape",
        ],
        [
          /\\./,
          "string.escape.invalid",
        ],
        [
          /'/,
          {
            token: "string.quote",
            bracket: "@close",
            next: "@pop",
          },
        ],
      ],
    },
  });
}
