// ArborNoiseInternal.h — Shared noise helpers for ArborNoiseLibrary + LandscapeBuilder.
// Pure math, no UE5 dependencies beyond basic types.
#pragma once

#include "Math/UnrealMathUtility.h"

namespace ArborNoise
{

/** Integer hash for 2D grid coordinates. Returns float in [0.0, 1.0]. */
FORCEINLINE float Hash(int32 IX, int32 IY, int32 Seed)
{
	int32 N = IX + IY * 57 + Seed * 131;
	N = (N << 13) ^ N;
	N = (N * (N * N * 15731 + 789221) + 1376312589) & 0x7FFFFFFF;
	return static_cast<float>(N) / static_cast<float>(0x7FFFFFFF);
}

/** Hermite smoothstep: 3t^2 - 2t^3 */
FORCEINLINE float Smoothstep(float T)
{
	return T * T * (3.0f - 2.0f * T);
}

/** Single-octave value noise at floating-point coordinates. Returns [0.0, 1.0]. */
FORCEINLINE float ValueNoise2D(float X, float Y, int32 Seed)
{
	const int32 IX = FMath::FloorToInt32(X);
	const int32 IY = FMath::FloorToInt32(Y);
	const float FX = X - static_cast<float>(IX);
	const float FY = Y - static_cast<float>(IY);

	const float SX = Smoothstep(FX);
	const float SY = Smoothstep(FY);

	const float N00 = Hash(IX,     IY,     Seed);
	const float N10 = Hash(IX + 1, IY,     Seed);
	const float N01 = Hash(IX,     IY + 1, Seed);
	const float N11 = Hash(IX + 1, IY + 1, Seed);

	const float NX0 = FMath::Lerp(N00, N10, SX);
	const float NX1 = FMath::Lerp(N01, N11, SX);
	return FMath::Lerp(NX0, NX1, SY);
}

/** Fractal Brownian motion — sum of value noise octaves. Returns ~[0.0, 1.0]. */
inline float Fbm2D(float X, float Y, int32 Octaves, float Lacunarity, float Persistence, int32 Seed)
{
	float Total = 0.0f;
	float Amplitude = 1.0f;
	float Frequency = 1.0f;
	float MaxValue = 0.0f;

	for (int32 I = 0; I < Octaves; ++I)
	{
		Total += ValueNoise2D(X * Frequency, Y * Frequency, Seed + I * 31) * Amplitude;
		MaxValue += Amplitude;
		Amplitude *= Persistence;
		Frequency *= Lacunarity;
	}

	return MaxValue > 0.0f ? Total / MaxValue : 0.5f;
}

/** Ridged multifractal noise for mountain ridges. Returns ~[0.0, 1.0]. */
inline float RidgeNoise2D(float X, float Y, int32 Octaves, float Lacunarity, float Persistence, int32 Seed)
{
	float Total = 0.0f;
	float Amplitude = 1.0f;
	float Frequency = 1.0f;
	float MaxValue = 0.0f;
	float Prev = 1.0f;

	for (int32 I = 0; I < Octaves; ++I)
	{
		float N = ValueNoise2D(X * Frequency, Y * Frequency, Seed + I * 31);
		// Create ridges: 1.0 - abs(noise * 2 - 1)
		N = 1.0f - FMath::Abs(N * 2.0f - 1.0f);
		N = N * N;        // sharpen ridges
		N *= Prev;         // weight by previous octave for detail in valleys
		Prev = N;
		Total += N * Amplitude;
		MaxValue += Amplitude;
		Amplitude *= Persistence;
		Frequency *= Lacunarity;
	}

	return MaxValue > 0.0f ? Total / MaxValue : 0.5f;
}

} // namespace ArborNoise
