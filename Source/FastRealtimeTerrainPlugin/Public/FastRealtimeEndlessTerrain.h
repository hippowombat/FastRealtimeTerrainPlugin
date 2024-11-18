

#pragma once

#include "CoreMinimal.h"
#include "RealtimeMeshActor.h"
#include "RealtimeMeshSimple.h"
#include "FastNoiseLayeringFunctions.h"
#include "FastRealtimeEndlessTerrain.generated.h"

/**
 * 
 */


UCLASS()
class FASTREALTIMETERRAINPLUGIN_API AFastRealtimeEndlessTerrain : public ARealtimeMeshActor
{
	GENERATED_BODY()

	AFastRealtimeEndlessTerrain();

	// Allows for EUW call to generate endless terrain from viewport position on tick
	virtual bool ShouldTickIfViewportsOnly() const override { return true; };

	virtual void Tick(float DeltaSeconds) override;

	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

public:
	
	// BEGIN PUBLIC PARAMETERS //

	// Material to use on the terrain
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Terrain")
	UMaterialInterface* TerrainMaterial;

	// Size of the terrain on each axis
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Terrain")
	float TerrainSize = 10000.0f;

	// Number of subdivisions along each side
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Terrain", meta = (UIMin = 1, UIMax = 100, ClampMin = 1, ClampMax = 250))
	int TerrainRes = 10;

	// Magnitude of Z-Offset on verts multiplied by noise value at vert position
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Terrain", meta = (UIMin = 0, UIMax = 10000))
	float TerrainDepth = 100.0f;

	// Number of tiles out from observer position that tiles should generate on observer position update
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Terrain", meta = (UIMin = 1, UIMax = 10));
	uint8 TileGenDepth = 1;

	// Number of subdivisions along each side
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Terrain", meta = (UIMin = 1, UIMax = 100, ClampMin = 1, ClampMax = 100))
	uint8 TileBuildTimeBudget = 10;

	// Number of LODs
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Terrain", meta = (UIMin = 1, UIMax = 10));
	uint8 LOD_Count = 1;

	// LOD Breakdown Scale
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Terrain", meta = (UIMin = 1, UIMax = 5));
	uint8 LOD_Breakdown_Count = 1;

	// LOD Distance Scale
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Terrain", meta = (UIMin = 0.001f, UIMax = 0.8f, ClampMin = 0.001f, ClampMax = 0.8f));
	float LOD_DistanceScale = 0.5f;

	// Smoothing Alpha
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Terrain", meta = (UIMin = 0, UIMax = 1, ClampMin = 0, ClampMax = 1))
	float SmoothingAlpha = 0.0f;

	// Smoothing Steps - how wide the sample area for neighboring heights is
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Terrain", meta = (UIMin = 1, UIMax = 100, ClampMin = 1, ClampMax = 100))
	int32 SmoothingSteps = 1;

	// Deterministic random generation seed
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Terrain")
	int Seed = -1;

	// Whether to assign a stochastic random seed for deterministic stream/seed
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Terrain")
	bool bRandomSeed = false;

	// Scale multiplier for noise values, noise is very high frequency by default, higher values = more coarse detail
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Terrain", meta = (UIMin = 1, UIMax = 1000))
	float NoiseScaleOV = 1000.0f;

	// Whether or not to calculate collisions
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Terrain")
	bool bDoCollision = false;

	// Whether or not to log tile generation times
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Terrain")
	bool bLogTileTimes = false;

	// Definitions of noise layers and how they're blended. Blending operations happen linearly through layer entries from index 0 up.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Terrain|Noise")
	TArray<FFN_NoiseLayerType> NoiseLayers;

	// END PUBLIC PARAMETERS //
	
	// BEGIN PUBLIC FUNCTIONS //

	// Calls function to build terrain
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Terrain")
	void GenerateTerrain();

	// Calls function to build single terrain tile
	UFUNCTION(BlueprintCallable, Category = "Terrain")
	void GenerateTerrainTile(const FVector TileCenter = FVector(0.0f));

	// Calls function to reset terrain
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Terrain")
	void ClearTerrain();

	// Calls function to update observer location for endless terrain dev
	UFUNCTION(BlueprintCallable, Category = "Terrain")
	void UpdateObserverPosition(FVector ObserverLocation);

	// END PUBLIC FUNCTIONS //

	UPROPERTY()
	URealtimeMeshSimple* RTM = nullptr;

private:

	// Variable to track pending tiles
	TArray<FVector2D> PendingTerrainTiles;

	// Variable to track already built tiles
	TArray<FVector2D> BuiltTerrainTiles;

	// Section keys
	TArray<FRealtimeMeshSectionKey> SectionKeys;

	// Array of mesh comps
	UPROPERTY()
	TArray<URealtimeMeshComponent*> GeneratedMeshComps;

	// Function to snap observer position to grid
	FVector2D SnapPositionToGrid(FVector DiscreetPosition) const;
};
