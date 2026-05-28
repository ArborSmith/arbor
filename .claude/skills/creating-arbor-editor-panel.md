---
name: creating-arbor-editor-panel
description: Use this skill when the user asks to add a new editor window / tab / panel to Arbor — anything that needs to live as a tabbed UE5 editor view with a Slate widget. Covers tab registration, the SCompoundWidget skeleton, calling Python from Slate via IPythonScriptPlugin (the dispatch pattern), and embedding a live 3D preview viewport.
---

# Creating an Arbor editor panel

The Material Catalog panel (`SArborMaterialCatalogWidget` + `FArborMaterialCatalogTab`) is the reference implementation; reuse its shape when adding a new one.

## Tab + module wiring

Per panel you need three files:

1. **`Public/Arbor<Name>Tab.h`** + **`Private/Arbor<Name>Tab.cpp`** — tab spawner. Mirror `ArborMaterialCatalogTab` exactly; change the `TabId`, display name, and SNew target widget. Pattern:

   ```cpp
   const FName FArbor<Name>Tab::TabId(TEXT("Arbor<Name>"));

   void FArbor<Name>Tab::Register() {
       if (FGlobalTabmanager::Get()->HasTabSpawner(TabId)) return;
       FGlobalTabmanager::Get()->RegisterNomadTabSpawner(TabId,
           FOnSpawnTab::CreateStatic(&FArbor<Name>Tab::SpawnTab))
           .SetDisplayName(LOCTEXT("TabTitle", "<Name>"))
           .SetMenuType(ETabSpawnerMenuType::Hidden)
           .SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.ContentBrowser"));
   }
   ```

2. **`Public/Arbor<Name>Widget.h`** + **`Private/Arbor<Name>Widget.cpp`** — the SCompoundWidget that lives inside the tab. State + UI.

3. **`ArborModule.cpp`** — three edits:
   - `#include "Arbor<Name>Tab.h"` at the top
   - `FArbor<Name>Tab::Register();` inside `StartupModule()` (alongside the other Register calls)
   - `FArbor<Name>Tab::Unregister();` inside `ShutdownModule()`
   - A `Section.AddMenuEntry(...)` block in the Tools menu registration (around line 100) so it appears under Tools > Arbor

Important: **new tab classes need a full editor restart** to register. Live Coding can patch widget method bodies but not new tab spawners. Use the `restart-ue-editor` skill after the first build, and any time you add new UCLASS/UFUNCTION/USTRUCT.

## Slate widget patterns used in the Material Catalog

- **Tab role**: `SNew(SDockTab).TabRole(NomadTab)` for standalone floating tabs (not docked into a specific area).
- **Two-pane layout**: `SSplitter` (Orientation = Orient_Horizontal) with grid on the left, detail panel on the right. Each `SSplitter::Slot()` takes a `Value(0..1)` for initial width fraction.
- **Scrollable detail**: wrap the right-side detail panel in `SScrollBox` so it works at any height.
- **Dynamic content swap**: have a `TSharedPtr<SBox> DetailContainer` member; `SAssignNew(DetailContainer, SBox)[...]` in Construct, then `DetailContainer->SetContent(BuildDetailPanel())` on selection change.
- **Status pills**: `SBorder` with `FAppStyle::GetBrush("RoundedFilledBorder")` + tinted `BorderBackgroundColor` for rounded color chips. Status-to-color via a method returning `FLinearColor`.

## Loading PNG thumbnails as Slate brushes

Standard pattern for displaying generated PNG files in Slate:

```cpp
TArray<uint8> FileData;
FFileHelper::LoadFileToArray(FileData, *AbsPath);
FImage Loaded;
FImageUtils::DecompressImage(FileData.GetData(), FileData.Num(), Loaded);
// PNGs always decode to BGRA8 in UE 5.7. Bail if not - don't try to convert
// via FImage::CopyTo since that's not ENGINE_API exported.
TArray<uint8> Raw;
Raw.Append(Loaded.RawData.GetData(), Loaded.RawData.Num());
TSharedPtr<FSlateDynamicImageBrush> Brush = FSlateDynamicImageBrush::CreateWithImageData(
    BrushName, FVector2D(Loaded.SizeX, Loaded.SizeY), Raw);
```

