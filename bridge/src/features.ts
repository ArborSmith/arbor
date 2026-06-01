import type { ExperimentalFeatureKey } from "./registry/experimental.js";

export interface Features {
  experimental: boolean;
  anchors?: boolean;
  environment?: boolean;
  codex?: boolean;
  concept_art_studio?: boolean;
  pcg?: boolean;
  widget?: boolean;
}

const ALL_EXPERIMENTAL_KEYS: ExperimentalFeatureKey[] = [
  "anchors",
  "environment",
  "codex",
  "concept_art_studio",
  "pcg",
  "widget",
];

function parseEnvOverride(raw: string): Features {
  const normalized = raw.trim().toLowerCase();
  if (normalized === "stable") {
    return { experimental: false };
  }
  if (normalized === "all") {
    const features: Features = { experimental: true };
    for (const key of ALL_EXPERIMENTAL_KEYS) features[key] = true;
    return features;
  }

  const parts = normalized.split(",").map((s) => s.trim()).filter(Boolean);
  const features: Features = { experimental: false };
  for (const key of ALL_EXPERIMENTAL_KEYS) features[key] = false;
  for (const part of parts) {
    if (part === "stable") continue;
    if (part === "experimental") {
      features.experimental = true;
      for (const key of ALL_EXPERIMENTAL_KEYS) features[key] = true;
      continue;
    }
    if ((ALL_EXPERIMENTAL_KEYS as string[]).includes(part)) {
      features.experimental = true;
      features[part as ExperimentalFeatureKey] = true;
    }
  }
  return features;
}

async function queryUe5Settings(): Promise<Features | null> {
  const port = process.env.UE5_REMOTE_PORT || "30010";
  const url = `http://127.0.0.1:${port}/remote/object/call`;

  try {
    const res = await fetch(url, {
      method: "PUT",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({
        objectPath: "/Script/Arbor.Default__ArborSettings",
        functionName: "GetEnabledFeaturesJson",
        parameters: {},
      }),
      signal: AbortSignal.timeout(2000),
    });

    if (!res.ok) return null;

    const envelope = (await res.json()) as { ReturnValue?: string };
    const json = envelope.ReturnValue;
    if (typeof json !== "string") return null;

    return JSON.parse(json) as Features;
  } catch {
    return null;
  }
}

export async function fetchEnabledFeatures(): Promise<Features> {
  const envOverride = process.env.ARBOR_TOOLS;
  if (envOverride) {
    return parseEnvOverride(envOverride);
  }

  const fromUe5 = await queryUe5Settings();
  if (fromUe5) return fromUe5;

  return { experimental: false };
}
