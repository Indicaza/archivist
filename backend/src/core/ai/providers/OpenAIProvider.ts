import OpenAI from "openai";
import { env } from "../../../config/env.js";
import type {
  AIMessage,
  AIProvider,
  AIProviderToolCall,
  AIProviderToolDefinition,
  DiscoveredAIModel,
  GenerateTextInput,
  GenerateTextResult,
  ProviderHealth,
  StreamTextOptions,
  StreamTextWithToolsOptions,
} from "../AIProvider.js";

const openAIClient = new OpenAI({
  apiKey: env.OPENAI_API_KEY,
});

const defaultMaximumToolRounds = 6;

function buildInput(messages: AIMessage[]) {
  return messages.map((message) => ({
    role: message.role,
    content: message.content,
  }));
}

function buildFunctionTools(tools: AIProviderToolDefinition[]) {
  return tools.map((tool) => ({
    type: "function" as const,
    name: tool.name,
    description: tool.description,
    parameters: tool.parameters,
    strict: true,
  }));
}

function parseToolArguments(value: string): unknown {
  try {
    return JSON.parse(value) as unknown;
  } catch {
    return {
      invalidJsonArguments: value,
    };
  }
}

function serializedToolOutput(value: unknown): string {
  const serialized = JSON.stringify(value);
  return serialized === undefined ? "null" : serialized;
}

function unixSecondsToIso(value: number | null | undefined): string | null {
  if (typeof value !== "number" || !Number.isFinite(value)) {
    return null;
  }

  return new Date(value * 1_000).toISOString();
}

type ResponseEventRecord = {
  type?: string;
  delta?: string;
  response?: {
    id?: string;
  };
  item?: {
    type?: string;
    call_id?: string;
    name?: string;
    arguments?: string;
  };
};

export class OpenAIProvider implements AIProvider {
  readonly providerId = "openai";
  readonly displayName = "OpenAI";

  async generateText(input: GenerateTextInput): Promise<GenerateTextResult> {
    return this.streamText(input);
  }

  async streamText(
    input: GenerateTextInput,
    options: StreamTextOptions = {},
  ): Promise<GenerateTextResult> {
    const stream = await openAIClient.responses.create(
      {
        model: input.generation.model,
        instructions: input.instructions,
        input: buildInput(input.messages),
        max_output_tokens: input.generation.maxOutputTokens ?? undefined,
        temperature: input.generation.temperature ?? undefined,
        top_p: input.generation.topP ?? undefined,
        stream: true,
      },
      {
        signal: options.signal,
      },
    );

    let text = "";

    for await (const event of stream) {
      if (
        event.type !== "response.output_text.delta" &&
        event.type !== "response.refusal.delta"
      ) {
        continue;
      }

      text += event.delta;
      await options.onDelta?.(event.delta);
    }

    const completedText = text.trim();

    if (!completedText) {
      throw new Error("OpenAI returned an empty response.");
    }

    return {
      text: completedText,
      provider: this.providerId,
      model: input.generation.model,
    };
  }

  async streamTextWithTools(
    input: GenerateTextInput,
    options: StreamTextWithToolsOptions,
  ): Promise<GenerateTextResult> {
    if (options.tools.length === 0) {
      return this.streamText(input, options);
    }

    const maximumToolRounds = Math.max(
      1,
      Math.min(12, options.maxToolRounds ?? defaultMaximumToolRounds),
    );
    const tools = buildFunctionTools(options.tools);
    let responseInput: unknown = buildInput(input.messages);
    let previousResponseId: string | undefined;
    let text = "";
    let toolCallCount = 0;
    let toolRoundCount = 0;

    for (let round = 0; round <= maximumToolRounds; round += 1) {
      const stream = await openAIClient.responses.create(
        {
          model: input.generation.model,
          instructions: input.instructions,
          input: responseInput as never,
          previous_response_id: previousResponseId,
          max_output_tokens: input.generation.maxOutputTokens ?? undefined,
          temperature: input.generation.temperature ?? undefined,
          top_p: input.generation.topP ?? undefined,
          tools: tools as never,
          tool_choice: "auto",
          parallel_tool_calls: false,
          stream: true,
        },
        {
          signal: options.signal,
        },
      );

      let responseId = "";
      const toolCalls: AIProviderToolCall[] = [];

      for await (const rawEvent of stream) {
        const event = rawEvent as unknown as ResponseEventRecord;

        if (event.response?.id) {
          responseId = event.response.id;
        }

        if (
          event.type === "response.output_text.delta" ||
          event.type === "response.refusal.delta"
        ) {
          const delta = event.delta ?? "";
          text += delta;
          await options.onDelta?.(delta);
          continue;
        }

        if (
          event.type !== "response.output_item.done" ||
          event.item?.type !== "function_call" ||
          !event.item.call_id ||
          !event.item.name
        ) {
          continue;
        }

        toolCalls.push({
          callId: event.item.call_id,
          name: event.item.name,
          arguments: parseToolArguments(event.item.arguments ?? "{}"),
          round: round + 1,
        });
      }

      if (toolCalls.length === 0) {
        const completedText = text.trim();

        if (!completedText) {
          throw new Error("OpenAI returned an empty response.");
        }

        return {
          text: completedText,
          provider: this.providerId,
          model: input.generation.model,
          toolCallCount,
          toolRoundCount,
        };
      }

      if (round >= maximumToolRounds) {
        throw new Error(
          `OpenAI exceeded the ${maximumToolRounds}-round AI tool limit.`,
        );
      }

      if (!responseId) {
        throw new Error("OpenAI did not return a response ID for tool continuation.");
      }

      toolRoundCount += 1;
      const toolOutputs = [];

      for (const toolCall of toolCalls) {
        toolCallCount += 1;
        const result = await options.onToolCall(toolCall);
        toolOutputs.push({
          type: "function_call_output" as const,
          call_id: toolCall.callId,
          output: serializedToolOutput(result.output),
        });
      }

      previousResponseId = responseId;
      responseInput = toolOutputs;
    }

    throw new Error("OpenAI tool execution ended without a final response.");
  }

  async discoverModels(): Promise<DiscoveredAIModel[]> {
    const page = await openAIClient.models.list();

    return page.data.map((model) => ({
      provider: this.providerId,
      modelId: model.id,
      createdAt: unixSecondsToIso(model.created),
      ownedBy: model.owned_by ?? null,
    }));
  }

  async checkHealth(): Promise<ProviderHealth> {
    const checkedAt = new Date().toISOString();

    try {
      await openAIClient.models.list();

      return {
        provider: this.providerId,
        status: "connected",
        checkedAt,
        message: null,
      };
    } catch (error) {
      return {
        provider: this.providerId,
        status: "unavailable",
        checkedAt,
        message:
          error instanceof Error
            ? error.message
            : "OpenAI could not be reached.",
      };
    }
  }
}

export const openAIProvider = new OpenAIProvider();
