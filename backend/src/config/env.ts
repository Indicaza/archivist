import "dotenv/config";
import { z } from "zod";

const environmentSchema = z.object({
  NODE_ENV: z
    .enum(["development", "test", "production"])
    .default("development"),

  OPENAI_API_KEY: z.string().trim().min(1, "OPENAI_API_KEY is required."),

  OPENAI_MODEL: z.string().trim().min(1).default("gpt-5-mini"),
});

const parsedEnvironment = environmentSchema.safeParse(process.env);

if (!parsedEnvironment.success) {
  console.error(
    "Archivist environment configuration is invalid.",
    parsedEnvironment.error.flatten().fieldErrors,
  );

  throw new Error("Archivist could not load its environment configuration.");
}

export const env = parsedEnvironment.data;
