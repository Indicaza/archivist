export type Chat = {
  id: string;
  title: string;
  archivedAt: string | null;
  createdAt: string;
  updatedAt: string;
};

export type MessageRole = "user" | "assistant" | "system";

export type MessageStatus = "streaming" | "complete" | "cancelled" | "failed";

export type ChatMessage = {
  id: string;
  chatId: string;
  role: MessageRole;
  content: string;
  status: MessageStatus;
  createdAt: string;
  updatedAt: string;
};

export type CreateChatInput = {
  title?: string;
};

export type CreateMessageInput = {
  role: MessageRole;
  content: string;
  status?: MessageStatus;
};

export type ArchiveChatResult = {
  chat: Chat;
  selectedChatId: string | null;
};

export type DeleteChatResult = {
  selectedChatId: string | null;
};