Cache the brushes in a `TMap<FString, TSharedPtr<FSlateDynamicImageBrush>>` so they survive the panel's lifetime.

## Calling Python from Slate

When the panel needs to write back to project data (YAML files, asset metadata, etc.), route through Python so the file format stays canonical. The pattern:

1. Add `PythonScriptPlugin` to `Arbor.Build.cs`'s `PrivateDependencyModuleNames`.
2. Include `IPythonScriptPlugin.h` in your widget cpp.
3. Have a Python module under `.claude/skills/<your-skill>/extraction/dispatch.py` (or wherever fits) with an entry point that takes a JSON command and writes a result via `arbor.utils.write_result()`:

   ```python
   def run(command_json):
       cmd = json.loads(command_json)
       op = cmd.get("op")
       result = OPS[op](cmd)
       import arbor.utils as u; u.write_result(result)
       return result
   ```

4. From the Slate widget, invoke it:

   ```cpp
   FPythonCommandEx PyCmd;
   PyCmd.Command = FString::Printf(
       TEXT("import sys; sys.path.insert(0, r'%s'); "
            "from <module> import dispatch; dispatch.run(r'''%s''')"),
       *PathToScriptDir,
       *SerializedJsonCommand);
   PyCmd.ExecutionMode = EPythonCommandExecutionMode::ExecuteStatement;
   IPythonScriptPlugin::Get()->ExecPythonCommandEx(PyCmd);
   ```

5. Read the result back from `<project>/Saved/Arbor/last_result.json`:

   ```cpp
   FString Raw;
   FFileHelper::LoadFileToString(Raw, *(FPaths::ProjectSavedDir() / TEXT("Arbor/last_result.json")));
   // parse via FJsonSerializer
   ```

Use a raw triple-quoted Python string (`r'''...'''`) for the JSON payload so you don't have to escape double-quotes. **Escape backslashes** in any Windows paths embedded in the Python literal — `PathToPyLiteral()` in `ArborMaterialCatalogWidget.cpp` is the helper.

## Embedding a live 3D preview viewport

The Material Catalog uses `FAdvancedPreviewScene` + a custom `FEditorViewportClient` subclass. Reference: `ArborCatalogPreviewViewport.h/.cpp`. Required Build.cs deps: `UnrealEd`, `AdvancedPreviewScene`, `InputCore`.

Key gotchas:

- **Sphere center is at component origin**, not at the floor. UE basic sphere is 100cm diameter; `FAdvancedPreviewScene`'s floor sits at Z=0. Lift the sphere by 50cm (`FTransform(FRotator::ZeroRotator, FVector(0, 0, 50.f))`) or it clips through.
- **Mouse rotation**: track LMB state in `InputKey`, accumulate yaw/pitch deltas in `InputAxis` (`MouseX` / `MouseY` keys), call `SetWorldRotation` on the component. Don't use UE's built-in orbit camera (`bUsingOrbitCamera = false`) — it moves the camera around the sphere instead of rotating the sphere itself, which is the wrong UX for a material preview.
- **EngineShowFlags.SetGrid(false)** kills the floor grid; `SetGame(true)` removes editor gizmos. Pre-set these in the viewport client's constructor.
- **Reuse one viewport across selections** — don't recreate the viewport widget when the selection changes. Cache it as a member, call `SetMaterial(...)` to swap.

## Index file pattern

Don't have Slate parse YAMLs directly (no yaml-cpp in the engine). Have Python emit a flat `_index.json` whenever entries change, and Slate reads that with `FJsonSerializer`. Pattern from `_index.py`:

- Python `refresh_index(entries_dir)` scans every YAML, projects down to a flat record per entry, writes `<root>/_index.json`.
- Every Python op that mutates a YAML calls `refresh_index()` before returning.
- Slate reloads the index after any Python op completes (`LoadIndex()` then re-select previous entry).

## When NOT to use this skill

- Read-only views over runtime data that already has a Python API → just use `ue5_run_python` from the bridge; no Slate widget needed.
- One-shot operations triggered from a script → console command via `ExecuteConsoleCommand` is enough.
- UI that needs to live inside the running game → this is editor-only; for gameplay UI use UMG/CommonUI.
