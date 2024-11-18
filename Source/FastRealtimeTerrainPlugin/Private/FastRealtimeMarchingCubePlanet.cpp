


#include "FastRealtimeMarchingCubePlanet.h"
#include "DrawDebugHelpers.h"
#include "Kismet/KismetMathLibrary.h"

AFastRealtimeMarchingCubePlanet::AFastRealtimeMarchingCubePlanet()
{
	// Set tick values
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	
	// Try & set the default value for the triangulation table to the DT in the plugin folder
	static ConstructorHelpers::FObjectFinder<UDataTable>TriDT(TEXT("/FastRealtimeTerrainPlugin/MarchingCubes/Data/TriangulationTable.TriangulationTable"));
	if (TriDT.Succeeded())
	{
		TriangulationTable = TriDT.Object;
	}

	// Set the default VertexDirections array values, for checking vert positions outwards from each block position
	VertexDirections[0] = FVector3f(1.0f, 1.0f, 1.0f);
	VertexDirections[1] = FVector3f(-1.0f, 1.0f, 1.0f);
	VertexDirections[2] = FVector3f(-1.0f, -1.0f, 1.0f);
	VertexDirections[3] = FVector3f(1.0f, -1.0f, 1.0f);
	VertexDirections[4] = FVector3f(1.0f, 1.0f, -1.0f);
	VertexDirections[5] = FVector3f(-1.0f, 1.0f, -1.0f);
	VertexDirections[6] = FVector3f(-1.0f, -1.0f, -1.0f);
	VertexDirections[7] = FVector3f(1.0f, -1.0f, -1.0f);

	MarchingBounds = CreateDefaultSubobject<UBoxComponent>(TEXT("MarchingBounds"), false);
	MarchingBounds->SetupAttachment(GetRootComponent());
	MarchingBounds->SetCollisionProfileName(TEXT("NoCollision"));
	MarchingBounds->SetBoxExtent(FVector(PlanetSize * 0.5f), false);
	MarchingBounds->SetIgnoreBoundsForEditorFocus(false);
}

void AFastRealtimeMarchingCubePlanet::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const FDateTime LastCacheTime = FDateTime::Now();
	bool BuildTimeExceeded = false;

	while (PendingTerrainChunks.Num() != 0 && BuildTimeExceeded == false)
	{
		// Generate the first chunk in the stack
		GenerateTerrainChunk(PendingTerrainChunks[0]);

		// Move the generated chunk to the end of the stack & then remove it, shrinking the stack down
		PendingTerrainChunks.Swap(0, PendingTerrainChunks.Num() - 1);
		PendingTerrainChunks.Pop();

		// Check if the process of generating that chunk exceeded the per-fame chunk build time budget, if so, delay remaining chunks to future frames
		if ((FDateTime::Now() - LastCacheTime).GetTotalMilliseconds() > BuildChunkTimeBudget)
		{
			BuildTimeExceeded = true;
		}
	}
}

void AFastRealtimeMarchingCubePlanet::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	MarchingBounds->SetBoxExtent(FVector(PlanetSize * 0.5f), false);

	if (BuildOnConstruct)
	{
		GenerateMesh();
		//GenerateMeshDeferred();
	}
}

