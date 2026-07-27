import type * as Monaco from "monaco-editor/editor/editor.api";

type MonacoApi = typeof Monaco;

function registerCmake(monaco: MonacoApi): void {
  if (
    monaco.languages
      .getLanguages()
      .some((language) => language.id === "cmake")
  ) {
    return;
  }

  monaco.languages.register({
    id: "cmake",
    aliases: ["CMake", "cmake"],
    extensions: [".cmake"],
    filenames: ["CMakeLists.txt"],
  });

  monaco.languages.setLanguageConfiguration("cmake", {
    comments: {
      lineComment: "#",
      blockComment: ["#[[", "]]"],
    },
    brackets: [
      ["(", ")"],
    ],
    autoClosingPairs: [
      {
        open: "(",
        close: ")",
      },
      {
        open: '"',
        close: '"',
      },
    ],
  });

  monaco.languages.setMonarchTokensProvider("cmake", {
    defaultToken: "",
    tokenPostfix: ".cmake",
    keywords: [
      "break",
      "continue",
      "else",
      "elseif",
      "endforeach",
      "endfunction",
      "endif",
      "endmacro",
      "endwhile",
      "foreach",
      "function",
      "if",
      "macro",
      "return",
      "while",
    ],
    tokenizer: {
      root: [
        [
          /[A-Za-z_][A-Za-z0-9_]*(?=\s*\()/,
          {
            cases: {
              "@keywords": "keyword",
              "@default": "function",
            },
          },
        ],
        [
          /\$\{[A-Za-z_][A-Za-z0-9_]*\}/,
          "variable",
        ],
        [
          /\$ENV\{[A-Za-z_][A-Za-z0-9_]*\}/,
          "variable",
        ],
        [
          /#[^\[].*$/,
          "comment",
        ],
        [
          /#\[\[/,
          "comment",
          "@comment",
        ],
        [
          /"/,
          "string",
          "@string",
        ],
        [
          /\b(ON|OFF|TRUE|FALSE|YES|NO)\b/i,
          "constant",
        ],
        [
          /\b\d+(\.\d+)*\b/,
          "number",
        ],
        [
          /[()]/,
          "@brackets",
        ],
      ],
      comment: [
        [
          /[^\]]+/,
          "comment",
        ],
        [
          /\]\]/,
          "comment",
          "@pop",
        ],
        [
          /[\]]/,
          "comment",
        ],
      ],
      string: [
        [
          /[^\\"]+/,
          "string",
        ],
        [
          /\\./,
          "string.escape",
        ],
        [
          /"/,
          "string",
          "@pop",
        ],
      ],
    },
  });
}

function registerMakefile(monaco: MonacoApi): void {
  if (
    monaco.languages
      .getLanguages()
      .some((language) => language.id === "makefile")
  ) {
    return;
  }

  monaco.languages.register({
    id: "makefile",
    aliases: ["Makefile", "make"],
    filenames: ["Makefile", "makefile", "GNUmakefile"],
  });

  monaco.languages.setLanguageConfiguration("makefile", {
    comments: {
      lineComment: "#",
    },
    brackets: [
      ["(", ")"],
      ["{", "}"],
    ],
  });

  monaco.languages.setMonarchTokensProvider("makefile", {
    defaultToken: "",
    tokenPostfix: ".makefile",
    tokenizer: {
      root: [
        [
          /^\s*#.*$/,
          "comment",
        ],
        [
          /^[A-Za-z0-9_.%\/-]+(?=\s*:)/,
          "type.identifier",
        ],
        [
          /^[A-Za-z_][A-Za-z0-9_]*(?=\s*[:+?]?=)/,
          "variable",
        ],
        [
          /\$\([^)]+\)|\$\{[^}]+\}/,
          "variable",
        ],
        [
          /^\t.*$/,
          "string",
        ],
        [
          /\b(include|define|endef|ifdef|ifndef|ifeq|ifneq|else|endif|export|override|private|unexport|vpath)\b/,
          "keyword",
        ],
      ],
    },
  });
}

export function registerBuildLanguages(
  monaco: MonacoApi,
): void {
  registerCmake(monaco);
  registerMakefile(monaco);
}
