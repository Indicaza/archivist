const CHARACTERS_PER_TOKEN = 4;

export function estimateTokens(content: string): number {
  if (!content) {
    return 0;
  }

  return Math.max(1, Math.ceil(content.length / CHARACTERS_PER_TOKEN));
}
