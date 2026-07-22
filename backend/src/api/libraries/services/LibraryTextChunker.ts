import { createHash } from "node:crypto";
import { estimateTokens } from "../../../core/cognition/conscious/context/utilities/estimateTokens.js";
import type { LibraryTextChunk } from "../types/LibraryTextIndexTypes.js";

const maximumChunkCharacters = 3_200;
const maximumChunkLines = 80;
const sourceOverlapLines = 12;

const proseExtensions = new Set([".md", ".txt"]);

type LineRange = {
  startLine: number;
  endLine: number;
};

function hashText(content: string): string {
  return createHash("sha256").update(content, "utf8").digest("hex");
}

function createChunk(input: {
  libraryId: string;
  libraryFileId: string;
  ordinal: number;
  lines: string[];
  range: LineRange;
}): LibraryTextChunk | null {
  const content = input.lines
    .slice(input.range.startLine - 1, input.range.endLine)
    .join("\n")
    .trim();

  if (!content) {
    return null;
  }

  const contentHash = hashText(content);
  const id = hashText(
    [
      input.libraryFileId,
      input.ordinal,
      input.range.startLine,
      input.range.endLine,
      contentHash,
    ].join(":"),
  );

  return {
    id,
    libraryFileId: input.libraryFileId,
    libraryId: input.libraryId,
    ordinal: input.ordinal,
    startLine: input.range.startLine,
    endLine: input.range.endLine,
    content,
    estimatedTokens: estimateTokens(content),
    contentHash,
  };
}

function createSourceRanges(lines: string[]): LineRange[] {
  const ranges: LineRange[] = [];
  let startIndex = 0;

  while (startIndex < lines.length) {
    let endIndex = startIndex;
    let characterCount = 0;

    while (endIndex < lines.length && endIndex - startIndex < maximumChunkLines) {
      const nextLength = lines[endIndex].length + 1;

      if (
        endIndex > startIndex &&
        characterCount + nextLength > maximumChunkCharacters
      ) {
        break;
      }

      characterCount += nextLength;
      endIndex += 1;
    }

    if (endIndex === startIndex) {
      endIndex += 1;
    }

    ranges.push({
      startLine: startIndex + 1,
      endLine: endIndex,
    });

    if (endIndex >= lines.length) {
      break;
    }

    const nextStartIndex = Math.max(startIndex + 1, endIndex - sourceOverlapLines);
    startIndex = nextStartIndex;
  }

  return ranges;
}

function collectProseBlocks(lines: string[]): LineRange[] {
  const blocks: LineRange[] = [];
  let blockStart: number | null = null;

  const closeBlock = (endIndexExclusive: number): void => {
    if (blockStart === null) {
      return;
    }

    blocks.push({
      startLine: blockStart + 1,
      endLine: endIndexExclusive,
    });
    blockStart = null;
  };

  for (let index = 0; index < lines.length; index += 1) {
    const line = lines[index];

    if (!line.trim()) {
      closeBlock(index);
      continue;
    }

    if (/^#{1,6}\s/.test(line) && blockStart !== null) {
      closeBlock(index);
    }

    if (blockStart === null) {
      blockStart = index;
    }

    if (/^#{1,6}\s/.test(line)) {
      closeBlock(index + 1);
    }
  }

  closeBlock(lines.length);

  return blocks;
}

function createProseRanges(lines: string[]): LineRange[] {
  const blocks = collectProseBlocks(lines);

  if (blocks.length === 0) {
    return [];
  }

  const ranges: LineRange[] = [];
  let currentStart = blocks[0].startLine;
  let currentEnd = blocks[0].endLine;
  let currentLength = lines
    .slice(currentStart - 1, currentEnd)
    .join("\n").length;

  for (const block of blocks.slice(1)) {
    const blockLength = lines
      .slice(block.startLine - 1, block.endLine)
      .join("\n").length;

    if (
      currentLength > 0 &&
      currentLength + blockLength + 2 > maximumChunkCharacters
    ) {
      ranges.push({
        startLine: currentStart,
        endLine: currentEnd,
      });

      currentStart = block.startLine;
      currentEnd = block.endLine;
      currentLength = blockLength;
      continue;
    }

    currentEnd = block.endLine;
    currentLength += blockLength + 2;
  }

  ranges.push({
    startLine: currentStart,
    endLine: currentEnd,
  });

  return ranges.flatMap((range) => {
    const length = lines
      .slice(range.startLine - 1, range.endLine)
      .join("\n").length;

    if (
      length <= maximumChunkCharacters &&
      range.endLine - range.startLine + 1 <= maximumChunkLines
    ) {
      return [range];
    }

    return createSourceRanges(
      lines.slice(range.startLine - 1, range.endLine),
    ).map((nestedRange) => ({
      startLine: range.startLine + nestedRange.startLine - 1,
      endLine: range.startLine + nestedRange.endLine - 1,
    }));
  });
}

export function normalizeLibraryText(content: string): string {
  return content.replace(/\r\n?/g, "\n");
}

export function hashLibraryText(content: string): string {
  return hashText(content);
}

export function chunkLibraryText(input: {
  libraryId: string;
  libraryFileId: string;
  extension: string;
  content: string;
}): LibraryTextChunk[] {
  const normalizedContent = normalizeLibraryText(input.content);

  if (!normalizedContent.trim()) {
    return [];
  }

  const lines = normalizedContent.split("\n");
  const ranges = proseExtensions.has(input.extension.toLowerCase())
    ? createProseRanges(lines)
    : createSourceRanges(lines);

  const chunks: LibraryTextChunk[] = [];

  for (const range of ranges) {
    const chunk = createChunk({
      libraryId: input.libraryId,
      libraryFileId: input.libraryFileId,
      ordinal: chunks.length,
      lines,
      range,
    });

    if (chunk) {
      chunks.push(chunk);
    }
  }

  return chunks;
}
