#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "ArborCharacterTypes.h"

struct FArborCodexContext;
class SEditableTextBox;
class SMultiLineEditableTextBox;
class STextComboBox;
class SWrapBox;
class SScrollBox;

class SArborCharacterSheetWidget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SArborCharacterSheetWidget) {}
		SLATE_ARGUMENT(TSharedPtr<FArborCodexContext>, CodexContext)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	struct FCharacterListEntry
	{
		FString AssetPath;
		FString Name;
	};

	// Codex context (optional - null when used standalone)
	TSharedPtr<FArborCodexContext> CodexContext;
	FDelegateHandle ContextChangedHandle;

	// Data
	TArray<FCharacterListEntry> AllCharacters;
	TArray<int32> FilteredIndices;
	int32 SelectedIndex = -1;

	// Filter state
	FString FolderPath;
	FString SearchText;
	int32 RoleFilterIndex = 0;  // 0 = All, 1+ = EArborCharacterRole values

	// Role filter
	TSharedPtr<STextComboBox> RoleFilterCombo;
	TArray<TSharedPtr<FString>> RoleFilterOptions;
	void OnRoleFilterChanged(TSharedPtr<FString> NewValue, ESelectInfo::Type SelectInfo);

	// Currently loaded asset path (empty = new character)
	FString CurrentAssetPath;

	// UI elements - left panel
	TSharedPtr<SVerticalBox> CharacterListBox;

	// UI elements - right panel (always-editable detail form)
	TSharedPtr<SScrollBox> DetailPanel;

	// Form inputs (created dynamically in RebuildDetailPanel)
	TSharedPtr<SEditableTextBox> NameInput;
	TSharedPtr<SMultiLineEditableTextBox> DescriptionInput;

	// Tags (dynamic pill editing)
	TArray<FString> CurrentTags;
	TSharedPtr<SWrapBox> TagsPillBox;
	TSharedPtr<SEditableTextBox> AddTagInput;

	// Section lock state
	TSet<FName> LockedSections;

	// Location selector
	TSharedPtr<STextComboBox> LocationComboBox;
	TArray<TSharedPtr<FString>> LocationDisplayNames;
	TArray<FString> LocationAssetPaths;
	int32 SelectedLocationIndex = -1;

	// Keywords
	TSharedPtr<SEditableTextBox> KeywordSearchBox;
	TSharedPtr<SWrapBox> AvailableKeywordsWrapBox;
	TSharedPtr<SWrapBox> SelectedKeywordsWrapBox;
	TArray<FString> AllKeywords;
	TArray<FString> SelectedKeywords;
	FString KeywordFilterText;

	// Game Context selector (standalone mode only)
	TSharedPtr<STextComboBox> GameContextComboBox;
	TArray<TSharedPtr<FString>> GameContextDisplayNames;
	TArray<FString> GameContextAssetPaths;
	int32 SelectedGameContextIndex = -1;

	// Status feedback
	TSharedPtr<STextBlock> StatusLabel;

	// Transient state for form fields
	FString CurrentName;
	FString CurrentDescription;

	// Codex context change handler
	void OnCodexContextChanged();

	// List management
	void ScanFolder();
	void ApplyFilter();
	void RebuildList();

	FReply OnScanClicked();
	void OnSearchTextChanged(const FText& NewText);
	void OnFolderPathCommitted(const FText& NewText, ETextCommit::Type CommitType);

	// Detail panel
	void RebuildDetailPanel();
	void PopulateFromAsset(int32 Index);
	void ClearAllFields();
	FReply OnNewCharacterClicked();

	// Section headers with lock + AI buttons
	TSharedRef<SWidget> CreateSectionHeader(const FText& Title, FName SectionId);
	FReply OnToggleLock(FName SectionId);
	bool IsSectionLocked(FName SectionId) const;

	// Per-section AI
	FReply OnSectionAIClicked(FName SectionId);
	FString BuildSectionPrompt(FName SectionId) const;

	// Generate All (respects locks)
	FReply OnGenerateAllClicked();
	FString BuildGenerateAllPrompt() const;

	// Unified save
	FReply OnSaveClicked();
	FString CollectFormAsJson() const;

	// Open in UE5
	FReply OnOpenInEditorClicked();

	// Delete character
	FReply OnDeleteCharacterClicked();

	// Generate 3D mesh from concept art
	FReply OnGenerate3DMeshClicked();

	// Tag editing
	void AddTag(const FString& InTag);
	void RemoveTag(const FString& InTag);
	TSharedRef<SWidget> CreateRemovableTagPill(const FString& InTag);
	void RebuildTagsPills();

	// Game context / location / keywords
	void ScanGameContextAssets();
	void ScanLocationAssets();
	void OnGameContextSelected(TSharedPtr<FString> NewValue, ESelectInfo::Type SelectInfo);
	void OnLocationSelected(TSharedPtr<FString> NewValue, ESelectInfo::Type SelectInfo);
	void LoadKeywordsFromContext();
	void RebuildAvailableKeywords();
	void RebuildSelectedKeywords();
	void OnKeywordSearchChanged(const FText& NewText);
	void AddKeyword(const FString& Keyword);
	void RemoveKeyword(const FString& Keyword);
	TSharedRef<SWidget> CreateClickableKeywordPill(const FString& Keyword);
	TSharedRef<SWidget> CreateRemovableKeywordPill(const FString& Keyword);
};
