import type {
  ArchiveChatResult,
  Chat,
  ChatMessage,
  CompleteChatTurnResult,
  CreateChatInput,
  CreateMessageInput,
  DeleteChatResult,
  UpdateChatInput,
} from "./chat.types";

const API_BASE_URL = "http://127.0.0.1:3333/api";

type ErrorResponse = {
  ok: false;
  error?: {
    message?: string;
  };
};

type ChatsResponse = {
  ok: true;
  chats: Chat[];
};

type ChatResponse = {
  ok: true;
  chat: Chat;
};

type MessagesResponse = {
  ok: true;
  messages: ChatMessage[];
};

type MessageResponse = {
  ok: true;
  message: ChatMessage;
};

type CompleteChatTurnResponse = {
  ok: true;
  userMessage: ChatMessage;
  assistantMessage: ChatMessage;
  provider: string;
  model: string;
  agentId: string;
};

type SelectedChatResponse = {
  ok: true;
  selectedChatId: string | null;
};

type ArchiveChatResponse = {
  ok: true;
  chat: Chat;
  selectedChatId: string | null;
};

type DeleteChatResponse = {
  ok: true;
  selectedChatId: string | null;
};

async function request<T>(path: string, options?: RequestInit): Promise<T> {
  const response = await fetch(`${API_BASE_URL}${path}`, {
    ...options,
    headers: {
      "Content-Type": "application/json",
      ...options?.headers,
    },
  });

  if (!response.ok) {
    const body = (await response
      .json()
      .catch(() => null)) as ErrorResponse | null;

    throw new Error(
      body?.error?.message ??
        `Archivist API request failed with status ${response.status}.`,
    );
  }

  return (await response.json()) as T;
}

export async function fetchChats(): Promise<Chat[]> {
  const response = await request<ChatsResponse>("/chats");

  return response.chats;
}

export async function fetchArchivedChats(): Promise<Chat[]> {
  const response = await request<ChatsResponse>("/chats/archived");

  return response.chats;
}

export async function fetchChat(chatId: string): Promise<Chat> {
  const response = await request<ChatResponse>(`/chats/${chatId}`);

  return response.chat;
}

export async function addChat(input: CreateChatInput = {}): Promise<Chat> {
  const response = await request<ChatResponse>("/chats", {
    method: "POST",
    body: JSON.stringify(input),
  });

  return response.chat;
}

export async function editChat(
  chatId: string,
  input: UpdateChatInput,
): Promise<Chat> {
  const response = await request<ChatResponse>(`/chats/${chatId}`, {
    method: "PATCH",
    body: JSON.stringify(input),
  });

  return response.chat;
}

export async function archiveChat(chatId: string): Promise<ArchiveChatResult> {
  const response = await request<ArchiveChatResponse>(
    `/chats/${chatId}/archive`,
    {
      method: "POST",
    },
  );

  return {
    chat: response.chat,
    selectedChatId: response.selectedChatId,
  };
}

export async function restoreChat(chatId: string): Promise<Chat> {
  const response = await request<ChatResponse>(`/chats/${chatId}/restore`, {
    method: "POST",
  });

  return response.chat;
}

export async function removeChat(chatId: string): Promise<DeleteChatResult> {
  const response = await request<DeleteChatResponse>(`/chats/${chatId}`, {
    method: "DELETE",
  });

  return {
    selectedChatId: response.selectedChatId,
  };
}

export async function fetchMessages(chatId: string): Promise<ChatMessage[]> {
  const response = await request<MessagesResponse>(`/chats/${chatId}/messages`);

  return response.messages;
}

export async function addMessage(
  chatId: string,
  input: CreateMessageInput,
): Promise<ChatMessage> {
  const response = await request<MessageResponse>(`/chats/${chatId}/messages`, {
    method: "POST",
    body: JSON.stringify(input),
  });

  return response.message;
}

export async function respondToChat(
  chatId: string,
  content: string,
): Promise<CompleteChatTurnResult> {
  const response = await request<CompleteChatTurnResponse>(
    `/chats/${chatId}/respond`,
    {
      method: "POST",
      body: JSON.stringify({
        content,
      }),
    },
  );

  return {
    userMessage: response.userMessage,
    assistantMessage: response.assistantMessage,
    provider: response.provider,
    model: response.model,
    agentId: response.agentId,
  };
}

export async function updateSelectedChat(
  chatId: string | null,
): Promise<string | null> {
  const response = await request<SelectedChatResponse>("/chats/selected", {
    method: "PATCH",
    body: JSON.stringify({
      chatId,
    }),
  });

  return response.selectedChatId;
}
