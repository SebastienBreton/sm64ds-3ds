// Gate-3b smoke: real game assets load through real game code.
//
// SharedFilePtr::Construct/LoadFile/Release from src/ run over the host
// card seam (port/hal/fs.cpp), allocating from the game's own root heap
// (gate 3a). Expected values below were computed by an INDEPENDENT Python
// LZ77 decoder over the extracted files, so the HAL's C decompressor is
// cross-checked against a second implementation, not against itself.
//
//   handle 2 -> FAT 1628  MG/d_2d_blue_shadow_nsc.bin   raw, 2048 bytes
//   handle 0 -> FAT 1626  MG/casino_back.bmd            LZ77, 18822 -> 35600
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ntr/mmio.h"

typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;

struct SharedFilePtr { u16 fileID; u8 numRefs; void *filePtr; };
struct HeapS;
extern "C" {
struct NativeFileID { u32 archive, file; };
void CpuCopy8(const void *, void *, unsigned);
int func_0204ee40(void *, const char *, void *);
int func_0204ee10(void *);
void FS_InitFile(void *);
void func_02018598(NativeFileID *, u32);
int func_02018dc4(void *, NativeFileID);
int func_02018d98(void *, const char *);
int func_02018d48(void *, void *, int);
int func_0205d644(NativeFileID *, const char *);
int func_0205d568(void *, NativeFileID);
int func_0205d368(void *, int, int);
int FS_ReadFile(void *, void *, int);
int FS_CloseFile(void *);
HeapS *_ZN4Heap13SetupRootHeapEv(void);
SharedFilePtr *_ZN13SharedFilePtr9ConstructEj(SharedFilePtr *self, u32 ov0FileID);
void *_ZN13SharedFilePtr8LoadFileEv(SharedFilePtr *self);
void _ZN13SharedFilePtr7ReleaseEv(SharedFilePtr *self);
}
#define Construct _ZN13SharedFilePtr9ConstructEj
#define LoadFile  _ZN13SharedFilePtr8LoadFileEv
#define Release   _ZN13SharedFilePtr7ReleaseEv

