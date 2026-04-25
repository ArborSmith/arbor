#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ArborCompileTools.generated.h"

/**
 * Live Coding helpers for the bridge.
 *
 * UE's `ILiveCodingModule` exposes `IsCompiling()`, a synchronous `Compile`
 * that takes a `WaitForCompletion` flag, and `OnPatchCompleteDelegate`. None
 * of these are reflected to Python out of the box, so callers that trigger
 * `LiveCoding.Compile` via the bridge have to file-poll generated headers /
 * grep `LogLiveCoding` to know when the compile is done. This wraps the
 * stable parts so Python (via `arbor.compile`) can:
 *
 *   - check whether a compile is currently running,
 *   - trigger a compile and wait synchronously for it to finish,
 *   - query the most recent OnPatchComplete result (for async polling).
 *
 * Tracks the most recent patch-complete event in a static variable so a
 * caller that triggered a compile and polled `is_compiling()` until it
 * returned false can still find out when the patch landed and reason about
 * staleness ("did the patch happen since I issued the compile").
 */
UCLASS()
class ARBOR_API UArborCompileTools : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Returns true if a Live Coding compile is currently in progress.
	 * Returns false if the LiveCoding module isn't loaded or isn't enabled
	 * for this session.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Arbor|Compile")
	static bool IsLiveCodingCompiling();

	/**
	 * Triggers a Live Coding compile and blocks until it finishes (or fails).
	 * Uses ILiveCodingModule::Compile(WaitForCompletion, &result) under the hood.
	 *
	 * @return JSON: {success, message, result_code, duration_sec}
	 *   result_code values mirror ELiveCodingCompileResult (Success, NoChanges,
	 *   Failure, Cancelled, NotStarted, CompileStillActive, InProgress).
	 *
	 * Note: blocks the editor's main thread for the duration of the compile.
	 * For a non-blocking flow use StartLiveCodingCompile + poll IsLiveCodingCompiling.
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Compile")
	static FString CompileAndWait();

	/**
	 * Triggers a Live Coding compile and returns immediately. Does not wait.
	 * Use IsLiveCodingCompiling + GetLastCompileResult to poll progress.
	 *
	 * @return JSON: {success, message}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Compile")
	static FString StartLiveCodingCompile();

	/**
	 * Returns the latest OnPatchComplete event captured by the helper.
	 *
	 * @return JSON: {has_result, completed_at_utc, time_since_seconds}
	 *   - has_result: false if no patch has fired since this helper started observing.
	 *   - completed_at_utc: ISO 8601 timestamp of the most recent patch event.
	 *   - time_since_seconds: how long ago (UTC clock).
	 *
	 * Note: ILiveCodingModule's OnPatchComplete fires on success and failure
	 * paths the same — pair this with the result_code returned by CompileAndWait
	 * (or with log scraping) if you need success/failure discrimination from
	 * the async path. (UE's delegate doesn't carry the result code.)
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Arbor|Compile")
	static FString GetLastCompileResult();
};
