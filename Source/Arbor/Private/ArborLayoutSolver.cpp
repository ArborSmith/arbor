#include "ArborLayoutSolver.h"
#include "ArborAnchorAnalyzer.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"

// ---------------------------------------------------------------------------
// JSON helpers
// ---------------------------------------------------------------------------

static TSharedPtr<FJsonObject> ParseJson(const FString& Json)
{
	TSharedPtr<FJsonObject> Obj;
	auto Reader = TJsonReaderFactory<>::Create(Json);
	FJsonSerializer::Deserialize(Reader, Obj);
	return Obj;
}

static FString SerializeJson(TSharedPtr<FJsonObject> Root)
{
	FString Output;
	auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
	return Output;
}

static TSharedPtr<FJsonObject> VecToJson(const FVector& V)
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetNumberField(TEXT("x"), V.X);
	Obj->SetNumberField(TEXT("y"), V.Y);
	Obj->SetNumberField(TEXT("z"), V.Z);
	return Obj;
}

static TSharedPtr<FJsonObject> RotToJson(const FRotator& R)
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetNumberField(TEXT("pitch"), R.Pitch);
	Obj->SetNumberField(TEXT("yaw"), R.Yaw);
	Obj->SetNumberField(TEXT("roll"), R.Roll);
	return Obj;
}

static FVector JsonToVec(const TSharedPtr<FJsonObject>& Obj)
{
	return FVector(
		Obj->GetNumberField(TEXT("x")),
		Obj->GetNumberField(TEXT("y")),
		Obj->GetNumberField(TEXT("z"))
	);
}

// ---------------------------------------------------------------------------
// Load anchors from sidecar
// ---------------------------------------------------------------------------

bool UArborLayoutSolver::LoadAnchorsForAsset(const FString& AssetPath, TArray<FAnchor>& OutAnchors)
{
	// Read metadata via ArborAnchorAnalyzer
	FString MetaJson = UArborAnchorAnalyzer::GetAnchorMetadata(AssetPath);
	auto Meta = ParseJson(MetaJson);
	if (!Meta.IsValid() || !Meta->GetBoolField(TEXT("success")))
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* AnchorsArr;
	if (!Meta->TryGetArrayField(TEXT("anchors"), AnchorsArr))
	{
		return false;
	}

	for (const auto& AnchorVal : *AnchorsArr)
	{
		auto AnchorObj = AnchorVal->AsObject();
		if (!AnchorObj.IsValid()) continue;

		FAnchor A;
		A.Id = AnchorObj->GetStringField(TEXT("id"));

		const TSharedPtr<FJsonObject>* PosObj;
		if (AnchorObj->TryGetObjectField(TEXT("position"), PosObj))
		{
			A.Position = JsonToVec(*PosObj);
		}

		const TSharedPtr<FJsonObject>* DirObj;
		if (AnchorObj->TryGetObjectField(TEXT("direction"), DirObj))
		{
			A.Direction = JsonToVec(*DirObj);
		}

		OutAnchors.Add(A);
	}

	return OutAnchors.Num() > 0;
}

const UArborLayoutSolver::FAnchor* UArborLayoutSolver::FindAnchor(
	const TArray<FAnchor>& Anchors, const FString& AnchorId)
{
	for (const auto& A : Anchors)
	{
		if (A.Id == AnchorId)
		{
			return &A;
		}
	}
	return nullptr;
}

// ---------------------------------------------------------------------------
// Anchor alignment
// ---------------------------------------------------------------------------

