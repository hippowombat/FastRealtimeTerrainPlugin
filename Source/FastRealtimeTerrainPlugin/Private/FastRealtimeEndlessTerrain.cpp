


#include "FastRealtimeEndlessTerrain.h"
#include "DrawDebugHelpers.h"
#include "Kismet/KismetMathLibrary.h"

AFastRealtimeEndlessTerrain::AFastRealtimeEndlessTerrain()
{
	// Set tick values
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
}

void AFastRealtimeEndlessTerrain::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const FDateTime LastCacheTime = FDateTime::Now();
	bool BuildTimeExceeded = false;

	while (PendingTerrainTiles.Num() != 0 && BuildTimeExceeded == false)
	{
		// Generate the terrain tile
		GenerateTerrainTile(FVector(PendingTerrainTiles[0].X, PendingTerrainTiles[0].Y, 0.0f));
		
		// Move the generated index to the end of the array, remove it & resize the array to shrink down one entry
		PendingTerrainTiles.Swap(0, PendingTerrainTiles.Num() - 1);
		PendingTerrainTiles.Pop();

		// Check if the time it took to generate this tile exceeds a threshold, if so then break the loop and wait
		// for next tick to continue generating more tiles
		if ((FDateTime::Now() - LastCacheTime).GetTotalMilliseconds() > TileBuildTimeBudget)
		{
			BuildTimeExceeded = true;
		}
	}
}

void AFastRealtimeEndlessTerrain::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	//GenerateTerrain();
}