static int g_failures;
#define CHECK(cond) \
    do { if (!(cond)) { ++g_failures; \
        fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)

static u32 sum32(const void *p, u32 n)
{
    const u8 *b = (const u8 *)p;
    u32 s = 0;
    for (u32 i = 0; i < n; ++i) s += b[i];
    return s;
}

int main(void)
{
    CHECK(_ZN4Heap13SetupRootHeapEv() != NULL);
    // All word alignments, odd tails and untouched edge bytes.
    unsigned char source[32], destination[32];
    for (unsigned i=0; i<32; ++i) source[i] = (unsigned char)(i*7+3);
    for (unsigned a=0; a<4; ++a) for (unsigned b=0; b<4; ++b)
        for (unsigned n=0; n<=17; ++n) {
            memset(destination, 0xa5, sizeof destination);
            CpuCopy8(source+a, destination+b+4, n);
            for (unsigned i=0; i<32; ++i)
                CHECK(destination[i] == (i>=b+4 && i<b+4+n ? source[a+i-b-4] : 0xa5));
        }
    CpuCopy8(NULL, NULL, 0);
    CHECK(ntr::io_init());
    auto *vram = ntr::xlate<unsigned char>((void*)0x06000000);
    memset(vram, 0xa5, 32);
    CpuCopy8(source+1, (void*)0x06000003, 17);
    CHECK(vram[2] == 0xa5 && vram[20] == 0xa5);
    CHECK(memcmp(vram+3, source+1, 17) == 0);
    puts("native CpuCopy8:288 alignment/length cases and translated VRAM passed");

    /* raw file: bytes arrive exactly as on disk */
    SharedFilePtr raw;
    Construct(&raw, 2);
    CHECK(raw.fileID == 1628);          /* handle->FAT map ran in the ctor */
    CHECK(raw.numRefs == 0 && raw.filePtr == NULL);
    u8 *rp = (u8 *)LoadFile(&raw);
    CHECK(rp != NULL);
    CHECK(raw.numRefs == 1);
    static const u8 raw_first8[8] = {0x9d,0xe3,0x9d,0xe3,0x9d,0xe3,0x9d,0xe3};
    CHECK(memcmp(rp, raw_first8, 8) == 0);
    CHECK(sum32(rp, 2048) == 0x48000u);

    /* compressed file: LZ77 stream decodes to the independent expectation */
    SharedFilePtr bmd;
    Construct(&bmd, 0);
    CHECK(bmd.fileID == 1626);
    u8 *bp = (u8 *)LoadFile(&bmd);
    CHECK(bp != NULL);
    static const u8 bmd_first8[8] = {0x04,0x00,0x00,0x00,0x01,0x00,0x00,0x00};
    static const u8 bmd_last8[8]  = {0xff,0x7f,0xff,0x7f,0xff,0x7f,0xff,0x7f};
    CHECK(memcmp(bp, bmd_first8, 8) == 0);
    CHECK(memcmp(bp + 35600 - 8, bmd_last8, 8) == 0);
    CHECK(sum32(bp, 35600) == 0x2128efu);

    /* refcounting: shared loads alias, release tears down at zero */
    u8 *bp2 = (u8 *)LoadFile(&bmd);
    CHECK(bp2 == bp);
    CHECK(bmd.numRefs == 2);
    Release(&bmd);
    CHECK(bmd.numRefs == 1 && bmd.filePtr == bp);
    Release(&bmd);
    CHECK(bmd.numRefs == 0 && bmd.filePtr == NULL);
    Release(&raw);
    CHECK(raw.filePtr == NULL);

    /* the heap survived: a fresh load still works after the churn */
    SharedFilePtr again;
    Construct(&again, 0);
    u8 *ap = (u8 *)LoadFile(&again);
    CHECK(ap != NULL);
    CHECK(sum32(ap, 35600) == 0x2128efu);
    Release(&again);

    // Exercise real path->FAT->open, original seek, partial reads and closure.
    u32 stream[0x11]; NativeFileID id;
    FS_InitFile(stream);
    CHECK(func_0205d644(&id, "rom:/MG/d_2d_blue_shadow_nsc.bin") == 1);
    CHECK(id.file == 1628);
    CHECK(func_0205d568(stream, id) == 1);
    CHECK(stream[8] == 0 && stream[9] == 2048 && stream[10] == 0);
    u8 chunk[16] = {};
    CHECK(FS_ReadFile(stream, chunk, 8) == 8);
    CHECK(memcmp(chunk, raw_first8, 8) == 0 && stream[10] == 8);
    CHECK(func_0205d368(stream, -4, 2) == 1);
    CHECK(FS_ReadFile(stream, chunk, 16) == 4);
    CHECK(stream[10] == 2048 && FS_ReadFile(stream, chunk, 1) == 0);
    CHECK(func_0205d368(stream, 0, 0) == 1);
    CHECK(FS_ReadFile(stream, chunk, 8) == 8);
    CHECK(memcmp(chunk, raw_first8, 8) == 0);
    CHECK(FS_CloseFile(stream) == 1);
    CHECK(FS_ReadFile(stream, chunk, 1) == -1 && FS_CloseFile(stream) == 0);
    CHECK(func_0205d644(&id, "rom:/does-not-exist.bin") == 0);
    CHECK(func_0205d568(stream, id) == 0);
    // Original sound archive: offsets and header bytes measured independently
    // from the supplied ROM, not derived from the native stream implementation.
    FS_InitFile(stream);
    CHECK(func_0205d644(&id, "rom:/data/sound_data.sdat") == 1 && id.file == 117);
    NativeFileID game_id;
    func_02018598(&game_id, 117);
    CHECK(game_id.archive == id.archive && game_id.file == 117);
    CHECK(func_02018dc4(stream, game_id) == 1 && stream[9] == 4456832);
    CHECK(func_0205d568(stream, id) == 0); // no leaked/replaced open stream
    u32 sdat_header[12] = {};
    CHECK(func_02018d48(stream, sdat_header, sizeof sdat_header) == sizeof sdat_header);
    CHECK(memcmp(sdat_header, "SDAT", 4) == 0);
    CHECK(sdat_header[2] == 4456832 && sdat_header[6] == 45776);
    CHECK(func_0205d368(stream, sdat_header[6], 0) == 1);
    CHECK(FS_ReadFile(stream, chunk, 8) == 8);
    const u8 info_header[] = {'I','N','F','O',0x68,0x22,0,0};
    CHECK(memcmp(chunk, info_header, 8) == 0);
    CHECK(FS_CloseFile(stream) == 1);
    FS_InitFile(stream);
    CHECK(func_02018d98(stream, "/ARCHIVE/arc0.narc") == 1);
    unsigned char narc_header[16] = {};
    CHECK(func_02018d48(stream, narc_header, sizeof narc_header) == sizeof narc_header);
    CHECK(memcmp(narc_header, "NARC", 4) == 0);
    CHECK(FS_CloseFile(stream) == 1);
    // Independent ndspy extraction:198 members,316042 bytes,FNV-1a22c6ae26.
    FS_InitFile(stream);
    CHECK(func_02018d98(stream, "/ARCHIVE/arc0.narc") == 1);
    unsigned narc_size=stream[9];
    void *narc=malloc(narc_size);
    CHECK(narc != NULL);
    if (!narc) return 1;
    CHECK(func_02018d48(stream,narc,narc_size)==(int)narc_size);
    CHECK(FS_CloseFile(stream)==1);
    u32 archive[24]={};
    CHECK(func_0204ee40(archive,"ar0",narc)==1);
    CHECK(func_0204ee40(archive,"ar0",narc)==0); // duplicate owner
    CHECK(archive[20]==(u32)(uintptr_t)narc);
    NativeFileID member={(u32)(uintptr_t)archive,198};
    FS_InitFile(stream);
    CHECK(func_0205d568(stream,member)==0); // end-exclusive file count
    u32 hash=2166136261u,total=0;
    unsigned char data[137];
    for (unsigned i=0;i<198;++i) {
        member.file=i;
        CHECK(func_0205d568(stream,member)==1);
        CHECK(func_0205d568(stream,member)==0); // no replacing a live stream
        unsigned length=stream[9];total+=length;
        unsigned seen=0;
        while (seen<length) {
            int got=FS_ReadFile(stream,data,sizeof data);
            CHECK(got>0);
            if (got<=0) return 1;
            for (int j=0;j<got;++j) hash=(hash^data[j])*16777619u;
            seen+=got;
        }
        CHECK(FS_ReadFile(stream,data,1)==0);
        CHECK(func_0205d368(stream,0,0)==1);
        unsigned sample=length<sizeof data?length:sizeof data;
        CHECK(FS_ReadFile(stream,data,sample)==(int)sample);
        // Direct-pointer layout is used by the game's fast member accessor.
        const u32 *fat=(const u32*)(uintptr_t)archive[21];
        const unsigned char *image=(const unsigned char*)(uintptr_t)archive[22];
        CHECK(memcmp(data,image+fat[3+2*i],sample)==0);
        CHECK(FS_CloseFile(stream)==1);
        CHECK(FS_ReadFile(stream,data,1)==-1);
    }
    CHECK(total==316042 && hash==0x22c6ae26u);
    CHECK(func_0204ee10(archive)==1);
    CHECK(func_0204ee10(archive)==0);
    CHECK(func_0205d568(stream,member)==0); // stale archive identity
    CHECK(func_0204ee40(archive,"ar0",narc)==1); // remount
    CHECK(func_0204ee10(archive)==1);
    free(narc);
    printf("native archive:198 members,316042 bytes,independent digest %08x\n",hash);
    printf("native stream: path/open/seek/read/EOF/close checks complete\n");

    if (g_failures) {
        fprintf(stderr, "smoke_fs: %d FAILURE(S)\n", g_failures);
        return 1;
    }
    printf("smoke_fs: all checks passed (raw + LZ77 assets loaded through "
           "SharedFilePtr on the game heap, refcounts exact)\n");
    return 0;
}