UArborLayoutSolver::FNodeTransform UArborLayoutSolver::AlignNodes(
	const FNodeTransform& FromTransform, const FAnchor& FromAnchor,
	const FAnchor& ToAnchor, const FString& Relationship, double Gap,
	double NodeYawHint)
{
	FNodeTransform Result;
	Result.Scale = FVector(1, 1, 1);

	// Classify anchors as vertical or horizontal based on Z component
	bool bFromVertical = FMath::Abs(FromAnchor.Direction.Z) > 0.7;
	bool bToVertical = FMath::Abs(ToAnchor.Direction.Z) > 0.7;

	// FromAnchor direction in world space (only apply yaw from FromTransform)
	double FromYaw = FMath::DegreesToRadians(FromTransform.Rotation.Yaw);
	FVector FromDirWorld = FVector(
		FromAnchor.Direction.X * FMath::Cos(FromYaw) - FromAnchor.Direction.Y * FMath::Sin(FromYaw),
		FromAnchor.Direction.X * FMath::Sin(FromYaw) + FromAnchor.Direction.Y * FMath::Cos(FromYaw),
		FromAnchor.Direction.Z
	);

	double ToYaw;

	if (bFromVertical && bToVertical)
	{
		// Both vertical (e.g. surface_center -> snap_base): skip direction-matching,
		// use NodeYawHint for the target rotation
		ToYaw = NodeYawHint;
	}
	else if (!bFromVertical && bToVertical)
	{
		// From horizontal, To vertical (e.g. edge_north -> snap_base): use NodeYawHint
		ToYaw = NodeYawHint;
	}
	else
	{
		// Both horizontal: existing direction-matching logic
		FVector DesiredToDir = -FromDirWorld;
		double DesiredAngle = FMath::Atan2(DesiredToDir.Y, DesiredToDir.X);
		double AnchorAngle = FMath::Atan2(ToAnchor.Direction.Y, ToAnchor.Direction.X);
		ToYaw = FMath::RadiansToDegrees(DesiredAngle - AnchorAngle);
	}

	Result.Rotation = FRotator(0, ToYaw, 0);

	// From anchor position in world space
	FVector FromAnchorLocal = FVector(
		FromAnchor.Position.X * FMath::Cos(FromYaw) - FromAnchor.Position.Y * FMath::Sin(FromYaw),
		FromAnchor.Position.X * FMath::Sin(FromYaw) + FromAnchor.Position.Y * FMath::Cos(FromYaw),
		FromAnchor.Position.Z
	);
	FVector FromAnchorWorld = FromTransform.Location + FromAnchorLocal;

	// To anchor position rotated by ToYaw
	double ToYawRad = FMath::DegreesToRadians(ToYaw);
	FVector ToAnchorRotated = FVector(
		ToAnchor.Position.X * FMath::Cos(ToYawRad) - ToAnchor.Position.Y * FMath::Sin(ToYawRad),
		ToAnchor.Position.X * FMath::Sin(ToYawRad) + ToAnchor.Position.Y * FMath::Cos(ToYawRad),
		ToAnchor.Position.Z
	);

	if (Relationship == TEXT("adjacent"))
	{
		// Anchors touch: place so ToAnchor lands at FromAnchor
		Result.Location = FromAnchorWorld - ToAnchorRotated;
	}
	else if (Relationship == TEXT("facing"))
	{
		// Face each other with gap along FromAnchor's direction
		Result.Location = FromAnchorWorld + FromDirWorld * Gap - ToAnchorRotated;
	}
	else
	{
		// Default: same as adjacent
		Result.Location = FromAnchorWorld - ToAnchorRotated;
	}

	return Result;
}

// ---------------------------------------------------------------------------
// ResolveGraph
// ---------------------------------------------------------------------------