void AFastRealtimeEndlessTerrain::GenerateTerrain()
{

	// Clear any previously generated data
	ClearTerrain();
	
	// Initialize Realtime Mesh Simple
	RTM = GetRealtimeMeshComponent()->InitializeRealtimeMesh<URealtimeMeshSimple>();

	// Initialize StreamSet. If RealtimeMeshSimple = DynamicMeshComponent, then StreamSet = DynamicMesh object
	FRealtimeMeshStreamSet StreamSet;

	// Set up a stream for vertex positions
	TRealtimeMeshStreamBuilder<FVector3f> PositionBuilder(
		StreamSet.AddStream(FRealtimeMeshStreams::Position, GetRealtimeMeshBufferLayout<FVector3f>()));

	// Set up a stream for tangents
	TRealtimeMeshStreamBuilder<FRealtimeMeshTangentsHighPrecision, FRealtimeMeshTangentsNormalPrecision> TangentBuilder(
		StreamSet.AddStream(FRealtimeMeshStreams::Tangents, GetRealtimeMeshBufferLayout<FRealtimeMeshTangentsNormalPrecision>()));

	// Set up a stream for texcoords
	TRealtimeMeshStreamBuilder<FVector2f, FVector2DHalf> TexCoordsBuilder(
		StreamSet.AddStream(FRealtimeMeshStreams::TexCoords, GetRealtimeMeshBufferLayout<FVector2DHalf>()));

	// Set up a stream for vertex colors
	TRealtimeMeshStreamBuilder<FColor> ColorBuilder(
		StreamSet.AddStream(FRealtimeMeshStreams::Color, GetRealtimeMeshBufferLayout<FColor>()));

	// Set up a stream for tris
	TRealtimeMeshStreamBuilder<TIndex3<uint32>, TIndex3<uint16>> TrianglesBuilder(
		StreamSet.AddStream(FRealtimeMeshStreams::Triangles, GetRealtimeMeshBufferLayout<TIndex3<uint16>>()));

	// Set up a stream for polygroups
	TRealtimeMeshStreamBuilder<uint32, uint16> PolygroupsBuilder(
		StreamSet.AddStream(FRealtimeMeshStreams::PolyGroups, GetRealtimeMeshBufferLayout<uint16>()));

	// Calculate reserve counts based on terrain resolution. 0 = 4 verts, 2 tris; 1 = 9 verts, 8 tris; 
	const int32 VertReserveCount = TerrainRes + 1;//(TerrainRes + 2) * (TerrainRes + 2);
	const int32 TriReserveCount = (VertReserveCount - 1);//((TerrainRes + 1) * (TerrainRes + 1) * 2);

	// Reserve space in buffers
	PositionBuilder.Reserve(VertReserveCount);
	TangentBuilder.Reserve(VertReserveCount);
	ColorBuilder.Reserve(VertReserveCount);
	TexCoordsBuilder.Reserve(VertReserveCount);
	TrianglesBuilder.Reserve(TriReserveCount);
	PolygroupsBuilder.Reserve(1);

	// Calculate step size between each vertex
	const float StepSize = TerrainSize / (TerrainRes);

	// Calculate offset position from center
	const FVector3f ExtentOffsetPosition = FVector3f((TerrainSize * -0.5f), (TerrainSize * -0.5f), 0.0f);

	// Initialize noise for terrain displacement on Z
	const bool bUseNoise = NoiseLayers.Num() > 0;
	TArray<UFastNoiseWrapper*> NoiseWrappers;

	if (bUseNoise)
	{
		UFastNoiseLayeringFunctions::InitNoiseWrappers(this, NoiseWrappers, NoiseLayers, Seed, NoiseScaleOV);
	}

	// Nested XY loop to generate terrain data, store it to the stream sets
	for (int32 Y = 0; Y < VertReserveCount; Y++)
	{
		for (int32 X = 0; X < VertReserveCount; X++)
		{
			// Vert positionXY = corner position + step size * vert row & column
			const FVector2D VertPosXY = FVector2D(ExtentOffsetPosition.X, ExtentOffsetPosition.Y) + FVector2D(StepSize * X, StepSize * Y);

			// Vert height = 0 by default, optionally offset if noise layers are configured
			float VertPosZ = 0.0f;
			if (bUseNoise)
			{
				VertPosZ = UFastNoiseLayeringFunctions::BlendNoises
				(
					FVector(VertPosXY.X, VertPosXY.Y, 0.0f) + GetActorLocation(), FVector(0.0f), NoiseWrappers, NoiseLayers
				);
				VertPosZ *= TerrainDepth;

				// If smoothing alpha is not 0, check neighboring vert heights on X+- and Y+- and get the average, then blend the current vert Z w/ the neighboring average using lerp
				if (SmoothingAlpha > 0)
				{
					// Neighbor X Positive
					float VertPosZN1 = UFastNoiseLayeringFunctions::BlendNoises
					(
						FVector(VertPosXY.X + (StepSize * SmoothingSteps), VertPosXY.Y, 0.0f) + GetActorLocation(), FVector(0.0f), NoiseWrappers, NoiseLayers
					);
					VertPosZN1 *= TerrainDepth;

					// Neighbor X Negative
					float VertPosZN2 = UFastNoiseLayeringFunctions::BlendNoises
					(
						FVector(VertPosXY.X - (StepSize * SmoothingSteps), VertPosXY.Y, 0.0f) + GetActorLocation(), FVector(0.0f), NoiseWrappers, NoiseLayers
					);
					VertPosZN2 *= TerrainDepth;

					// Neighbor Y Positive
					float VertPosZN3 = UFastNoiseLayeringFunctions::BlendNoises
					(
						FVector(VertPosXY.X, VertPosXY.Y + (StepSize * SmoothingSteps), 0.0f) + GetActorLocation(), FVector(0.0f), NoiseWrappers, NoiseLayers
					);
					VertPosZN3 *= TerrainDepth;

					// Neighbor Y Negative
					float VertPosZN4 = UFastNoiseLayeringFunctions::BlendNoises
					(
						FVector(VertPosXY.X, VertPosXY.Y - (StepSize * SmoothingSteps), 0.0f) + GetActorLocation(), FVector(0.0f), NoiseWrappers, NoiseLayers
					);
					VertPosZN4 *= TerrainDepth;

					// Get average of 4 neighbor height checks
					const float VertPosZNA = (VertPosZN1 + VertPosZN2 + VertPosZN3 + VertPosZN4) / 4;

					// Lerp smoothed height
					VertPosZ = UKismetMathLibrary::Lerp(VertPosZ, VertPosZNA, SmoothingAlpha);
				}
			}

			// Append VertPosZ to VertPosXY for final vert position
			const FVector3f VertPos = FVector3f(VertPosXY.X, VertPosXY.Y, VertPosZ);

			// Declare VertNormalTangent variable
			FRealtimeMeshTangentsHighPrecision VertNormalTangent;

			// If no noise, return straight-up facing normals
			if (!bUseNoise)
			{
				const FVector3f VertNormal = FVector3f(0.0f, 0.0f, 1.0f);
				const FVector3f VertTangent = FVector3f(1.0f, 0.0f, 0.0f);
				VertNormalTangent = FRealtimeMeshTangentsHighPrecision(VertNormal, VertTangent);
			}

			// Otherwise, calculate normals & tangents by sampling noise height at neighboring vert positions
			// While we are taking additional noise lookup costs per-vert, this should allow smooth normals between tile seams
			else
			{
				// Find neighboring XY positions
				const FVector2D NeighborPosX = FVector2D(VertPos.X, VertPos.Y) + FVector2D(StepSize, 0.0f);
				const FVector2D NeighborPosY = FVector2D(VertPos.X, VertPos.Y) + FVector2D(0.0f, StepSize);

				// Find height at neighboring XY positions
				float NeighborHeightX = TerrainDepth * UFastNoiseLayeringFunctions::BlendNoises
				(
					FVector(NeighborPosX.X, NeighborPosX.Y, 0.0f) + GetActorLocation(), FVector(0.0f), NoiseWrappers, NoiseLayers
				);

				// If smoothing, get smoothed neighbor height
				if (SmoothingAlpha > 0)
				{
					// Neighbor X Positive
					float VertPosZN1 = UFastNoiseLayeringFunctions::BlendNoises
					(
						FVector(NeighborPosX.X + (StepSize * SmoothingSteps), NeighborPosX.Y, 0.0f) + GetActorLocation(), FVector(0.0f), NoiseWrappers, NoiseLayers
					);
					VertPosZN1 *= TerrainDepth;

					// Neighbor X Negative
					float VertPosZN2 = UFastNoiseLayeringFunctions::BlendNoises
					(
						FVector(NeighborPosX.X - (StepSize * SmoothingSteps), NeighborPosX.Y, 0.0f) + GetActorLocation(), FVector(0.0f), NoiseWrappers, NoiseLayers
					);
					VertPosZN2 *= TerrainDepth;

					// Neighbor Y Positive
					float VertPosZN3 = UFastNoiseLayeringFunctions::BlendNoises
					(
						FVector(NeighborPosX.X, NeighborPosX.Y + (StepSize * SmoothingSteps), 0.0f) + GetActorLocation(), FVector(0.0f), NoiseWrappers, NoiseLayers
					);
					VertPosZN3 *= TerrainDepth;

					// Neighbor Y Negative
					float VertPosZN4 = UFastNoiseLayeringFunctions::BlendNoises
					(
						FVector(NeighborPosX.X, NeighborPosX.Y - (StepSize * SmoothingSteps), 0.0f) + GetActorLocation(), FVector(0.0f), NoiseWrappers, NoiseLayers
					);
					VertPosZN4 *= TerrainDepth;

					// Get average of 4 neighbor height checks
					const float VertPosZNA = (VertPosZN1 + VertPosZN2 + VertPosZN3 + VertPosZN4) / 4;

					// Lerp smoothed height
					NeighborHeightX = UKismetMathLibrary::Lerp(NeighborHeightX, VertPosZNA, SmoothingAlpha);
				}

				// Find height at neighboring XY positions
				float NeighborHeightY = TerrainDepth * UFastNoiseLayeringFunctions::BlendNoises
				(
					FVector(NeighborPosY.X, NeighborPosY.Y, 0.0f) + GetActorLocation(), FVector(0.0f), NoiseWrappers, NoiseLayers
				);

				// If smoothing, get smoothed neighbor height
				if (SmoothingAlpha > 0)
				{
					// Neighbor X Positive
					float VertPosZN1 = UFastNoiseLayeringFunctions::BlendNoises
					(
						FVector(NeighborPosY.X + (StepSize * SmoothingSteps), NeighborPosY.Y, 0.0f) + GetActorLocation(), FVector(0.0f), NoiseWrappers, NoiseLayers
					);
					VertPosZN1 *= TerrainDepth;

					// Neighbor X Negative
					float VertPosZN2 = UFastNoiseLayeringFunctions::BlendNoises
					(
						FVector(NeighborPosY.X - (StepSize * SmoothingSteps), NeighborPosY.Y, 0.0f) + GetActorLocation(), FVector(0.0f), NoiseWrappers, NoiseLayers
					);
					VertPosZN2 *= TerrainDepth;

					// Neighbor Y Positive
					float VertPosZN3 = UFastNoiseLayeringFunctions::BlendNoises
					(
						FVector(NeighborPosY.X, NeighborPosY.Y + (StepSize * SmoothingSteps), 0.0f) + GetActorLocation(), FVector(0.0f), NoiseWrappers, NoiseLayers
					);
					VertPosZN3 *= TerrainDepth;

					// Neighbor Y Negative
					float VertPosZN4 = UFastNoiseLayeringFunctions::BlendNoises
					(
						FVector(NeighborPosY.X, NeighborPosY.Y - (StepSize * SmoothingSteps), 0.0f) + GetActorLocation(), FVector(0.0f), NoiseWrappers, NoiseLayers
					);
					VertPosZN4 *= TerrainDepth;

					// Get average of 4 neighbor height checks
					const float VertPosZNA = (VertPosZN1 + VertPosZN2 + VertPosZN3 + VertPosZN4) / 4;

					// Lerp smoothed height
					NeighborHeightY = UKismetMathLibrary::Lerp(NeighborHeightY, VertPosZNA, SmoothingAlpha);
				}

				// Calculate tangent vectors with proper grid spacing
				const FVector3f TangentX = FVector3f(StepSize, 0.0f, NeighborHeightX - VertPos.Z).GetUnsafeNormal();
				const FVector3f TangentY = FVector3f(0.0f, StepSize, NeighborHeightY - VertPos.Z).GetUnsafeNormal();

				// Calculate normal from cross product of tangents
				const FVector3f Normal = FVector3f::CrossProduct(TangentX, TangentY).GetSafeNormal();

				// Store tangent & normal to VertNormalTangent
				VertNormalTangent = FRealtimeMeshTangentsHighPrecision(Normal, TangentX);
			}

			// Vert color = dummy value for now, maybe tie color to separate noise layer setup for biomes later
			const FColor VertColor = FColor::Black;

			// Vert UV = XY / TerrainRes
			const FVector2DHalf VertUV = FVector2DHalf
			(
				UKismetMathLibrary::SafeDivide(float(X), float(VertReserveCount - 1)),
				UKismetMathLibrary::SafeDivide(float(Y), float(VertReserveCount - 1))
			);
			
			// Add this generated data to the stream sets
			PositionBuilder.Add(VertPos);
			TangentBuilder.Add(VertNormalTangent);
			ColorBuilder.Add(VertColor);
			TexCoordsBuilder.Add(VertUV);
		}
	}

	// Pack tris into RMC format, setup courtesy of Joseph James
	for (int32 Y = 0; Y < TriReserveCount; Y++)
	{
		for (int32 X = 0; X < TriReserveCount; X++)
		{
			// Calculate the index of the bottom left-corner of the current cell
			const int32 i = (Y * TriReserveCount) + Y + X;

			// First triangle (bottom-left corner of the quad)
			TrianglesBuilder.Add(TIndex3<uint32>(i, i + TriReserveCount + 1, i + 1));
			PolygroupsBuilder.Add(0);

			// Second triangle (top-right corner of the quad)
			TrianglesBuilder.Add(TIndex3<uint32>(i + 1, i + TriReserveCount + 1, i + TriReserveCount + 2));
			PolygroupsBuilder.Add(0);
		}
	}

	// Setup the material slot
	RTM->SetupMaterialSlot(0, "PrimaryMaterial");

	// Setup the group key
	const FRealtimeMeshSectionGroupKey GroupKey = FRealtimeMeshSectionGroupKey::Create(0, FName("TestTriangle"));

	// Setup the section key
	const FRealtimeMeshSectionKey PolyGroup0SectionKey = FRealtimeMeshSectionKey::CreateForPolyGroup(GroupKey, 0);
	SectionKeys.Add(PolyGroup0SectionKey);
	
	// Now we create the section group
	RTM->CreateSectionGroup(GroupKey, StreamSet);

	// Update the configuration of the polygroup section
	RTM->UpdateSectionConfig(PolyGroup0SectionKey, FRealtimeMeshSectionConfig(0), bDoCollision);
	
	//Super::OnGenerateMesh_Implementation();

}

