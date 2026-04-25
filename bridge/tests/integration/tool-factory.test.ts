import { describe, it, expect } from "vitest";
import { buildDescription } from "../../src/tool-factory.js";
import type { CategoryTool } from "../../src/registry/types.js";
import { z } from "zod";

/** Minimal CategoryTool for testing (no real handlers needed). */
function makeTool(overrides: Partial<CategoryTool>): CategoryTool {
  return {
    description: "Test tool description.",
    schema: {},
    actions: {},
    ...overrides,
  };
}

const noop = async () => ({});

describe("buildDescription", () => {
  it("returns raw description when actionParams is absent", () => {
    const tool = makeTool({
      actions: { foo: noop, bar: noop },
    });
    expect(buildDescription(tool, ["foo", "bar"])).toBe(
      "Test tool description."
    );
  });

  it("appends suffix when actionParams is absent", () => {
    const tool = makeTool({
      actions: { foo: noop },
    });
    expect(buildDescription(tool, ["foo"], "(read-only queries)")).toBe(
      "Test tool description. (read-only queries)"
    );
  });

  it("generates per-action param listing when actionParams is present", () => {
    const tool = makeTool({
      schema: {
        asset_path: z.string().optional(),
        name: z.string().optional(),
        filter: z.string().optional(),
      },
      actions: { create: noop, query: noop, list: noop },
      actionParams: {
        create: {
          summary: "Create something",
          required: ["asset_path"],
          optional: ["name"],
        },
        query: {
          summary: "Query structure",
          required: ["asset_path"],
        },
        list: {
          summary: "List available types",
          optional: ["filter"],
        },
      },
    });

    const desc = buildDescription(tool, ["create", "query", "list"]);
    expect(desc).toContain("Actions:");
    expect(desc).toContain(
      "- create(asset_path, name?) \u2014 Create something"
    );
    expect(desc).toContain("- query(asset_path) \u2014 Query structure");
    expect(desc).toContain("- list(filter?) \u2014 List available types");
  });

  it("handles actions with no params", () => {
    const tool = makeTool({
      actions: { count: noop },
      actionParams: {
        count: { summary: "Get total count" },
      },
    });

    const desc = buildDescription(tool, ["count"]);
    expect(desc).toContain("- count() \u2014 Get total count");
  });

  it("only includes actions from the provided list", () => {
    const tool = makeTool({
      schema: {
        path: z.string().optional(),
      },
      actions: { read: noop, write: noop },
      actionParams: {
        read: { summary: "Read data", required: ["path"] },
        write: { summary: "Write data", required: ["path"] },
      },
    });

    const queryDesc = buildDescription(tool, ["read"]);
    expect(queryDesc).toContain("read");
    expect(queryDesc).not.toContain("write");

    const writeDesc = buildDescription(tool, ["write"]);
    expect(writeDesc).toContain("write");
    expect(writeDesc).not.toContain("- read");
  });

  it("falls back gracefully for actions missing from actionParams", () => {
    const tool = makeTool({
      actions: { known: noop, unknown: noop },
      actionParams: {
        known: { summary: "Known action" },
        // 'unknown' deliberately omitted
      },
    });

    const desc = buildDescription(tool, ["known", "unknown"]);
    expect(desc).toContain("- known() \u2014 Known action");
    expect(desc).toContain("- unknown");
  });

  it("includes suffix in description when actionParams is present", () => {
    const tool = makeTool({
      actions: { foo: noop },
      actionParams: {
        foo: { summary: "Do foo" },
      },
    });

    const desc = buildDescription(tool, ["foo"], "(read-only queries)");
    expect(desc).toContain("Test tool description. (read-only queries)");
    expect(desc).toContain("- foo() \u2014 Do foo");
  });
});
