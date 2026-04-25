#pragma once

#include "CoreMinimal.h"

/**
 * Derives a content folder path from a GameContext asset path and a category subfolder.
 *
 * Given a GameContext path like "/Game/GameCodex/Verdant_Hollow/GC_Verdant_Hollow"
 * (or with .ObjectName suffix), strips to the parent directory and appends the subfolder:
 *   → "/Game/GameCodex/Verdant_Hollow/Locations"
 *
 * @param GameContextAssetPath  Full or partial asset path (with or without .ObjectName)
 * @param SubfolderName         Category subfolder (e.g. "Locations", "Enemies"). Empty = parent dir itself.
 * @return Derived content path, or empty string if input is empty.
 */
inline FString DerivePathFromGameContext(const FString& GameContextAssetPath, const FString& SubfolderName)
{
	if (GameContextAssetPath.IsEmpty())
	{
		return FString();
	}

	// Strip ".ObjectName" suffix if present (e.g. "/Game/X/GC_Y.GC_Y" → "/Game/X/GC_Y")
	FString PackagePath = GameContextAssetPath;
	int32 DotIdx;
	if (PackagePath.FindChar(TEXT('.'), DotIdx))
	{
		PackagePath = PackagePath.Left(DotIdx);
	}

	// Take the parent directory: "/Game/X/GC_Y" → "/Game/X"
	FString ParentDir = FPaths::GetPath(PackagePath);

	if (SubfolderName.IsEmpty())
	{
		return ParentDir;
	}

	return ParentDir / SubfolderName;
}