void AFastRealtimeMarchingCubePlanet::GenerateMesh()
{
	ClearGeneratedMesh();

	const FVector3f InitialOffsetPosition = FVector3f(PlanetSize * -0.5f);
	const float StepSize = PlanetSize / CubeRes;
	const float PerCubeHalfSize = StepSize * 0.5f;

	if (bRandomSeed) { Seed = UKismetMathLibrary::RandomInteger(2147483647); }

	const bool bUseNoise = NoiseLayers.Num() > 0;
	TArray<UFastNoiseWrapper*> NoiseWrappers;
	if (bUseNoise)
	{
		UFastNoiseLayeringFunctions::InitNoiseWrappers(this, NoiseWrappers, NoiseLayers, Seed, NoiseScaleOV);
	}

	if (DrawDebugCubeEdges || DrawDebugCubeVerts)
	{
		FlushPersistentDebugLines(GetWorld());
	}

	FDateTime ScalarGatherStartTime = FDateTime::Now();

	// Initialize Point Values Array for use in the below loop
	TArray<float> PointValues;
	PointValues.SetNumUninitialized(8);
	
	// Loop through XYZ grid cube vert positions for scalar field values
	for (int32 Z = 0; Z < CubeRes; Z++)
	{
		for (int32 Y = 0; Y < CubeRes; Y++)
		{
			for (int32 X = 0; X < CubeRes; X++)
			{
				
				// Store Current Cube Position
				FVector3f CurrentCubePosition = InitialOffsetPosition + FVector3f(PerCubeHalfSize) + FVector3f(StepSize * X, StepSize * Y, StepSize * Z);

				// Optional Debugging
				if (DrawDebugCubeEdges)
				{
					DrawDebugBox(
						GetWorld(),
						UKismetMathLibrary::TransformLocation(GetActorTransform(),FVector(CurrentCubePosition)),
						FVector(PerCubeHalfSize),
						FColor(125, 125, 125, 255),
						false,
						5.0f,
						0,
						1.0f
						);
				}
				
				// Skip cube vert checks if past planet surface point
				if (FMath::Abs(CurrentCubePosition.Length()) - StepSize > PlanetSize * 0.5f)
				{
					continue;
				}
	
				// From each cube center, step out to each cube vertex & get a density value. For now just from a distance falloff, later w/ noise blend
				//for (FVector3f Direction : VertexDirections)
				for (int32 i = 0; i < 8; i++)
				{
					const FVector3f Direction = VertexDirections[i];
					const FVector3f VertPosition = CurrentCubePosition + (Direction * PerCubeHalfSize);
					const float NoiseValue = UFastNoiseLayeringFunctions::BlendNoises3D(FVector(VertPosition), NoiseWrappers, NoiseLayers) * NoiseDisplacementStrength;
					const float DistanceNormalized = (VertPosition.Size() + NoiseValue) / (PlanetSize * 0.5f);
					float VertValue = 1.0f;
					if (DistanceNormalized < SurfaceHeight)
					{
						VertValue = 0.0f;
					}
					PointValues[i] = VertValue;

					// Optional Debugging
					if (DrawDebugCubeVerts)
					{
						FColor PointColor = FColor::Black;
						if (DistanceNormalized < SurfaceHeight)
						{
							PointColor = FColor::White;
						}
						DrawDebugPoint(
							GetWorld(),
							UKismetMathLibrary::TransformLocation(GetActorTransform(), FVector(VertPosition)),
							5.0f,
							PointColor,
							false,
							5.0f
							);
					}
				}
	
				// Add cube center & cube vert scalar values to CubeData
				FCubeData CD;
				CD.CubePosition = CurrentCubePosition;
				CD.VertexValues = PointValues;
				CubeData.CubeDatum.Add(CD);
			}
		}
	}

	const int32 ScalarGatherTime = (FDateTime::Now() - ScalarGatherStartTime).GetTotalMilliseconds();
	//UE_LOG(LogTemp, Log, TEXT("%i point Scalar Field Gather took %i ms"), CubeRes * CubeRes * CubeRes * 4,ScalarGatherTime);

	// Initialize Realtime Mesh & Streams// Initialize Realtime Mesh Simple
	URealtimeMeshSimple* RTM = GetRealtimeMeshComponent()->InitializeRealtimeMesh<URealtimeMeshSimple>();

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
	TRealtimeMeshStreamBuilder<TIndex3<uint32>, TIndex3<uint32>> TrianglesBuilder(
		StreamSet.AddStream(FRealtimeMeshStreams::Triangles, GetRealtimeMeshBufferLayout<TIndex3<uint32>>()));

	// Set up a stream for polygroups
	TRealtimeMeshStreamBuilder<uint32, uint16> PolygroupsBuilder(
	StreamSet.AddStream(FRealtimeMeshStreams::PolyGroups, GetRealtimeMeshBufferLayout<uint16>()));

	// Set up holdover flat tri array to track max tri index offsets as we cook tries along the way
	int32 MaxTri = 0;
	int32 ModCounter = 0;
	int32 CurrentMaxTri = MaxTri;
	TArray<FVector> Positions;

	InitializeTriangulationTableData();

	FDateTime TableDataLookupStartTime = FDateTime::Now();
	FTriangulationData TriangulationData;

	// Loop through all CubeData entries, get their triangulation data, and store it to our realtime mesh streams
	for (FCubeData& CD : CubeData.CubeDatum)
	{
		// Look for the data table index value, skip if it's invalid
		const int32 TriTableIndex = BinaryFromVertices(CD.VertexValues);
		if (TriTableIndex <= 0 || TriTableIndex >= 255)
		{
			continue;
		}

		// Grab triangulation data from DT
		GetTriangulationData(TriangulationData, TriTableIndex);

		for (int32 i = 0; i < TriangulationData.Vertices.Num(); i++)
		{
			// Vertex Positions
			FVector3f PO = (TriangulationData.Vertices[i] - 1.0f);
			FVector3f Position = (PO * PerCubeHalfSize);
			PositionBuilder.Add(CD.CubePosition - FVector3f(Position));

			// Normals Tangents
			FRealtimeMeshTangentsHighPrecision NT = FRealtimeMeshTangentsHighPrecision(TriangulationData.Normals[i], TriangulationData.Tangents[i]);
			TangentBuilder.Add(NT);

			// Vertex Colors
			ColorBuilder.Add(FColor::Black);

			// UVs
			TexCoordsBuilder.Add(FVector2DHalf(TriangulationData.UV0[i]));
		}
		
		for (int32 i = 0; i < TriangulationData.Triangles.Num(); i++)
		{
			
			// Triangles are tricky, they're in a flat array, so they must be done in groups of 3 every 3 entries
			if (ModCounter == 0)
			{
				TrianglesBuilder.Add(TIndex3<uint32>(TriangulationData.Triangles[i] + MaxTri, TriangulationData.Triangles[i + 1] + MaxTri, TriangulationData.Triangles[i + 2] + MaxTri));
				PolygroupsBuilder.Add(0);

				// Compare max tri values for incrementing after this loop
				if (TriangulationData.Triangles[i] + MaxTri > CurrentMaxTri)
				{
					CurrentMaxTri = TriangulationData.Triangles[i] + MaxTri;
				}
				if (TriangulationData.Triangles[i + 1] + MaxTri > CurrentMaxTri)
				{
					CurrentMaxTri = TriangulationData.Triangles[i + 1] + MaxTri;
				}
				if (TriangulationData.Triangles[i + 2] + MaxTri > CurrentMaxTri)
				{
					CurrentMaxTri = TriangulationData.Triangles[i + 2] + MaxTri;
				}

				// Increment in-loop "every-three" counter to avoid using modulo
				ModCounter += 1;
			}
			else
			{
				ModCounter += 1;
				if (ModCounter >= 3)
				{
					ModCounter = 0;
				}
			}
		}

		// Increment tri count index
		MaxTri = CurrentMaxTri;
		if (MaxTri > 0)
		{
			MaxTri += 1;
		}
	}

	const int32 TableDataLookupTime = (FDateTime::Now() - TableDataLookupStartTime).GetTotalMilliseconds();
	//UE_LOG(LogTemp, Log, TEXT("Table Data lookup took %i ms for %i CD instances"), TableDataLookupTime, CubeData.CubeDatum.Num());

	FDateTime MeshUpdateStartTime = FDateTime::Now();
	
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
	RTM->UpdateSectionConfig(PolyGroup0SectionKey, FRealtimeMeshSectionConfig(0), true);

	const int32 MeshUpdateTime = (FDateTime::Now() - MeshUpdateStartTime).GetTotalMilliseconds();
	//UE_LOG(LogTemp, Log, TEXT("Mesh Update took %i ms"), MeshUpdateTime);
	
}

