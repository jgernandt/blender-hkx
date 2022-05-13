
//////////////////
// Product inclusion
//////////////////

#undef HK_FEATURE_PRODUCT_AI
#undef HK_FEATURE_PRODUCT_CLOTH
#undef HK_FEATURE_PRODUCT_DESTRUCTION
#undef HK_FEATURE_PRODUCT_BEHAVIOR

//////////////////
// Library exclusion
//////////////////

	// Animation
//#define HK_EXCLUDE_LIBRARY_hkaRagdoll

	// Physics
// #define HK_EXCLUDE_LIBRARY_hkpUtilities
#define HK_EXCLUDE_LIBRARY_hkpVehicle

	// Common
// #define HK_EXCLUDE_LIBRARY_hkSceneData
// #define HK_EXCLUDE_LIBRARY_hkVisualize
// #define HK_EXCLUDE_LIBRARY_hkGeometryUtilities
// #define HK_EXCLUDE_LIBRARY_hkCompat

	// Convex Decomposition
#define HK_EXCLUDE_LIBRARY_hkgpConvexDecomposition

//////////////////
// Individual feature exclusion
//////////////////

//#define HK_EXCLUDE_FEATURE_SerializeDeprecatedPre700
// #define HK_EXCLUDE_FEATURE_RegisterVersionPatches
// #define HK_EXCLUDE_FEATURE_RegisterReflectedClasses
 #define HK_EXCLUDE_FEATURE_MemoryTracker
#define HK_EXCLUDE_FEATURE_ConvexDecomposition


//#define HK_EXCLUDE_FEATURE_hkMonitorStream
#define HK_EXCLUDE_FEATURE_hkpAabbTreeWorldManager
#define HK_EXCLUDE_FEATURE_hkpAccurateInertiaTensorComputer
//#define HK_EXCLUDE_FEATURE_hkpCompressedMeshShape
#define HK_EXCLUDE_FEATURE_hkpContinuousSimulation
#define HK_EXCLUDE_FEATURE_hkpConvexPieceMeshShape
#define HK_EXCLUDE_FEATURE_hkpExtendedMeshShape
#define HK_EXCLUDE_FEATURE_hkpHeightField
#define HK_EXCLUDE_FEATURE_hkpKdTreeWorldManager
#define HK_EXCLUDE_FEATURE_hkpMeshShape
#define HK_EXCLUDE_FEATURE_hkpMultiThreadedSimulation
//#define HK_EXCLUDE_FEATURE_hkpPoweredChainData
#define HK_EXCLUDE_FEATURE_hkpSimpleMeshShape
#define HK_EXCLUDE_FEATURE_hkpSimulation
