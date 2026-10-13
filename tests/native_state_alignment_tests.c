#include <platform/native_state.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "check failed at %d: %s\n", __LINE__, #c); exit(1); } } while (0)

static int captures, restores, failCapture, failRestore;
int Platform_InputGetStateSize(void) { return 8; }
int NativeAudio_GetStateSize(void) { return 16; }
int NativeGpu_GetStateSize(void) { return 24; }

static int capture(void *dst, int size)
{
	CHECK((uintptr_t)dst % _Alignof(uint64_t) == 0);
	captures++;
	memset(dst, size, (size_t)size);
	return !failCapture;
}
static int restore(const void *src, int size)
{
	CHECK((uintptr_t)src % _Alignof(uint64_t) == 0);
	for (int i = 0; i < size; i++) CHECK(((const unsigned char *)src)[i] == size);
	restores++;
	return !failRestore;
}
int Platform_InputCaptureState(void *d, int n) { return capture(d, n); }
int NativeAudio_CaptureState(void *d, int n) { return capture(d, n); }
int NativeGpu_CaptureState(void *d, int n) { return capture(d, n); }
int Platform_InputRestoreState(const void *d, int n) { return restore(d, n); }
int NativeAudio_RestoreState(const void *d, int n) { return restore(d, n); }
int NativeGpu_RestoreState(const void *d, int n) { return restore(d, n); }

int main(void)
{
	const int size = NativeState_GetSize();
	unsigned char *storage = malloc((size_t)size + 8);
	CHECK(storage != NULL);
	// Exercise every possible eight-byte alignment of the serialized bundle.
	for (int offset = 0; offset < 8; offset++)
	{
		CHECK(NativeState_Capture(storage + offset, size));
		CHECK(NativeState_Restore(storage + offset, size));
	}
	CHECK(captures == 24 && restores == 24);
	CHECK(!NativeState_Capture(storage, size - 1));
	CHECK(!NativeState_Restore(storage, size - 1));
	failCapture = 1;
	CHECK(!NativeState_Capture(storage, size));
	failCapture = 0;
	CHECK(NativeState_Capture(storage, size));
	failRestore = 1;
	CHECK(!NativeState_Restore(storage, size));
	free(storage);
	puts("Native snapshot regions accept unaligned serialized buffers.");
	return 0;
}