void AFastRealtimeMarchingCubePlanet::GenerateMeshDeferred()
{

	ClearGeneratedMesh();
	InitializeTriangulationTableData();
	InitializeScalarField();
	
	// Loop through the volume component subdivisions, adding new components at each subdivision center
	const float StepSize = PlanetSize / ComponentBreakupScale;
	const float PerCompHalfSize = StepSize * 0.5f;
	const FVector StartingOffset = FVector(PlanetSize * -0.5f) + PerCompHalfSize;
	for (int32 Z = 0; Z < ComponentBreakupScale; Z++)
	{
		for (int32 Y = 0; Y < ComponentBreakupScale; Y++)
		{
			for (int32 X = 0; X < ComponentBreakupScale; X++)
			{
				const FVector CellOffset = StartingOffset + FVector(X * StepSize, Y * StepSize, Z * StepSize);
				PendingTerrainChunks.Add(CellOffset);
				if (DrawDebugCubeVerts)
				{
					DrawDebugBox(
					GetWorld(),
					UKismetMathLibrary::TransformLocation(GetActorTransform(), CellOffset),
					FVector(PerCompHalfSize),
					FColor::White,
					false,
					5.0f,
					0,
					1.0f
					);
				}
			}
		}
	}
}

void AFastRealtimeMarchingCubePlanet::GenerateTerrainChunk(const FVector TileCenter)
{

	// Initialize a new RealtimeMesh for each chunk
	URealtimeMeshComponent* NewMeshComp = NewObject<URealtimeMeshComponent>(this, URealtimeMeshComponent::StaticClass());
	NewMeshComp->RegisterComponent();
	NewMeshComp->SetCollisionProfileName("BlockAll");
	NewMeshComp->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::SnapToTargetIncludingScale);
	GeneratedMeshComps.Add(NewMeshComp);

	// Initialize realtime mesh simple
	URealtimeMeshSimple* NRTM = NewMeshComp->InitializeRealtimeMesh<URealtimeMeshSimple>();

	//const FVector3f InitialOffsetPosition = FVector3f(PlanetSize * -0.5f);
	const FVector3f InitialOffsetPosition = FVector3f(TileCenter - FVector((PlanetSize / ComponentBreakupScale) * 0.5f));
	if (DrawDebugCubeVerts)
	{
		DrawDebugSphere(
			GetWorld(),
			UKismetMathLibrary::TransformLocation(GetActorTransform(), FVector(InitialOffsetPosition)),
			50.0f,
			16,
			FColor::Magenta,
			false,
			5.0f,
			0,
			2.5f
			);
	}
	const float VolumeSize = PlanetSize / ComponentBreakupScale;
	const float StepSize = VolumeSize / PerCompRes;
	const float PerCubeHalfSize = StepSize * 0.5f;

	// Initialize Point Values Array for use in the below loop
	TArray<float> PointValues;
	PointValues.SetNumUninitialized(8);

	int32 index = -1;

	int32 ResOffset = 0;

	FDateTime ScalarGatherStartTime = FDateTime::Now();
	
	// Loop through XYZ grid cube vert positions for scalar field values
	for (int32 Z = 0; Z < PerCompRes - ResOffset; Z++)
	{
		for (int32 Y = 0; Y < PerCompRes - ResOffset; Y++)
		{
			for (int32 X = 0; X < PerCompRes - ResOffset; X++)
			{

				index++;
				
				// Store Current Cube Position
				FVector3f CurrentCubePosition = InitialOffsetPosition + PerCubeHalfSize + FVector3f(StepSize * X, StepSize * Y, StepSize * Z);

				// Optional Debugging
				if (DrawDebugCubeEdges)
				{
					DrawDebugBox(
						GetWorld(),
						UKismetMathLibrary::TransformLocation(GetActorTransform(),FVector(CurrentCubePosition)),
						FVector(PerCubeHalfSize),
						FColor(125, 125, 125, 255),
						false,
						5.0f,
						0,
						1.0f
						);
				}
				
				// Skip cube vert checks if past planet surface point
				if (FMath::Abs(CurrentCubePosition.Length()) - StepSize > PlanetSize * 0.5f)
				{
					continue;
				}
	
				// From each cube center, step out to each cube vertex & get a density value. For now just from a distance falloff, later w/ noise blend
				//for (FVector3f Direction : VertexDirections)
				for (int32 i = 0; i < 8; i++)
				{
					const FVector3f Direction = VertexDirections[i];
					const FVector3f VertPosition = CurrentCubePosition + (Direction * PerCubeHalfSize);
					const FVector NormalizedPosition = FVector(VertPosition) + (PlanetSize * 0.5f);
					//const int32 IndexX = FMath::FloorToInt(NormalizedPosition.X / (PlanetSize * 0.5f / (ComponentBreakupScale + PerCompRes + 1)));
					const int32 IndexX = FMath::FloorToInt(NormalizedPosition.X / (PlanetSize / (PerCompRes * ComponentBreakupScale)));
					//const int32 IndexY = FMath::FloorToInt(NormalizedPosition.Y / (PlanetSize * 0.5f / (ComponentBreakupScale + PerCompRes + 1))) * (PerCompRes * ComponentBreakupScale);
					const int32 IndexY = FMath::FloorToInt(NormalizedPosition.Y / (PlanetSize / (PerCompRes * ComponentBreakupScale))) * (PerCompRes * ComponentBreakupScale + 1);
					//const int32 IndexZ = FMath::FloorToInt(NormalizedPosition.Z / (PlanetSize * 0.5f / (ComponentBreakupScale + PerCompRes + 1))) * ((PerCompRes * ComponentBreakupScale) * (PerCompRes * ComponentBreakupScale));
					const int32 IndexZ = FMath::FloorToInt(NormalizedPosition.Z / (PlanetSize / (PerCompRes * ComponentBreakupScale))) * (PerCompRes * ComponentBreakupScale + 1) * (PerCompRes * ComponentBreakupScale + 1);
					const int32 LookupIndex = IndexX + IndexY + IndexZ;
					//UE_LOG(LogTemp, Log, TEXT("NormalizedPosition = %s | Index X = %i | Index Y = %i | Index Z = %i | LookupIndex = %i"), *NormalizedPosition.ToString(), IndexX, IndexY, IndexZ, LookupIndex);

					FColor DebugBoxColor = FColor::Orange;
					if (ScalarField.IsValidIndex(LookupIndex))
					{
						PointValues[i] = ScalarField[LookupIndex];
						DebugBoxColor = FColor(ScalarField[LookupIndex] * 255,ScalarField[LookupIndex] * 255,ScalarField[LookupIndex] * 255,ScalarField[LookupIndex] * 255);
						//UE_LOG(LogTemp, Log, TEXT("Found Scalar Value %f from key %i at position %s normalized position %s"), ScalarField[LookupIndex], LookupIndex, *FVector(VertPosition).ToString(), *FVector(NormalizedPosition).ToString());
					}
					else
					{
						//UE_LOG(LogTemp, Log, TEXT("Could not find Scalar Value from key %i at position %s normalized position %s"), LookupIndex, *FVector(VertPosition).ToString(), *FVector(NormalizedPosition).ToString());
					}
					if (DrawDebugCubeVerts)
					{
						DrawDebugBox(
							GetWorld(),
							UKismetMathLibrary::TransformLocation(GetActorTransform(), FVector(NormalizedPosition)),
							FVector(10.0f),
							DebugBoxColor,
							false,
							5.0f,
							0,
							2.0f
							);
					}
				}
	
				// Add cube center & cube vert scalar values to CubeData
				FCubeData CD;
				CD.CubePosition = CurrentCubePosition;
				CD.VertexValues = PointValues;
				CubeData.CubeDatum.Add(CD);
			}
		}
	}

	const int32 ScalarGatherTime = (FDateTime::Now() - ScalarGatherStartTime).GetTotalMilliseconds();
	UE_LOG(LogTemp, Log, TEXT("Scalar Field Gather took %i ms"), ScalarGatherTime);
	
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
	TRealtimeMeshStreamBuilder<TIndex3<uint32>, TIndex3<uint32>> TrianglesBuilder(
		StreamSet.AddStream(FRealtimeMeshStreams::Triangles, GetRealtimeMeshBufferLayout<TIndex3<uint32>>()));
	
	// Set up a stream for polygroups
	TRealtimeMeshStreamBuilder<uint32, uint16> PolygroupsBuilder(
	StreamSet.AddStream(FRealtimeMeshStreams::PolyGroups, GetRealtimeMeshBufferLayout<uint16>()));
	
	// Set up holdover flat tri array to track max tri index offsets as we cook tries along the way
	int32 MaxTri = 0;
	int32 ModCounter = 0;
	int32 CurrentMaxTri = MaxTri;
	TArray<FVector> Positions;
	
	FDateTime TableDataLookupStartTime = FDateTime::Now();
	FTriangulationData TriangulationData;
	
	// Loop through all CubeData entries, get their triangulation data, and store it to our realtime mesh streams
	for (FCubeData& CD : CubeData.CubeDatum)
	{
		// Look for the data table index value, skip if it's invalid
		const int32 TriTableIndex = BinaryFromVertices(CD.VertexValues);
		if (TriTableIndex <= 0 || TriTableIndex >= 255)
		{
			//UE_LOG(LogTemp, Log, TEXT("Could not find TriTableIndex %i"), TriTableIndex);
			continue;
		}
		else
		{
			//UE_LOG(LogTemp, Log, TEXT("Found TriTableIndex %i"), TriTableIndex);
		}
	
		// Grab triangulation data from DT
		GetTriangulationData(TriangulationData, TriTableIndex);
	
		for (int32 i = 0; i < TriangulationData.Vertices.Num(); i++)
		{
			// Vertex Positions
			FVector3f PO = (TriangulationData.Vertices[i] - 1.0f);
			FVector3f Position = (PO * PerCubeHalfSize);
			PositionBuilder.Add(CD.CubePosition - FVector3f(Position));
	
			// Normals Tangents
			FRealtimeMeshTangentsHighPrecision NT = FRealtimeMeshTangentsHighPrecision(TriangulationData.Normals[i], TriangulationData.Tangents[i]);
			TangentBuilder.Add(NT);
	
			// Vertex Colors
			ColorBuilder.Add(FColor::Black);
	
			// UVs
			TexCoordsBuilder.Add(FVector2DHalf(TriangulationData.UV0[i]));
		}
		
		for (int32 i = 0; i < TriangulationData.Triangles.Num(); i++)
		{
			
			// Triangles are tricky, they're in a flat array, so they must be done in groups of 3 every 3 entries
			if (ModCounter == 0)
			{
				TrianglesBuilder.Add(TIndex3<uint32>(TriangulationData.Triangles[i] + MaxTri, TriangulationData.Triangles[i + 1] + MaxTri, TriangulationData.Triangles[i + 2] + MaxTri));
				PolygroupsBuilder.Add(0);
	
				// Compare max tri values for incrementing after this loop
				if (TriangulationData.Triangles[i] + MaxTri > CurrentMaxTri)
				{
					CurrentMaxTri = TriangulationData.Triangles[i] + MaxTri;
				}
				if (TriangulationData.Triangles[i + 1] + MaxTri > CurrentMaxTri)
				{
					CurrentMaxTri = TriangulationData.Triangles[i + 1] + MaxTri;
				}
				if (TriangulationData.Triangles[i + 2] + MaxTri > CurrentMaxTri)
				{
					CurrentMaxTri = TriangulationData.Triangles[i + 2] + MaxTri;
				}
	
				// Increment in-loop "every-three" counter to avoid using modulo
				ModCounter += 1;
			}
			else
			{
				ModCounter += 1;
				if (ModCounter >= 3)
				{
					ModCounter = 0;
				}
			}
		}
	
		// Increment tri count index
		MaxTri = CurrentMaxTri;
		if (MaxTri > 0)
		{
			MaxTri += 1;
		}
	}
	
	const int32 TableDataLookupTime = (FDateTime::Now() - TableDataLookupStartTime).GetTotalMilliseconds();
	UE_LOG(LogTemp, Log, TEXT("Table Data lookup took %i ms for %i CD instances"), TableDataLookupTime, CubeData.CubeDatum.Num());

	// Don't update mesh section if no geo to update with
	if (MaxTri == 0)
	{
		return;
	}
	
	FDateTime MeshUpdateStartTime = FDateTime::Now();

	//UE_LOG(LogTemp, Log, TEXT("MaxTri = %i"), MaxTri);
	
	// Setup the material slot
	NRTM->SetupMaterialSlot(0, "PrimaryMaterial");
	
	// Setup the group key
	const FRealtimeMeshSectionGroupKey GroupKey = FRealtimeMeshSectionGroupKey::Create(0, FName("TestTriangle"));
	
	// Setup the section key
	const FRealtimeMeshSectionKey PolyGroup0SectionKey = FRealtimeMeshSectionKey::CreateForPolyGroup(GroupKey, 0);
	SectionKeys.Add(PolyGroup0SectionKey);
	
	// Now we create the section group
	NRTM->CreateSectionGroup(GroupKey, StreamSet);
	
	// Update the configuration of the polygroup section
	NRTM->UpdateSectionConfig(PolyGroup0SectionKey, FRealtimeMeshSectionConfig(0), true);
	
	const int32 MeshUpdateTime = (FDateTime::Now() - MeshUpdateStartTime).GetTotalMilliseconds();
	//UE_LOG(LogTemp, Log, TEXT("Mesh Update took %i ms"), MeshUpdateTime);

	Super::OnGenerateMesh_Implementation();
}