void AFastRealtimeEndlessTerrain::GenerateTerrainTile(const FVector TileCenter)
{
	// Cache build start time for logging
	FDateTime StartTime = FDateTime::Now();

	// Initialize a new RuntimeMeshComponent for each tile
	URealtimeMeshComponent* NewMeshComp = NewObject<URealtimeMeshComponent>(this, URealtimeMeshComponent::StaticClass());
	NewMeshComp->RegisterComponent();
	NewMeshComp->SetCollisionProfileName("BlockAll");
	GeneratedMeshComps.Add(NewMeshComp);
	
	// Initialize Realtime Mesh Simple
	URealtimeMeshSimple* NRTM = NewMeshComp->InitializeRealtimeMesh<URealtimeMeshSimple>();

	// Set collision settings
	FRealtimeMeshCollisionConfiguration CollisionConfig = FRealtimeMeshCollisionConfiguration();
	CollisionConfig.bUseAsyncCook = true;
	CollisionConfig.bDeformableMesh = false;
	CollisionConfig.bUseComplexAsSimpleCollision = true;
	CollisionConfig.bMergeAllMeshes = true;
	NRTM->SetCollisionConfig(CollisionConfig);
		
	// Setup the material slot
	NRTM->SetupMaterialSlot(0, TEXT("TerrainMaterial"));

	// Set Material
	NewMeshComp->SetMaterial(0, TerrainMaterial);

	// Add tile position to built tiles array
	BuiltTerrainTiles.Add(FVector2D(TileCenter.X, TileCenter.Y));
	
	// Get polygroup ID from BuiltTerrainTiles num
	const uint16 TileID = BuiltTerrainTiles.Num() - 1;

	// Calculate offset position from center
	const FVector3f ExtentOffsetPosition = FVector3f((TerrainSize * -0.5f), (TerrainSize * -0.5f), 0.0f);

	// Initialize noise for terrain displacement on Z
	const bool bUseNoise = NoiseLayers.Num() > 0;
	TArray<UFastNoiseWrapper*> NoiseWrappers;

	// LOD Config
	NRTM->UpdateLODConfig(0, FRealtimeMeshLODConfig(0.9f));

	if (bUseNoise)
	{
		UFastNoiseLayeringFunctions::InitNoiseWrappers(this, NoiseWrappers, NoiseLayers, Seed, NoiseScaleOV);
	}
	
	// Build SectionGroupKey name
	//FName SectionGroupKeyName = FName(TEXT("Section%i"), TileID);
	FName SectionGroupKeyName = FName("Mesh");

	// Generate mesh data per-LOD in a loop
	for (int32 LODIndex = 0; LODIndex < LOD_Count; LODIndex++)
	{

		// Initialize StreamSet. If RealtimeMeshSimple = DynamicMeshComponent, then StreamSet = DynamicMesh object
		FRealtimeMeshStreamSet StreamSet;

		// Set up a stream for vertex positions
		TRealtimeMeshStreamBuilder<FVector3f> PositionBuilder(
			StreamSet.AddStream(FRealtimeMeshStreams::Position, GetRealtimeMeshBufferLayout<FVector3f>()));

		// Set up a stream for tangents
		TRealtimeMeshStreamBuilder<FRealtimeMeshTangentsHighPrecision, FRealtimeMeshTangentsNormalPrecision> TangentBuilder(
			StreamSet.AddStream(FRealtimeMeshStreams::Tangents, GetRealtimeMeshBufferLayout<FRealtimeMeshTangentsNormalPrecision>()));

		// Set up a stream for texcoords
		TRealtimeMeshStreamBuilder<FVector2f, FVector2DHalf> TexCoordsBuilder(
			StreamSet.AddStream(FRealtimeMeshStreams::TexCoords, GetRealtimeMeshBufferLayout<FVector2DHalf>()));

		// Set up a stream for vertex colors
		TRealtimeMeshStreamBuilder<FColor> ColorBuilder(
			StreamSet.AddStream(FRealtimeMeshStreams::Color, GetRealtimeMeshBufferLayout<FColor>()));

		// Set up a stream for tris
		TRealtimeMeshStreamBuilder<TIndex3<uint32>, TIndex3<uint16>> TrianglesBuilder(
			StreamSet.AddStream(FRealtimeMeshStreams::Triangles, GetRealtimeMeshBufferLayout<TIndex3<uint16>>()));

		// Set up a stream for polygroups
		TRealtimeMeshStreamBuilder<uint32, uint16> PolygroupsBuilder(
		StreamSet.AddStream(FRealtimeMeshStreams::PolyGroups, GetRealtimeMeshBufferLayout<uint16>()));
		
		// Calculate Divisor for res
		int32 ResDivisor = (LODIndex + LOD_Breakdown_Count);
		if (LODIndex == 0) { ResDivisor = 1; }

		// Calculate reserve counts based on terrain resolution. 0 = 4 verts, 2 tris; 1 = 9 verts, 8 tris; 
		const int32 VertReserveCount = (TerrainRes + 1) / ResDivisor;
		const int32 TriReserveCount = (VertReserveCount - 1);

		// Reserve space in buffers
		PositionBuilder.Reserve(VertReserveCount);
		TangentBuilder.Reserve(VertReserveCount);
		ColorBuilder.Reserve(VertReserveCount);
		TexCoordsBuilder.Reserve(VertReserveCount);
		TrianglesBuilder.Reserve(TriReserveCount);
		PolygroupsBuilder.Reserve(1);

		// Setup LOD if necessary
		if (LODIndex > 0)
		{
			NRTM->AddLOD(FRealtimeMeshLODConfig(FMath::Pow(LOD_DistanceScale, LODIndex)));
		}

		// Calculate step size between each vertex
		float StepSize = TerrainSize / TriReserveCount;// * (LODIndex + (1 + (1 / (2 * (LODIndex + 1)))));
		if (LODIndex == 0) { StepSize = TerrainSize / TerrainRes; }
		
		// Nested XY loop to generate terrain data, store it to the stream sets
		for (int32 Y = 0; Y < VertReserveCount; Y++)
		{
			for (int32 X = 0; X < VertReserveCount; X++)
			{
				// Vert positionXY = corner position + step size * vert row & column
				const FVector2D VertPosXY = FVector2D(ExtentOffsetPosition.X, ExtentOffsetPosition.Y) + FVector2D(StepSize * X, StepSize * Y) + FVector2D(TileCenter.X, TileCenter.Y);

				//Vert height = 0 by default, optionally offset if noise layers are configured
				float VertPosZ = 0.0f;
				if (bUseNoise)
				{
					VertPosZ = UFastNoiseLayeringFunctions::BlendNoises
					(
						FVector(VertPosXY.X, VertPosXY.Y, 0.0f), FVector(0.0f), NoiseWrappers, NoiseLayers
					);
					VertPosZ *= TerrainDepth;

					// If smoothing alpha is not 0, check neighboring vert heights on X+- and Y+- and get the average, then blend the current vert Z w/ the neighboring average using lerp
					if (SmoothingAlpha > 0 && LODIndex == 0)
					{
						// Neighbor X Positive
						float VertPosZN1 = UFastNoiseLayeringFunctions::BlendNoises
						(
							FVector(VertPosXY.X + (StepSize * SmoothingSteps), VertPosXY.Y, 0.0f) + GetActorLocation(), FVector(0.0f), NoiseWrappers, NoiseLayers
						);
						VertPosZN1 *= TerrainDepth;

						// Neighbor X Negative
						float VertPosZN2 = UFastNoiseLayeringFunctions::BlendNoises
						(
							FVector(VertPosXY.X - (StepSize * SmoothingSteps), VertPosXY.Y, 0.0f) + GetActorLocation(), FVector(0.0f), NoiseWrappers, NoiseLayers
						);
						VertPosZN2 *= TerrainDepth;

						// Neighbor Y Positive
						float VertPosZN3 = UFastNoiseLayeringFunctions::BlendNoises
						(
							FVector(VertPosXY.X, VertPosXY.Y + (StepSize * SmoothingSteps), 0.0f) + GetActorLocation(), FVector(0.0f), NoiseWrappers, NoiseLayers
						);
						VertPosZN3 *= TerrainDepth;

						// Neighbor Y Negative
						float VertPosZN4 = UFastNoiseLayeringFunctions::BlendNoises
						(
							FVector(VertPosXY.X, VertPosXY.Y - (StepSize * SmoothingSteps), 0.0f) + GetActorLocation(), FVector(0.0f), NoiseWrappers, NoiseLayers
						);
						VertPosZN4 *= TerrainDepth;

						// Get average of 4 neighbor height checks
						const float VertPosZNA = (VertPosZN1 + VertPosZN2 + VertPosZN3 + VertPosZN4) / 4;

						// Lerp smoothed height
						VertPosZ = UKismetMathLibrary::Lerp(VertPosZ, VertPosZNA, SmoothingAlpha);
					}
				}

				// Append VertPosZ to VertPosXY for final vert position
				const FVector3f VertPos = FVector3f(VertPosXY.X, VertPosXY.Y, VertPosZ);

				// Declare VertNormalTangent variable
				FRealtimeMeshTangentsHighPrecision VertNormalTangent;

				// If no noise, return straight-up facing normals
				if (!bUseNoise)
				{
					const FVector3f VertNormal = FVector3f(0.0f, 0.0f, 1.0f);
					const FVector3f VertTangent = FVector3f(1.0f, 0.0f, 0.0f);
					VertNormalTangent = FRealtimeMeshTangentsHighPrecision(VertNormal, VertTangent);
				}

				// Otherwise, calculate normals & tangents by sampling noise height at neighboring vert positions
				// While we are taking additional noise lookup costs per-vert, this should allow smooth normals between tile seams
				else
				{
					// Find neighboring XY positions
					const FVector2D NeighborPosX = FVector2D(VertPos.X, VertPos.Y) + FVector2D(StepSize, 0.0f);
					const FVector2D NeighborPosY = FVector2D(VertPos.X, VertPos.Y) + FVector2D(0.0f, StepSize);

					// Find height at neighboring XY positions
					float NeighborHeightX = TerrainDepth * UFastNoiseLayeringFunctions::BlendNoises
					(
						FVector(NeighborPosX.X, NeighborPosX.Y, 0.0f),FVector(0.0f), NoiseWrappers, NoiseLayers
						);

					// If smoothing, get smoothed neighbor height
					if (SmoothingAlpha > 0 && LODIndex == 0)
					{
						// Neighbor X Positive
						float VertPosZN1 = UFastNoiseLayeringFunctions::BlendNoises
						(
							FVector(NeighborPosX.X + (StepSize * SmoothingSteps), NeighborPosX.Y, 0.0f) + GetActorLocation(), FVector(0.0f), NoiseWrappers, NoiseLayers
						);
						VertPosZN1 *= TerrainDepth;

						// Neighbor X Negative
						float VertPosZN2 = UFastNoiseLayeringFunctions::BlendNoises
						(
							FVector(NeighborPosX.X - (StepSize * SmoothingSteps), NeighborPosX.Y, 0.0f) + GetActorLocation(), FVector(0.0f), NoiseWrappers, NoiseLayers
						);
						VertPosZN2 *= TerrainDepth;

						// Neighbor Y Positive
						float VertPosZN3 = UFastNoiseLayeringFunctions::BlendNoises
						(
							FVector(NeighborPosX.X, NeighborPosX.Y + (StepSize * SmoothingSteps), 0.0f) + GetActorLocation(), FVector(0.0f), NoiseWrappers, NoiseLayers
						);
						VertPosZN3 *= TerrainDepth;

						// Neighbor Y Negative
						float VertPosZN4 = UFastNoiseLayeringFunctions::BlendNoises
						(
							FVector(NeighborPosX.X, NeighborPosX.Y - (StepSize * SmoothingSteps), 0.0f) + GetActorLocation(), FVector(0.0f), NoiseWrappers, NoiseLayers
						);
						VertPosZN4 *= TerrainDepth;

						// Get average of 4 neighbor height checks
						const float VertPosZNA = (VertPosZN1 + VertPosZN2 + VertPosZN3 + VertPosZN4) / 4;

						// Lerp smoothed height
						NeighborHeightX = UKismetMathLibrary::Lerp(NeighborHeightX, VertPosZNA, SmoothingAlpha);
					}

					// Find height at neighboring XY positions
					float NeighborHeightY = TerrainDepth * UFastNoiseLayeringFunctions::BlendNoises
					(
						FVector(NeighborPosY.X, NeighborPosY.Y, 0.0f), FVector(0.0f), NoiseWrappers, NoiseLayers
						);

					// If smoothing, get smoothed neighbor height
					if (SmoothingAlpha > 0 && LODIndex == 0)
					{
						// Neighbor X Positive
						float VertPosZN1 = UFastNoiseLayeringFunctions::BlendNoises
						(
							FVector(NeighborPosY.X + (StepSize * SmoothingSteps), NeighborPosY.Y, 0.0f) + GetActorLocation(), FVector(0.0f), NoiseWrappers, NoiseLayers
						);
						VertPosZN1 *= TerrainDepth;

						// Neighbor X Negative
						float VertPosZN2 = UFastNoiseLayeringFunctions::BlendNoises
						(
							FVector(NeighborPosY.X - (StepSize * SmoothingSteps), NeighborPosY.Y, 0.0f) + GetActorLocation(), FVector(0.0f), NoiseWrappers, NoiseLayers
						);
						VertPosZN2 *= TerrainDepth;

						// Neighbor Y Positive
						float VertPosZN3 = UFastNoiseLayeringFunctions::BlendNoises
						(
							FVector(NeighborPosY.X, NeighborPosY.Y + (StepSize * SmoothingSteps), 0.0f) + GetActorLocation(), FVector(0.0f), NoiseWrappers, NoiseLayers
						);
						VertPosZN3 *= TerrainDepth;

						// Neighbor Y Negative
						float VertPosZN4 = UFastNoiseLayeringFunctions::BlendNoises
						(
							FVector(NeighborPosY.X, NeighborPosY.Y - (StepSize * SmoothingSteps), 0.0f) + GetActorLocation(), FVector(0.0f), NoiseWrappers, NoiseLayers
						);
						VertPosZN4 *= TerrainDepth;

						// Get average of 4 neighbor height checks
						const float VertPosZNA = (VertPosZN1 + VertPosZN2 + VertPosZN3 + VertPosZN4) / 4;

						// Lerp smoothed height
						NeighborHeightY = UKismetMathLibrary::Lerp(NeighborHeightY, VertPosZNA, SmoothingAlpha);
					}

					// Calculate tangent vectors with proper grid spacing
					const FVector3f TangentX = FVector3f(StepSize, 0.0f, NeighborHeightX - VertPosZ).GetUnsafeNormal();
					const FVector3f TangentY = FVector3f(0.0f, StepSize, NeighborHeightY - VertPosZ).GetUnsafeNormal();

					// Calculate normal from cross product of tangents
					const FVector3f Normal = FVector3f::CrossProduct(TangentX, TangentY).GetUnsafeNormal();

					// Store tangent & normal to VertNormalTangent
					VertNormalTangent = FRealtimeMeshTangentsHighPrecision(Normal, TangentX);
				}

				// Vert color = dummy value for now, maybe tie color to separate noise layer setup for biomes later
				const FColor VertColor = FColor::Black;

				// Vert UV = XY / TerrainRes
				const FVector2DHalf VertUV = FVector2DHalf
				(
					UKismetMathLibrary::SafeDivide(float(X), float(VertReserveCount - 1)),
					UKismetMathLibrary::SafeDivide(float(Y), float(VertReserveCount - 1))
				);
				
				// Add this generated data to the stream sets
				PositionBuilder.Add(VertPos);
				TangentBuilder.Add(VertNormalTangent);
				ColorBuilder.Add(VertColor);
				TexCoordsBuilder.Add(VertUV);
			}
		}

		// Example of accessing builder data, use to populate custom triangle data struct laaaterrrr
		//const FVector3f Example = PositionBuilder.Get(0);
		//UE_LOG(LogTemp, Log, TEXT("Example = %s"), *Example.ToString());

		// Pack tris into RMC format, setup courtesy of Joseph James
		for (int32 Y = 0; Y < TriReserveCount; Y++)
		{
			for (int32 X = 0; X < TriReserveCount; X++)
			{
				// Calculate the index of the bottom left-corner of the current cell
				const int32 i = (Y * TriReserveCount) + Y + X;

				// First triangle (bottom-left corner of the quad)
				TrianglesBuilder.Add(TIndex3<uint32>(i, i + TriReserveCount + 1, i + 1));
				PolygroupsBuilder.Add(0);

				// Second triangle (top-right corner of the quad)
				TrianglesBuilder.Add(TIndex3<uint32>(i + 1, i + TriReserveCount + 1, i + TriReserveCount + 2));
				PolygroupsBuilder.Add(0);
			}
		}

		// Setup the group key
		const FRealtimeMeshSectionGroupKey GroupKey = FRealtimeMeshSectionGroupKey::Create(LODIndex, FName("Mesh"));

		// Setup the section key
		const FRealtimeMeshSectionKey PolyGroup0SectionKey = FRealtimeMeshSectionKey::CreateForPolyGroup(GroupKey, 0);
		
		// Now we create the section group
		NRTM->CreateSectionGroup(GroupKey, StreamSet);

		// Update the configuration of the polygroup section
		NRTM->UpdateSectionConfig(PolyGroup0SectionKey, FRealtimeMeshSectionConfig(0), bDoCollision && LODIndex == 0);
	}

	// Log tile generation time
	if (bLogTileTimes)
	{
		const int32 TileGenTime = (FDateTime::Now() - StartTime).GetTotalMilliseconds();
		UE_LOG(LogTemp, Log, TEXT("Tile %i Gen Time = %i"), TileID, TileGenTime);
	}
	
	Super::OnGenerateMesh_Implementation();
}

