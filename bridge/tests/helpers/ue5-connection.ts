import { isConnected } from "../../src/ue5-client.js";

let cachedResult: boolean | null = null;

export async function checkEditor(): Promise<boolean> {
  if (cachedResult === null) {
    cachedResult = await isConnected();
  }
  return cachedResult;
}
