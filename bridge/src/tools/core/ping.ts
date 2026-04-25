import { isConnected, getPort } from "../../ue5-client.js";

export const pingSchema = {};

export async function ping(): Promise<{
  connected: boolean;
  port: number;
  message: string;
}> {
  const port = getPort();
  const connected = await isConnected();
  return {
    connected,
    port,
    message: connected
      ? `UE5 editor is reachable at port ${port}`
      : `Cannot reach UE5 editor at port ${port}. Make sure the editor is running and the Remote Control API plugin is enabled.`,
  };
}