void AFastRealtimeMarchingCubePlanet::ClearGeneratedMesh()
{
	URealtimeMeshSimple* NullMesh = nullptr;

	GetRealtimeMeshComponent()->SetRealtimeMesh(NullMesh);
	CubeData.CubeDatum.Empty();
	SectionKeys.Empty();
	TriangulationTableDataInitialized = false;
	PendingTerrainChunks.Empty();
	ScalarField.Empty();
	for (URealtimeMeshComponent* GRMC : GeneratedMeshComps)
	{
		if (GRMC)
		{
			GRMC->SetRealtimeMesh(NullMesh);
			GRMC->DestroyComponent();
		}
	}
	GeneratedMeshComps.Empty();
}

int32 AFastRealtimeMarchingCubePlanet::BinaryFromVertices(TArray<float>& Vertices)
{

	int32 T = Vertices[0];
	int32 P = 2;
	for (int32 i = 1; i < 8; i++)
	{
		T += Vertices[i] * P;
		P *= 2;
	}
	
	return T;
}

void AFastRealtimeMarchingCubePlanet::GetTriangulationData(FTriangulationData& TriangulationData, const int32 Key)
{

	// If Triangulation Table Data isn't filled yet, fill it now
	if (!TriangulationTableDataInitialized)
	{
		InitializeTriangulationTableData();
	}

	// Validate key, if not valid try initializing triangulation table data and validate again, bail if still failing
	if (Key - 1 > TriangulationTableData.Num() - 1 || Key - 1 < 0)
	{
		if (!TriangulationTableDataInitialized)
		{
			InitializeTriangulationTableData();
		}
		if (Key - 1 > TriangulationTableData.Num() - 1 || Key - 1 < 0)
		{
			return;
		}
	}
	TriangulationData = TriangulationTableData[Key - 1];
}

