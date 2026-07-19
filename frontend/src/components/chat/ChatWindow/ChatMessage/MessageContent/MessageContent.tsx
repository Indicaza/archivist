import { Check, Code2, Copy } from "lucide-react";
import { useEffect, useMemo, useRef, useState } from "react";
import styles from "./MessageContent.module.css";

type MessageContentProps = {
  content: string;
};

type MessageSegment =
  | {
      type: "text";
      value: string;
    }
  | {
      type: "code";
      value: string;
      language: string;
    };

function parseContent(content: string): MessageSegment[] {
  const expression = /```([^\n`]*)\n?([\s\S]*?)```/g;
  const segments: MessageSegment[] = [];

  let cursor = 0;
  let match = expression.exec(content);

  while (match) {
    if (match.index > cursor) {
      segments.push({
        type: "text",
        value: content.slice(cursor, match.index),
      });
    }

    segments.push({
      type: "code",
      language: match[1].trim() || "code",
      value: match[2].replace(/\n$/, ""),
    });

    cursor = expression.lastIndex;
    match = expression.exec(content);
  }

  if (cursor < content.length) {
    segments.push({
      type: "text",
      value: content.slice(cursor),
    });
  }

  if (segments.length === 0) {
    return [
      {
        type: "text",
        value: content,
      },
    ];
  }

  return segments;
}

async function copyText(content: string): Promise<void> {
  if (navigator.clipboard?.writeText) {
    await navigator.clipboard.writeText(content);
    return;
  }

  const textarea = document.createElement("textarea");

  textarea.value = content;
  textarea.style.position = "fixed";
  textarea.style.opacity = "0";

  document.body.appendChild(textarea);
  textarea.select();
  document.execCommand("copy");
  textarea.remove();
}

export function MessageContent({ content }: MessageContentProps) {
  const [copiedIndex, setCopiedIndex] = useState<number | null>(null);
  const copiedTimerRef = useRef<number | null>(null);

  const segments = useMemo(() => parseContent(content), [content]);

  useEffect(() => {
    return () => {
      if (copiedTimerRef.current !== null) {
        window.clearTimeout(copiedTimerRef.current);
      }
    };
  }, []);

  async function handleCopyCode(index: number, value: string) {
    try {
      await copyText(value);

      setCopiedIndex(index);

      if (copiedTimerRef.current !== null) {
        window.clearTimeout(copiedTimerRef.current);
      }

      copiedTimerRef.current = window.setTimeout(() => {
        setCopiedIndex(null);
      }, 1600);
    } catch {
      setCopiedIndex(null);
    }
  }

  return (
    <div className={styles.content}>
      {segments.map((segment, index) => {
        if (segment.type === "text") {
          return segment.value ? (
            <div key={index} className={styles.textBlock}>
              {segment.value}
            </div>
          ) : null;
        }

        return (
          <section key={index} className={styles.codeBlock}>
            <header className={styles.codeHeader}>
              <span className={styles.codeLanguage}>
                <Code2 size={12} strokeWidth={2} />
                {segment.language}
              </span>

              <button
                className={styles.copyCodeButton}
                type="button"
                onClick={() => void handleCopyCode(index, segment.value)}
                aria-label="Copy code"
                title={copiedIndex === index ? "Copied" : "Copy code"}
              >
                {copiedIndex === index ? (
                  <Check size={12} strokeWidth={2.2} />
                ) : (
                  <Copy size={12} strokeWidth={2} />
                )}

                <span>{copiedIndex === index ? "Copied" : "Copy"}</span>
              </button>
            </header>

            <pre className={styles.code}>
              <code>{segment.value}</code>
            </pre>
          </section>
        );
      })}
    </div>
  );
}