void AFastRealtimeEndlessTerrain::ClearTerrain()
{
	if(GetRealtimeMeshComponent()->HasBeenInitialized())
	{
		GetRealtimeMeshComponent()->GetRealtimeMesh()->Reset(false);
	}
	URealtimeMesh* EmptyMesh = nullptr;
	for (URealtimeMeshComponent* GRMC : GeneratedMeshComps)
	{
		GRMC->SetRealtimeMesh(EmptyMesh);
		GRMC->DestroyComponent();
	}
	GeneratedMeshComps.Empty();
	GetRealtimeMeshComponent()->SetRealtimeMesh(EmptyMesh);
	PendingTerrainTiles.Empty();
	BuiltTerrainTiles.Empty();
	SectionKeys.Empty();
}

void AFastRealtimeEndlessTerrain::UpdateObserverPosition(FVector ObserverLocation)
{
	// Snap position to discreet tile grid
	const FVector2D SnappedObserverLocation = SnapPositionToGrid(ObserverLocation);

	// Calculate starting tile range evaluation position
	const FVector2D StartingOffset = SnappedObserverLocation - FVector2D((TerrainSize * 0.5f) * (TileGenDepth - 1), (TerrainSize * 0.5f) * (TileGenDepth - 1));

	// Loop through local area tile cells, checking to see if they've already been generated, generating them & storing them if not
	for (int32 Y = 0; Y < TileGenDepth; Y++)
	{
		for (int32 X = 0; X < TileGenDepth; X++)
		{
			const FVector2D P = StartingOffset + FVector2D(X * TerrainSize, Y * TerrainSize);
			if (!BuiltTerrainTiles.Contains(P))
			{
				//GenerateTerrainTile(FVector(P.X, P.Y, 0.0f));
				if (!PendingTerrainTiles.Contains(P))
				{
					PendingTerrainTiles.Add(P);
				}
			}
		}
	}
}

FVector2D AFastRealtimeEndlessTerrain::SnapPositionToGrid(FVector DiscreetPosition) const
{
	return FVector2D(UKismetMathLibrary::Vector_SnappedToGrid(DiscreetPosition, TerrainSize));
}
