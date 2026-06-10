// TEMPORARY M0 feasibility spike for Niagara stack-level authoring.
//
// Exercises the full create -> add emitter -> add module -> set inputs ->
// renderer -> compile -> read errors loop against the UE 5.7 NiagaraEditor
// APIs, headlessly (no Niagara editor window). The outcome decides the
// design of UArborNiagaraTools (M1). This class is scratch code and will be
// deleted once UArborNiagaraTools lands.
//
// Open questions this spike answers:
//   1. Does the exported API surface link and behave outside NiagaraEditor?
//   2. What is the exact rapid iteration constant alias format?
//   3. Does a local pin-splice module removal work (RemoveModuleFromStack is
//      not exported)?
//   4. Does SetLinkedParameterValueForFunctionInput work headlessly?
//   5. What dirty/refresh calls are needed for the asset to open clean in
//      the Niagara editor afterwards?

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ArborNiagaraSpike.generated.h"

UCLASS()
class ARBOR_API UArborNiagaraSpike : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Run the M0 feasibility spike. Builds two systems:
	 *   A: default-template emitter + GravityForce module + local input +
	 *      linked input + add/remove module (splice test) + extra renderer
	 *   B: minimal emitter assembled from scratch (SpawnRate,
	 *      InitializeParticle, ParticleState, GravityForce, Drag, solver,
	 *      sprite renderer)
	 * Both are compiled and their per-script compile statuses reported.
	 *
	 * @param ParamsJson  JSON: {path_a?, path_b?} - target asset paths.
	 *                    Defaults: /Game/VFX/Spike/NS_ArborSpikeA and ...B.
	 *                    Existing assets at those paths are deleted first.
	 * @return JSON: {success, steps:[{step, success, detail}],
	 *                a:{path, compile:[...]}, b:{path, compile:[...]},
	 *                rapid_iteration_params:[...]}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|NiagaraSpike")
	static FString RunNiagaraSpike(const FString& ParamsJson);
};
