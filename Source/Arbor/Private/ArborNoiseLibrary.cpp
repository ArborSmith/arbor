#include "ArborNoiseLibrary.h"
#include "ArborNoiseInternal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

// ============================================================================
// GenerateHeightmap
// ============================================================================

TArray<int32> UArborNoiseLibrary::GenerateHeightmap(
	int32 Width, int32 Height,
	float Frequency, float Amplitude, int32 Octaves,
	float Lacunarity, float Persistence, int32 Seed,
	float BaseHeight, const FString& NoiseType)
{
	const int32 Total = Width * Height;
	TArray<int32> Result;
	Result.SetNumUninitialized(Total);

	if (Total <= 0)
	{
		return Result;
	}

	const bool bRidge = NoiseType.Equals(TEXT("ridge"), ESearchCase::IgnoreCase);

	// BaseHeight is in float [0,1] space — convert to uint16 center
	const float Base = 32768.0f;
	const float Scale = 16384.0f;

	const float InvW = Frequency / FMath::Max(static_cast<float>(Width), 1.0f);
	const float InvH = Frequency / FMath::Max(static_cast<float>(Height), 1.0f);

	for (int32 Row = 0; Row < Height; ++Row)
	{
		const float NY = static_cast<float>(Row) * InvH;
		for (int32 Col = 0; Col < Width; ++Col)
		{
			const float NX = static_cast<float>(Col) * InvW;

			float Val;
			if (bRidge)
			{
				Val = ArborNoise::RidgeNoise2D(NX, NY, Octaves, Lacunarity, Persistence, Seed);
			}
			else
			{
				Val = ArborNoise::Fbm2D(NX, NY, Octaves, Lacunarity, Persistence, Seed);
			}

			// Map to [0,1] with base_height and amplitude
			Val = BaseHeight + (Val - 0.5f) * Amplitude;
			Val = FMath::Clamp(Val, 0.0f, 1.0f);

			// Convert to uint16 range
			int32 U16 = FMath::RoundToInt32(Base + (Val - 0.5f) * 2.0f * Scale);
			U16 = FMath::Clamp(U16, 0, 65535);

			Result[Row * Width + Col] = U16;
		}
	}

	return Result;
}

// ============================================================================
// GenerateRiverPath
// ============================================================================

namespace
{

/** Sample heightmap at fractional coords [0,1] with bilinear interpolation. */
float SampleHeightmap(const TArray<int32>& HeightData, int32 W, int32 H, float FX, float FY)
{
	const float PX = FX * static_cast<float>(W - 1);
	const float PY = FY * static_cast<float>(H - 1);
	const int32 IX = FMath::Clamp(FMath::FloorToInt32(PX), 0, W - 2);
	const int32 IY = FMath::Clamp(FMath::FloorToInt32(PY), 0, H - 2);
	const float FX2 = PX - static_cast<float>(IX);
	const float FY2 = PY - static_cast<float>(IY);

	const float N00 = static_cast<float>(HeightData[IY * W + IX]);
	const float N10 = static_cast<float>(HeightData[IY * W + IX + 1]);
	const float N01 = static_cast<float>(HeightData[(IY + 1) * W + IX]);
	const float N11 = static_cast<float>(HeightData[(IY + 1) * W + IX + 1]);

	return FMath::Lerp(
		FMath::Lerp(N00, N10, FX2),
		FMath::Lerp(N01, N11, FX2),
		FY2);
}

/** Simple seeded random [0,1]. Mutates Seed. */
float SeededRandom(int32& Seed)
{
	// LCG
	Seed = Seed * 1103515245 + 12345;
	return static_cast<float>((Seed & 0x7FFFFFFF) % 10000) / 10000.0f;
}

/** Seeded uniform [Min, Max]. */
float SeededUniform(int32& Seed, float Min, float Max)
{
	return Min + SeededRandom(Seed) * (Max - Min);
}

} // anonymous namespace

