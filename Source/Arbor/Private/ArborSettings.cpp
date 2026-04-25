#include "ArborSettings.h"

UArborSettings::UArborSettings()
{
}

FString UArborSettings::GetEnabledFeaturesJson() const
{
	return FString::Printf(
		TEXT("{")
		TEXT("\"experimental\":%s,")
		TEXT("\"codex\":%s,")
		TEXT("\"concept_art_studio\":%s,")
		TEXT("\"environment\":%s,")
		TEXT("\"anchors\":%s,")
		TEXT("\"pcg\":%s")
		TEXT("}"),
		bEnableExperimentalFeatures ? TEXT("true") : TEXT("false"),
		bEnableCodex                ? TEXT("true") : TEXT("false"),
		bEnableConceptArtStudio     ? TEXT("true") : TEXT("false"),
		bEnableEnvironment          ? TEXT("true") : TEXT("false"),
		bEnableAnchors              ? TEXT("true") : TEXT("false"),
		bEnablePCG                  ? TEXT("true") : TEXT("false")
	);
}