FString UArborLayoutSolver::ResolveGraph(const FString& GraphJson)
{
	auto Graph = ParseJson(GraphJson);
	if (!Graph.IsValid())
	{
		return TEXT("{\"success\":false,\"error\":\"Invalid graph JSON\"}");
	}

	// Parse origin
	FVector Origin = FVector::ZeroVector;
	const TSharedPtr<FJsonObject>* OriginObj;
	if (Graph->TryGetObjectField(TEXT("origin"), OriginObj))
	{
		Origin = JsonToVec(*OriginObj);
	}

	// Parse nodes
	const TSharedPtr<FJsonObject>* NodesObj;
	if (!Graph->TryGetObjectField(TEXT("nodes"), NodesObj))
	{
		return TEXT("{\"success\":false,\"error\":\"Missing 'nodes' in graph\"}");
	}

	struct FGraphNode
	{
		FString Id;
		FString AssetPath;
		TArray<FAnchor> Anchors;
		double Yaw = 0;
		double Scale = 1.0;
	};

	TMap<FString, FGraphNode> Nodes;
	for (const auto& Pair : (*NodesObj)->Values)
	{
		auto NodeObj = Pair.Value->AsObject();
		if (!NodeObj.IsValid()) continue;

		FGraphNode N;
		N.Id = Pair.Key;
		N.AssetPath = NodeObj->GetStringField(TEXT("asset_path"));

		// Optional transform hints
		const TSharedPtr<FJsonObject>* HintsObj;
		if (NodeObj->TryGetObjectField(TEXT("transform_hints"), HintsObj))
		{
			double TmpYaw;
			if ((*HintsObj)->TryGetNumberField(TEXT("yaw"), TmpYaw))
			{
				N.Yaw = TmpYaw;
			}
			double TmpScale;
			if ((*HintsObj)->TryGetNumberField(TEXT("scale"), TmpScale))
			{
				N.Scale = TmpScale;
			}
		}

		// Load anchors from sidecar
		if (!LoadAnchorsForAsset(N.AssetPath, N.Anchors))
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[ArborLayoutSolver] No anchor metadata for %s — run AnalyzeMesh first"),
				*N.AssetPath);
		}

		Nodes.Add(N.Id, MoveTemp(N));
	}

	if (Nodes.Num() == 0)
	{
		return TEXT("{\"success\":false,\"error\":\"No nodes in graph\"}");
	}

	// Parse edges
	struct FGraphEdge
	{
		FString FromNode;
		FString FromAnchor;
		FString ToNode;
		FString ToAnchor;
		FString Relationship;
		double Gap = 0;
	};

	TArray<FGraphEdge> Edges;
	const TArray<TSharedPtr<FJsonValue>>* EdgesArr;
	if (Graph->TryGetArrayField(TEXT("edges"), EdgesArr))
	{
		for (const auto& EdgeVal : *EdgesArr)
		{
			auto EdgeObj = EdgeVal->AsObject();
			if (!EdgeObj.IsValid()) continue;

			FGraphEdge E;

			const TSharedPtr<FJsonObject>* FromObj;
			if (EdgeObj->TryGetObjectField(TEXT("from"), FromObj))
			{
				E.FromNode = (*FromObj)->GetStringField(TEXT("node"));
				E.FromAnchor = (*FromObj)->GetStringField(TEXT("anchor"));
			}

			const TSharedPtr<FJsonObject>* ToObj;
			if (EdgeObj->TryGetObjectField(TEXT("to"), ToObj))
			{
				E.ToNode = (*ToObj)->GetStringField(TEXT("node"));
				E.ToAnchor = (*ToObj)->GetStringField(TEXT("anchor"));
			}

			E.Relationship = EdgeObj->GetStringField(TEXT("relationship"));

			const TSharedPtr<FJsonObject>* ParamsObj;
			if (EdgeObj->TryGetObjectField(TEXT("params"), ParamsObj))
			{
				(*ParamsObj)->TryGetNumberField(TEXT("gap"), E.Gap);
			}

			Edges.Add(E);
		}
	}

	// BFS: seed first node at origin
	TMap<FString, FNodeTransform> Transforms;

	// Build adjacency list
	TMap<FString, TArray<int32>> AdjList;
	for (int32 i = 0; i < Edges.Num(); i++)
	{
		AdjList.FindOrAdd(Edges[i].FromNode).Add(i);
		AdjList.FindOrAdd(Edges[i].ToNode).Add(i);
	}

	// Seed the first node
	TArray<FString> NodeIds;
	Nodes.GetKeys(NodeIds);
	const FString& SeedId = NodeIds[0];

	FNodeTransform SeedTransform;
	SeedTransform.Location = Origin;
	SeedTransform.Rotation = FRotator(0, Nodes[SeedId].Yaw, 0);
	SeedTransform.Scale = FVector(Nodes[SeedId].Scale);
	Transforms.Add(SeedId, SeedTransform);

	// BFS queue
	TQueue<FString> Queue;
	Queue.Enqueue(SeedId);

	while (!Queue.IsEmpty())
	{
		FString CurrentId;
		Queue.Dequeue(CurrentId);

		const TArray<int32>* EdgeIndices = AdjList.Find(CurrentId);
		if (!EdgeIndices) continue;

		for (int32 EdgeIdx : *EdgeIndices)
		{
			const FGraphEdge& Edge = Edges[EdgeIdx];

			// Determine which node is the neighbor
			FString NeighborId;
			FString MyAnchorId;
			FString TheirAnchorId;
			FString Relationship = Edge.Relationship;

			if (Edge.FromNode == CurrentId && !Transforms.Contains(Edge.ToNode))
			{
				NeighborId = Edge.ToNode;
				MyAnchorId = Edge.FromAnchor;
				TheirAnchorId = Edge.ToAnchor;
			}
			else if (Edge.ToNode == CurrentId && !Transforms.Contains(Edge.FromNode))
			{
				NeighborId = Edge.FromNode;
				MyAnchorId = Edge.ToAnchor;
				TheirAnchorId = Edge.FromAnchor;
			}
			else
			{
				continue; // Both already placed or same node
			}

			// Look up anchors
			const FGraphNode& MyNode = Nodes[CurrentId];
			const FGraphNode& TheirNode = Nodes[NeighborId];

			const FAnchor* MyAnchor = FindAnchor(MyNode.Anchors, MyAnchorId);
			const FAnchor* TheirAnchor = FindAnchor(TheirNode.Anchors, TheirAnchorId);

			if (!MyAnchor || !TheirAnchor)
			{
				UE_LOG(LogTemp, Warning,
					TEXT("[ArborLayoutSolver] Missing anchor: %s.%s or %s.%s"),
					*CurrentId, *MyAnchorId, *NeighborId, *TheirAnchorId);
				// Fallback: place at origin offset
				FNodeTransform Fallback;
				Fallback.Location = Transforms[CurrentId].Location + FVector(500, 0, 0);
				Fallback.Rotation = FRotator(0, TheirNode.Yaw, 0);
				Fallback.Scale = FVector(TheirNode.Scale);
				Transforms.Add(NeighborId, Fallback);
				Queue.Enqueue(NeighborId);
				continue;
			}

			FNodeTransform Aligned = AlignNodes(
				Transforms[CurrentId], *MyAnchor, *TheirAnchor,
				Relationship, Edge.Gap, TheirNode.Yaw);

			// Apply node-level hints
			Aligned.Scale = FVector(TheirNode.Scale);

			Transforms.Add(NeighborId, Aligned);
			Queue.Enqueue(NeighborId);
		}
	}

	// Place any disconnected nodes at offset from origin
	FVector DisconnectedOffset = Origin;
	for (const auto& Pair : Nodes)
	{
		if (!Transforms.Contains(Pair.Key))
		{
			DisconnectedOffset += FVector(500, 0, 0);
			FNodeTransform T;
			T.Location = DisconnectedOffset;
			T.Rotation = FRotator(0, Pair.Value.Yaw, 0);
			T.Scale = FVector(Pair.Value.Scale);
			Transforms.Add(Pair.Key, T);

			UE_LOG(LogTemp, Warning,
				TEXT("[ArborLayoutSolver] Node '%s' is disconnected, placed at fallback"),
				*Pair.Key);
		}
	}

	// Build result
	TSharedPtr<FJsonObject> TransformsObj = MakeShared<FJsonObject>();
	for (const auto& Pair : Transforms)
	{
		TSharedPtr<FJsonObject> T = MakeShared<FJsonObject>();
		T->SetObjectField(TEXT("location"), VecToJson(Pair.Value.Location));
		T->SetObjectField(TEXT("rotation"), RotToJson(Pair.Value.Rotation));
		T->SetObjectField(TEXT("scale"), VecToJson(Pair.Value.Scale));
		TransformsObj->SetObjectField(Pair.Key, T);
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetNumberField(TEXT("node_count"), Transforms.Num());
	Result->SetObjectField(TEXT("transforms"), TransformsObj);
	return SerializeJson(Result);
}