FString UArborNoiseLibrary::GenerateRiverPath(
	int32 Width, int32 Height,
	const TArray<int32>& HeightData,
	int32 NumPoints, const FString& StartEdge,
	int32 Seed, float Meander)
{
	if (HeightData.Num() != Width * Height || Width <= 0 || Height <= 0)
	{
		return TEXT("[]");
	}

	int32 RngSeed = Seed;
	const float Margin = 0.1f;

	// Determine start and end based on edge
	float SX, SY, EX, EY;
	if (StartEdge.Equals(TEXT("north"), ESearchCase::IgnoreCase))
	{
		SX = SeededUniform(RngSeed, 0.3f, 0.7f); SY = Margin;
		EX = SeededUniform(RngSeed, 0.3f, 0.7f); EY = 1.0f - Margin;
	}
	else if (StartEdge.Equals(TEXT("south"), ESearchCase::IgnoreCase))
	{
		SX = SeededUniform(RngSeed, 0.3f, 0.7f); SY = 1.0f - Margin;
		EX = SeededUniform(RngSeed, 0.3f, 0.7f); EY = Margin;
	}
	else if (StartEdge.Equals(TEXT("west"), ESearchCase::IgnoreCase))
	{
		SX = Margin; SY = SeededUniform(RngSeed, 0.3f, 0.7f);
		EX = 1.0f - Margin; EY = SeededUniform(RngSeed, 0.3f, 0.7f);
	}
	else // east
	{
		SX = 1.0f - Margin; SY = SeededUniform(RngSeed, 0.3f, 0.7f);
		EX = Margin; EY = SeededUniform(RngSeed, 0.3f, 0.7f);
	}

	// Walk from start to end, biasing toward lower terrain
	const int32 Steps = FMath::Max(NumPoints * 10, 50);

	struct FPoint { float X, Y; };
	TArray<FPoint> Path;
	Path.Reserve(Steps + 2);

	float CX = SX;
	float CY = SY;

	for (int32 Step = 0; Step < Steps; ++Step)
	{
		const float T = static_cast<float>(Step) / static_cast<float>(FMath::Max(Steps - 1, 1));
		Path.Add({CX, CY});

		// Target direction (toward endpoint)
		const float NextT = T + 1.0f / static_cast<float>(Steps);
		float TargetX = FMath::Lerp(SX, EX, NextT);
		float TargetY = FMath::Lerp(SY, EY, NextT);
		float DX = TargetX - CX;
		float DY = TargetY - CY;

		// Sample gradient — bias toward lower terrain
		const float GradStep = 0.02f;
		if (CX > 0.01f && CX < 0.99f && CY > 0.01f && CY < 0.99f)
		{
			const float HLeft  = SampleHeightmap(HeightData, Width, Height, CX - GradStep, CY);
			const float HRight = SampleHeightmap(HeightData, Width, Height, CX + GradStep, CY);
			const float HUp    = SampleHeightmap(HeightData, Width, Height, CX, CY - GradStep);
			const float HDown  = SampleHeightmap(HeightData, Width, Height, CX, CY + GradStep);
			const float GX = HLeft - HRight;
			const float GY = HUp - HDown;
			DX += GX * Meander * 0.5f;
			DY += GY * Meander * 0.5f;
		}

		// Add noise-based meander
		const float NoiseVal = ArborNoise::ValueNoise2D(CX * 5.0f, CY * 5.0f, Seed + 777) - 0.5f;
		float PerpX = -DY;
		float PerpY = DX;
		const float PerpLen = FMath::Sqrt(PerpX * PerpX + PerpY * PerpY);
		if (PerpLen > 1e-6f)
		{
			PerpX /= PerpLen;
			PerpY /= PerpLen;
		}
		DX += PerpX * NoiseVal * Meander * 0.3f;
		DY += PerpY * NoiseVal * Meander * 0.3f;

		// Normalize step length
		const float StepLen = 1.0f / static_cast<float>(Steps);
		const float DLen = FMath::Sqrt(DX * DX + DY * DY);
		if (DLen > 1e-6f)
		{
			CX += DX / DLen * StepLen;
			CY += DY / DLen * StepLen;
		}
		else
		{
			CX += StepLen * (EX > SX ? 1.0f : -1.0f);
		}

		CX = FMath::Clamp(CX, 0.02f, 0.98f);
		CY = FMath::Clamp(CY, 0.02f, 0.98f);
	}

	Path.Add({EX, EY});

	// Downsample to NumPoints
	TArray<FPoint> Result;
	if (Path.Num() <= NumPoints)
	{
		Result = Path;
	}
	else
	{
		Result.Reserve(NumPoints);
		for (int32 I = 0; I < NumPoints; ++I)
		{
			const int32 Idx = FMath::RoundToInt32(
				static_cast<float>(I) / static_cast<float>(FMath::Max(NumPoints - 1, 1))
				* static_cast<float>(Path.Num() - 1));
			Result.Add(Path[Idx]);
		}
	}

	// Serialize to JSON: [[x, y], ...]
	FString JsonStr = TEXT("[");
	for (int32 I = 0; I < Result.Num(); ++I)
	{
		if (I > 0) JsonStr += TEXT(",");
		JsonStr += FString::Printf(TEXT("[%.6f,%.6f]"), Result[I].X, Result[I].Y);
	}
	JsonStr += TEXT("]");

	return JsonStr;
}
