#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

struct IConsoleCommand;

class FArborModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	void RegisterMenus();
	IConsoleCommand* BuildBTCommand = nullptr;
	IConsoleCommand* BuildBPCommand = nullptr;
	IConsoleCommand* BuildEQSCommand = nullptr;
	IConsoleCommand* TextureReviewCommand = nullptr;
	IConsoleCommand* TextVariationCommand = nullptr;
	IConsoleCommand* PreviewCommand = nullptr;
	IConsoleCommand* BuildAnimGraphCommand = nullptr;
	IConsoleCommand* RegistryCommand = nullptr;
	IConsoleCommand* LiveCompileCommand = nullptr;
	IConsoleCommand* ScreenshotsCommand = nullptr;
	IConsoleCommand* ShowImageCommand = nullptr;
	IConsoleCommand* BuildPCGCommand = nullptr;
	IConsoleCommand* ConceptArtStudioCommand = nullptr;

	FDelegateHandle AssetAddedHandle;
	FDelegateHandle AssetRemovedHandle;
};
