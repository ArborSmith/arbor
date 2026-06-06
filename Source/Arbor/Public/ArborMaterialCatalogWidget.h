// Slate widget that browses the project's MaterialCatalog.
//
// Reads <project>/MaterialCatalog/_index.json for a fast load. Two-pane layout:
// list of entry tiles (thumbnail + id + status pill) on the left, detail panel
// (larger thumbnail + tags / traits / description) on the right.
//
// Read-only in the MVP; edit + action buttons land in a follow-up.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"
#include "Brushes/SlateDynamicImageBrush.h"
#include "UObject/StrongObjectPtr.h"

class SArborMaterialCatalogWidget;
class UMaterialInterface;
class UMaterialInstanceDynamic;

/** One catalog entry as seen by the widget. Populated from _index.json. */
struct FArborCatalogEntry
{
	FString Id;
	FString YamlPath;            // relative to entries/
	FString Type;                // "reference_material" (default) or "pattern"
	FString Source;              // /Game/.../M_X  (empty for pattern entries)
	FString MFPath;              // /Game/.../MF_X (set for pattern entries only)
	FString Status;              // ok / needs_review / bad / deprecated / broken
	FString Description;
	FString ShadingModel;
	FString BlendMode;
	FString ThumbnailAbsPath;    // absolute filesystem path; may not exist
	int32 ExpressionCount = 0;
	int32 ConnectionCount = 0;
	int32 OutputCount = 0;
	TArray<FString> Tags;
	TArray<FString> VisualTraits;
	TArray<FString> ProposedTags;          // from AI auto-tag
	TArray<FString> ProposedVisualTraits;
	FString ProposedDescription;
};


class SArborMaterialCatalogWidget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SArborMaterialCatalogWidget) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	// ---- Data ----

	/** Absolute path to <project>/MaterialCatalog/. */
	FString CatalogRoot;

	/** All entries loaded from _index.json. */
	TArray<TSharedPtr<FArborCatalogEntry>> AllEntries;

	/** Entries currently visible after status filter + search. */
	TArray<TSharedPtr<FArborCatalogEntry>> VisibleEntries;

	/** Cache of dynamic image brushes for thumbnails so SImage can show them. */
	TMap<FString, TSharedPtr<FSlateDynamicImageBrush>> ThumbnailBrushes;

	/** Live 3D preview viewport in the detail panel. */
	TSharedPtr<class SArborCatalogPreviewViewport> PreviewViewport;

	/** The selected entry in the list view (drives the detail panel). */
	TSharedPtr<FArborCatalogEntry> SelectedEntry;

	/** Active status filter; empty = "show all". */
	TSet<FString> StatusFilter;

	/** Search box content. */
	FString SearchText;

	// ---- Widgets ----

	TSharedPtr<SListView<TSharedPtr<FArborCatalogEntry>>> ListView;

	/** SBox we swap content into when the selection changes. */
	TSharedPtr<class SBox> DetailContainer;

	// ---- Methods ----

	/** Locate the project's catalog dir. Returns "" if not found. */
	FString ResolveCatalogRoot() const;

	/** Reload _index.json and rebuild the visible-entries view. */
	void LoadIndex();

	/** Re-apply status filter + search to AllEntries -> VisibleEntries. */
	void RefreshFilter();

	/** Slate row generator for the list view. */
	TSharedRef<class ITableRow> OnGenerateRow(TSharedPtr<FArborCatalogEntry> InEntry,
	                                           const TSharedRef<class STableViewBase>& OwnerTable);

	/** Selection change callback. */
	void OnSelectionChanged(TSharedPtr<FArborCatalogEntry> InEntry, ESelectInfo::Type SelectInfo);

	/** Load (or fetch cached) thumbnail brush for an entry. */
	TSharedPtr<FSlateDynamicImageBrush> GetThumbnailBrush(const TSharedPtr<FArborCatalogEntry>& Entry);

	/** Build the right-side detail panel for the currently selected entry. */
	TSharedRef<class SWidget> BuildDetailPanel();

	/** Color for the status pill in the row. */
	FLinearColor GetStatusColor(const FString& Status) const;

	// ---- Operations (dispatch to Python via IPythonScriptPlugin) ----

	/** Execute a catalog op via the Python dispatch table. Returns the result
	 *  dict (parsed from Saved/Arbor/last_result.json). */
	TSharedPtr<class FJsonObject> RunCatalogOp(const TSharedRef<FJsonObject>& Command);

	/** Save the in-memory edits on the currently selected entry. */
	FReply OnSaveSelected();
	FReply OnSetStatus(FString NewStatus);
	FReply OnAcceptProposals();
	FReply OnRejectProposals();
	FReply OnDeleteSelected();
	FReply OnOpenInEditor();

	/** Pending edits on the selection (not yet saved). Cleared on save / select. */
	FString PendingTagsCsv;
	FString PendingTraitsCsv;
	FString PendingDescription;
	bool bHasUnsavedEdits = false;

	/** Refresh the visible entries list after a save (also re-renders the
	 *  detail panel for the selected entry from the updated YAML). */
	void ReloadAfterEdit();

	// ---- Texture picker ----

	/** Param name -> ranked list of texture paths used by MICs in the project.
	 *  Loaded from <catalog>/_texture_suggestions.json. */
	TMap<FString, TArray<FString>> TextureSuggestions;

	/** Transient MID applied to the preview viewport when the user picks a
	 *  texture override. Reset on selection change. */
	TStrongObjectPtr<UMaterialInstanceDynamic> PreviewMID;

	/** SComboBox needs persistent TSharedPtr<FString> options - cache them. */
	TMap<FString, TArray<TSharedPtr<FString>>> ComboOptionsCache;

	/** Number of MICs covered by the loaded suggestions cache (for status text). */
	int32 TextureSuggestionsScannedCount = 0;

	void LoadTextureSuggestions();
	FReply OnRescanTextures();
	TSharedRef<class SWidget> BuildTextureParamPicker(UMaterialInterface* Mat);
	void OnTextureSelected(FString ParamName, TSharedPtr<FString> NewPath);

	/** ParamName -> TexturePath for the picker overrides on the current entry.
	 *  Empty until the user picks something; reset on selection change. */
	TMap<FString, FString> CurrentTextureOverrides;

	/** Re-render the entry's thumbnail using the current picker overrides,
	 *  then re-run the AI tagger so the proposed_* fields reflect the new
	 *  visual. */
	FReply OnRetagFromPreview();
};
