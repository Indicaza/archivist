export type Chat = {
  id: string;
  libraryId: string | null;
  libraryName: string | null;
  title: string;
  agentId: string;
  agentIds: string[];
  archivedAt: string | null;
  createdAt: string;
  updatedAt: string;
};


export type ChatFileAttachment = {
  id: string;
  chatId: string;
  libraryId: string;
  libraryName: string;
  fileId: string;
  fileName: string;
  relativePath: string;
  extension: string;
  sizeBytes: number;
  fileStatus: "available" | "unreadable" | "missing";
  createdAt: string;
};

export type CreateChatFileAttachmentInput = {
  libraryId: string;
  fileId: string;
};

export type ChatAttachmentSource = {
  attachmentId: string;
  libraryId: string;
  libraryName: string;
  fileId: string;
  fileName: string;
  relativePath: string;
  estimatedTokens: number;
  truncated: boolean;
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

export type ChatMessagePage = {
  messages: ChatMessage[];
  hasMore: boolean;
  nextBeforeMessageId: string | null;
};

export type CreateChatInput = {
  libraryId: string;
  title?: string;
  agentId?: string;
};

export type UpdateChatInput = {
  title?: string;
  agentId?: string;
};

export type AttachChatAgentInput = {
  agentId: string;
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
