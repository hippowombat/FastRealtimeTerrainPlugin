

#pragma once

#include "CoreMinimal.h"
#include "RealtimeMeshActor.h"
#include "RealtimeMeshSimple.h"
#include "RealtimeMeshComponent.h"
#include "FastNoiseLayeringFunctions.h"
#include "Components/BoxComponent.h"
#include "FastRealtimeMarchingCubePlanet.generated.h"

/**
 * 
 */

USTRUCT() struct FCubeData
{
	// Container for basic per-cube data, including vert values & cube center position
	
	GENERATED_BODY()

	UPROPERTY()
	FVector3f CubePosition;

	UPROPERTY()
	TArray<float> VertexValues;
	
};

USTRUCT() struct FVertexValues
{

	GENERATED_BODY()
	
	// Container for float array vert values in cube grid
	UPROPERTY()
	TArray<float> VertValues;
};

USTRUCT() struct FCubeDataContainer
{
	GENERATED_BODY()

	TArray<FCubeData> CubeDatum;
};

USTRUCT(BlueprintType) struct FTriangulationData : public FTableRowBase
{
	// Data structure for geometry triangulation for use in marching cubes algorithm with RMC

	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	TArray<FVector3f> Vertices;
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	TArray<int> Triangles;
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	TArray<FVector3f> Normals;
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	TArray<FVector2D> UV0;
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	TArray<FVector3f> Tangents;
	
};


UCLASS()
class FASTREALTIMETERRAINPLUGIN_API AFastRealtimeMarchingCubePlanet : public ARealtimeMeshActor
{
	GENERATED_BODY()

	AFastRealtimeMarchingCubePlanet();

	// Allows for EUW call to generate endless terrain from viewport position on tick
	virtual bool ShouldTickIfViewportsOnly() const override { return true; };

	virtual void Tick(float DeltaSeconds) override;

	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

	virtual void BeginPlay() override;

public:

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Meta = (AllowPrivateAccess = "true"))
	UBoxComponent* MarchingBounds;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Terrain")
	UDataTable* TriangulationTable;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Terrain", meta = (UIMin = 100.0f, UIMax = 10000.0f))
	float PlanetSize = 100.0f;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Terrain", meta = (UIMin = 2, UIMax = 100))
	int32 ComponentBreakupScale = 4;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Terrain", meta = (UIMin = 3, UIMax = 100))
	int32 PerCompRes = 5;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Terrain", meta = (UIMin = 3, UIMax = 100))
	int32 CubeRes = 5;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Terrain", meta = (UIMin = 0, UIMax = 100))
	int32 BuildChunkTimeBudget = 2;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Terrain", meta = (UIMin = 0.0f, UIMax = 1.0f))
	float SurfaceHeight = 0.5f;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Terrain")
	bool BuildOnConstruct = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Terrain")
	bool DoCollision = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Terrain")
	bool DebugOnlyDrawOneChunk = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Terrain")
	int32 SingleChunk = 0;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Terrain")
	bool DrawDebugCubeEdges = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Terrain")
	bool DrawDebugCubeVerts = false;

	// Deterministic random generation seed
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Terrain")
	int Seed = -1;

	// Whether to assign a stochastic random seed for deterministic stream/seed
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Terrain")
	bool bRandomSeed = false;

	// Scale multiplier for noise values, noise is very high frequency by default, higher values = more coarse detail
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Terrain", meta = (UIMin = 1, UIMax = 1000))
	float NoiseScaleOV = 1000.0f;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Terrain", meta = (UIMin = 1, UIMax = 100))
	float NoiseDisplacementStrength = 0.0f;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Terrain")
	TArray<FFN_NoiseLayerType> NoiseLayers;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Terrain|Stats")
	int64 TotalTriCount;

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Terrain")
	void GenerateMesh();

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Terrain")
	void GenerateMeshDeferred();

	// Calls function to build single terrain chunk
	UFUNCTION(BlueprintCallable, Category = "Terrain")
	void GenerateTerrainChunk(const FVector TileCenter = FVector(0.0f), bool Update = false);

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Terrain")
	void ClearGeneratedMesh();

	UFUNCTION(BlueprintCallable, Category = "Terrain")
	static void AffectPlanetGeo(AFastRealtimeMarchingCubePlanet* PlanetRef, FVector EffectLocation, float EffectRadius, bool AddTo);

private:

	UPROPERTY()
	FCubeDataContainer CubeData;

	UPROPERTY()
	bool TriangulationTableDataInitialized = false;

	UPROPERTY()
	TArray<FVector> PendingTerrainChunks;

	UPROPERTY()
	TArray<float> ScalarField;

	UPROPERTY()
	TMap<URealtimeMeshComponent*, FVector> GeneratedMeshComps;
	
	TStaticArray<FTriangulationData, 254> TriangulationTableData;
	
	TStaticArray<FVector3f, 8> VertexDirections;

	// Section keys
	TArray<FRealtimeMeshSectionKey> SectionKeys;
	
	static int32 BinaryFromVertices(TArray<float>& Vertices);

	int32 ScalarIndexLookupFromLocalLocation(FVector LocalLocation) const;
	
	void GetTriangulationData(FTriangulationData& TriangulationData, const int32 Key);

	void InitializeTriangulationTableData();

	void InitializeScalarField();
	
};
