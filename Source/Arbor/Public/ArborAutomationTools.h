#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ArborAutomationTools.generated.h"

/**
 * Automation test runner helpers for the bridge.
 *
 * UE's `Automation RunTests <filter>` console command serialises through a
 * single queue, gates on `FWaitForInteractiveFrameRate` (≥10 FPS), and surfaces
 * results only via log entries. This wrapper bypasses the controller layer and
 * drives `FAutomationTestFramework` directly — each filtered test runs
 * synchronously on the calling thread, results are collected via
 * `FAutomationTestExecutionInfo`, and the whole run returns as a single JSON
 * payload. No FPS gate, no queue contention, no log scraping.
 *
 * Pythonic wrappers live in `arbor.automation`.
 */
UCLASS()
class ARBOR_API UArborAutomationTools : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * List every test path registered with the framework that matches the filter.
	 *
	 * @param Filter   Empty = all tests. Otherwise only tests whose full path
	 *                 starts with Filter (e.g. "MyGame.Combat.Damage").
	 * @return JSON: {count, tests: [path1, path2, ...]}
	 *
	 * Useful before a run to confirm what would actually execute.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Arbor|Automation")
	static FString ListTests(const FString& Filter);

	/**
	 * Run every test matching Filter and block until done. Each test runs synchronously
	 * via `FAutomationTestFramework::StartTestByName` + `ExecuteLatentCommands` loop
	 * + `StopTest`. No controller queue, no FPS gate.
	 *
	 * @param Filter            Same prefix-matching as ListTests. Empty = all tests.
	 * @param TimeoutSeconds    Per-test latent-command timeout. -1 = no per-test timeout.
	 * @param MaxErrorsPerTest  Cap on number of error messages captured per test (full
	 *                          counts are still reported). 0 = no cap. Default 5.
	 * @return JSON:
	 *   {
	 *     summary: {total, passed, failed, duration_sec},
	 *     tests: [
	 *       {path, success, duration_sec, error_count, warning_count, errors: [...]},
	 *       ...
	 *     ]
	 *   }
	 *
	 * Note: blocks the editor's main thread for the duration of the run. Caller
	 * is expected to be a script-driven invocation (bridge / console / commandlet),
	 * not the interactive editor UI.
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Automation")
	static FString RunTestsAndWait(const FString& Filter, float TimeoutSeconds = 300.0f, int32 MaxErrorsPerTest = 5);
};
