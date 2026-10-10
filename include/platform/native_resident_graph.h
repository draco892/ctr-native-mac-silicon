#ifndef PLATFORM_NATIVE_RESIDENT_GRAPH_H
#define PLATFORM_NATIVE_RESIDENT_GRAPH_H
#include <platform/native_resident.h>
struct NativeResidentGraph;
struct NativeResidentGraphError
{
	enum NativeResidentKind kind;
	u32 offset;
	size_t count;
	enum NativeAssetResult result;
};
// Owns copies of wire/PTR data and all resident objects. No four-byte pointer
// patching, partial publication or dependency on the original buffer lifetime.
// *out is unchanged on failure; free a previously owned graph explicitly.
enum NativeAssetResult NativeResidentGraph_Build(const struct NativePtrMapView *, enum NativeResidentKind rootKind, u32 rootOffset,
                                                 struct NativeResidentGraph **out, struct NativeResidentGraphError *);
void NativeResidentGraph_Free(struct NativeResidentGraph *);
void *NativeResidentGraph_Root(const struct NativeResidentGraph *);
size_t NativeResidentGraph_ObjectCount(const struct NativeResidentGraph *);
// Reopen a wire view for a resident pointer; arrays translate by element index.
int NativeResidentGraph_WireOffset(const struct NativeResidentGraph *, const void *, enum NativeResidentKind, u32 *);
const struct NativePtrMapView *NativeResidentGraph_Map(const struct NativeResidentGraph *);
typedef int (*NativeResidentGraphRebaseExternal)(void *, uintptr_t saved, void **live);
size_t NativeResidentGraph_CheckpointSize(const struct NativeResidentGraph *);
int NativeResidentGraph_CaptureCheckpoint(const struct NativeResidentGraph *, void *, size_t);
// Transactional: an invalid blob or unresolved external reference leaves *out
// unchanged. Caller releases the previous graph only after success.
int NativeResidentGraph_RestoreCheckpoint(const void *, size_t, struct NativeResidentGraph **, NativeResidentGraphRebaseExternal, void *user);
int NativeResidentGraph_RebaseSavedRange(const void *, size_t, const struct NativeResidentGraph *, uintptr_t, size_t, void **);
int NativeResidentGraph_RebaseSavedPointer(const void *, size_t, const struct NativeResidentGraph *, uintptr_t saved, void **live);
// Staging API for owners with cross-graph references. Apply may mutate a pending
// graph on failure: never call it on a published graph.
int NativeResidentGraph_PrepareCheckpoint(const void *, size_t, struct NativeResidentGraph **);
int NativeResidentGraph_ApplyCheckpoint(const void *, size_t, struct NativeResidentGraph *, NativeResidentGraphRebaseExternal, void *);
int NativeResidentGraph_Contains(const struct NativeResidentGraph *, const void *, size_t);
#endif