void AFastRealtimeMarchingCubePlanet::InitializeTriangulationTableData()
{
	// If TriangulatinoTable isn't valid for whatever reason, bail
	if (!TriangulationTable)
	{
		return;
	}

	// Step through row names & get their data to store in a static array for quicker(?) access
	int32 i = 0;
	for (const FName RowName : TriangulationTable->GetRowNames())
	{
		TriangulationTableData[i] = *TriangulationTable->FindRow<FTriangulationData>(RowName, FString(), true);
		i++;
	}

	// Set TriangulationTableDataInitialized flag so we don't have to do this again unnecessarily
	TriangulationTableDataInitialized = true;
	UE_LOG(LogTemp, Log, TEXT("Initializing TriangulationTableData"));
}

void AFastRealtimeMarchingCubePlanet::InitializeScalarField()
{
	ScalarField.Empty();
	const int32 VertCount = ComponentBreakupScale * PerCompRes;
	const float StepSize = PlanetSize / VertCount;
	const FVector3f InitialOffsetPosition = FVector3f(PlanetSize * -0.5f);

	if (bRandomSeed) { Seed = UKismetMathLibrary::RandomInteger(2147483647); }

	const bool bUseNoise = NoiseLayers.Num() > 0;
	TArray<UFastNoiseWrapper*> NoiseWrappers;
	if (bUseNoise)
	{
		UFastNoiseLayeringFunctions::InitNoiseWrappers(this, NoiseWrappers, NoiseLayers, Seed, NoiseScaleOV);
	}

	int32 i = 0;

	for (int32 Z = 0; Z < VertCount + 1; Z++)
	{
		for (int32 Y = 0; Y < VertCount + 1; Y++)
		{
			for (int32 X = 0; X < VertCount + 1; X++)
			{
				// Store Current Cube Position
				FVector3f VertPosition = InitialOffsetPosition + FVector3f(StepSize * X, StepSize * Y, StepSize * Z);
				const float NoiseValue = UFastNoiseLayeringFunctions::BlendNoises3D(FVector(VertPosition), NoiseWrappers, NoiseLayers) * NoiseDisplacementStrength;
				const float DistanceNormalized = (VertPosition.Size() + NoiseValue) / (PlanetSize * 0.5f);
				float VertValue = 1.0f;
				if (DistanceNormalized < SurfaceHeight)
				{
					VertValue = 0.0f;
				}

				ScalarField.Add(VertValue);
				//UE_LOG(LogTemp, Log, TEXT("ScalarField Entry %i (X:%i Y%i Z%i) = %f at position %s"), i, X, Y, Z, VertValue, *VertPosition.ToString());
				i++;

				// Optional Debugging
				if (DrawDebugCubeVerts)
				{
					FColor PointColor = FColor::Black;
					if (DistanceNormalized < SurfaceHeight)
					{
						PointColor = FColor::White;
					}
					DrawDebugPoint(
						GetWorld(),
						UKismetMathLibrary::TransformLocation(GetActorTransform(), FVector(VertPosition)),
						5.0f,
						PointColor,
						false,
						5.0f
						);
				}
			}
		}
	}
	UE_LOG(LogTemp, Log, TEXT("ScalarField Initialized w/ %i entries"), ScalarField.Num());
}