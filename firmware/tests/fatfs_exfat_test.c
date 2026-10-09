// Disposable regular-file block device for the actual pinned FatFs and VFS code.
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "ff.h"
#include "diskio.h"

static unsigned checks, reads, writes, trims;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); exit(1); } } while (0)
enum { SECTOR = 512, SECTORS = 131072, START = 2048 };
static FILE* media;
static bool fail_allocation;
static size_t live_allocation, peak_allocation;
// Logical0 selects the existing MBR partition for fixture creation only.
// Logical1 uses the production auto-detection path on the same block device.
const PARTITION VolToPart[FF_VOLUMES] = {{0,1},{0,0}};

void* ff_memalloc(unsigned bytes) {
    if (fail_allocation) return NULL;
    size_t* p = malloc(sizeof(size_t) + bytes);
    if (!p) return NULL;
    *p = bytes; live_allocation += bytes;
    if (live_allocation > peak_allocation) peak_allocation = live_allocation;
    return p + 1;
}
void ff_memfree(void* p) { if (p) { size_t* h = (size_t*)p - 1; live_allocation -= *h; free(h); } }
int ff_mutex_create(int v) { return 1; }
void ff_mutex_delete(int v) {}
int ff_mutex_take(int v) { return 1; }
void ff_mutex_give(int v) {}
DWORD get_fattime(void) { return (DWORD)(46U << 25 | 10U << 21 | 8U << 16); }
DSTATUS disk_initialize(BYTE drive) { return drive == 0 && media ? 0 : STA_NOINIT; }
DSTATUS disk_status(BYTE drive) { return disk_initialize(drive); }
DRESULT disk_read(BYTE drive, BYTE* data, LBA_t sector, UINT count) {
    if (drive || !media || !data || !count || sector >= SECTORS || count > SECTORS-sector) return RES_PARERR;
    ++reads;
    if (fseeko(media,(off_t)sector*SECTOR,SEEK_SET) || fread(data,SECTOR,count,media) != count) return RES_ERROR;
    return RES_OK;
}
DRESULT disk_write(BYTE drive, const BYTE* data, LBA_t sector, UINT count) {
    if (drive || !media || !data || !count || sector >= SECTORS || count > SECTORS-sector) return RES_PARERR;
    ++writes;
    if (fseeko(media,(off_t)sector*SECTOR,SEEK_SET) || fwrite(data,SECTOR,count,media) != count) return RES_ERROR;
    return RES_OK;
}
DRESULT disk_ioctl(BYTE drive, BYTE command, void* data) {
    if (drive || !media) return RES_PARERR;
    switch (command) {
    case CTRL_SYNC: return fflush(media) ? RES_ERROR : RES_OK;
    case GET_SECTOR_COUNT: *(LBA_t*)data = SECTORS; return RES_OK;
    case GET_SECTOR_SIZE: *(WORD*)data = SECTOR; return RES_OK;
    case GET_BLOCK_SIZE: *(DWORD*)data = 1; return RES_OK;
    case CTRL_TRIM: ++trims; return RES_PARERR;
    default: return RES_PARERR;
    }
}

// Minimal OS/VFS context doubles; functions below are extracted without edits
// from the actual vendored vfs_fat.c by test_fatfs_exfat.py.
typedef struct { char fat_drive[8]; int lock; char tmp_path_buf[1027],tmp_path_buf2[1027]; FIL files[2]; } vfs_fat_ctx_t;
#define _lock_acquire(x) ((void)(x))
#define _lock_release(x) ((void)(x))
#define ESP_LOGD(...) ((void)0)
#include "actual_vfs_stat.inc"

static void put32(unsigned char* p, uint32_t n) { for (unsigned i=0;i<4;++i) p[i]=(unsigned char)(n>>(i*8)); }
static void put64(unsigned char* p, uint64_t n) { for (unsigned i=0;i<8;++i) p[i]=(unsigned char)(n>>(i*8)); }
static void create_image(const char* path, unsigned format) {
    // argv is generated inside a fresh tempfile directory; O_EXCL prevents
    // overwriting anything, and the regular-file check excludes raw devices.
    int fd = open(path,O_RDWR|O_CREAT|O_EXCL,0600);
    CHECK(fd >= 0); struct stat info; CHECK(fstat(fd,&info)==0 && S_ISREG(info.st_mode));
    CHECK(ftruncate(fd,(off_t)SECTOR*SECTORS)==0);
    media=fdopen(fd,"w+b"); CHECK(media);
    unsigned char mbr[SECTOR]={0};
    mbr[446+4]=(format==FM_EXFAT)?0x07:0x0c;
    put32(mbr+446+8,START); put32(mbr+446+12,SECTORS-START);
    mbr[510]=0x55; mbr[511]=0xaa;
    CHECK(disk_write(0,mbr,0,1)==RES_OK);
    unsigned char work[4096];
    MKFS_PARM options={.fmt=(BYTE)format,.n_fat=1,.align=1,.n_root=512,.au_size=(format==FM_EXFAT)?4096:512};
    CHECK(f_mkfs("0:",&options,work,sizeof(work))==FR_OK);
}
static void write_payload(const char* name, const unsigned char* data, UINT length) {
    FIL file; UINT count=0;
    CHECK(f_open(&file,name,FA_CREATE_NEW|FA_WRITE)==FR_OK);
    CHECK(f_write(&file,data,length,&count)==FR_OK && count==length);
    CHECK(f_sync(&file)==FR_OK); CHECK(f_close(&file)==FR_OK);
}
static void verify_payload(const char* name, const unsigned char* data, UINT length) {
    FIL file; unsigned char chunk[513]; UINT done=0;
    CHECK(f_open(&file,name,FA_READ)==FR_OK);
    while (done<length) {
        UINT want=length-done; if (want>sizeof(chunk)) want=sizeof(chunk);
        UINT got=0; CHECK(f_read(&file,chunk,want,&got)==FR_OK && got==want);
        CHECK(memcmp(chunk,data+done,got)==0); done+=got;
    }
    UINT got=1; CHECK(f_read(&file,chunk,1,&got)==FR_OK && got==0);
    CHECK(f_close(&file)==FR_OK);
}
static void size_boundaries(void) {
    vfs_fat_ctx_t ctx={0}; struct stat info;
    const uint64_t sizes[]={0,INT32_MAX,(uint64_t)INT32_MAX+1,UINT64_C(0x100000011),UINT64_MAX};
    for (unsigned i=0;i<sizeof(sizes)/sizeof(sizes[0]);++i) {
        ctx.files[0].obj.objsize=sizes[i]; errno=0;
        int result=vfs_fat_fstat(&ctx,0,&info);
        if (sizes[i]<=INT32_MAX) CHECK(result==0 && (uint64_t)info.st_size==sizes[i]);
        else CHECK(result==-1 && errno==EOVERFLOW && info.st_size==0);
    }
}
static void inject_large_exfat_metadata(const FATFS* fs) {
    // Adversarial disposable fixture only: BIG.BIN is the first root file.
    // Patch its stream sizes and standard entry checksum, without allocating
    // 4GiB or reading/writing beyond this 64MiB regular-file image.
    unsigned char sector[SECTOR];
    const LBA_t root=fs->database+(LBA_t)(fs->dirbase-2)*fs->csize;
    CHECK(disk_read(0,sector,root,1)==RES_OK);
    unsigned at=0;
    while (at+96<=SECTOR && sector[at]!=0x85) at+=32;
    CHECK(at+96<=SECTOR && sector[at+1]==2 && sector[at+32]==0xc0 && sector[at+64]==0xc1);
    CHECK(sector[at+66]=='B' && sector[at+68]=='I' && sector[at+70]=='G');
    put64(sector+at+40,UINT64_C(0x100000011));
    put64(sector+at+56,UINT64_C(0x100000011));
    uint16_t checksum=0;
    for (unsigned i=0;i<96;++i) if(i!=2 && i!=3)
        checksum=(uint16_t)(((checksum&1)?0x8000:0)+(checksum>>1)+sector[at+i]);
    sector[at+2]=(unsigned char)checksum; sector[at+3]=(unsigned char)(checksum>>8);
    CHECK(disk_write(0,sector,root,1)==RES_OK); CHECK(disk_ioctl(0,CTRL_SYNC,NULL)==RES_OK);
}
static void exercise(const char* path, unsigned format) {
    create_image(path,format);
    FATFS fs={0}; CHECK(f_mount(&fs,"1:",1)==FR_OK);
    CHECK(fs.fs_type==((format==FM_EXFAT)?FS_EXFAT:FS_FAT32) && fs.volbase==START);
    unsigned char data[8193]; for(unsigned i=0;i<sizeof(data);++i) data[i]=(unsigned char)(i*37U+11U);
    write_payload("1:/BIG.BIN",data,17);
    write_payload("1:/DSF00011.DVA",data,sizeof(data));
    write_payload("1:/MEADOW.JPG",data,4097);
    write_payload("1:/Long scene caf\xc3\xa9.txt",data,777);
    verify_payload("1:/DSF00011.DVA",data,sizeof(data));
    CHECK(f_rename("1:/DSF00011.DVA","1:/MEADOW.JPG")==FR_EXIST);
    verify_payload("1:/MEADOW.JPG",data,4097);
    CHECK(f_rename("1:/DSF00011.DVA","1:/DSF00011.TMP")==FR_OK);
    CHECK(f_rename("1:/DSF00011.TMP","1:/DSF00011.DVA")==FR_OK);
    vfs_fat_ctx_t ctx={0}; strcpy(ctx.fat_drive,"1:");
    FF_DIR dir; FILINFO item; unsigned entries=0;
    CHECK(f_opendir(&dir,"1:/")==FR_OK);
    for (;;) {
        CHECK(f_readdir(&dir,&item)==FR_OK); if(!item.fname[0]) break;
        char name[260]; snprintf(name,sizeof(name),"/%s",item.fname);
        struct stat info; CHECK(vfs_fat_stat(&ctx,name,&info)==0);
        CHECK((uint64_t)info.st_size==item.fsize); ++entries;
    }
    CHECK(entries==4); CHECK(f_closedir(&dir)==FR_OK);
    CHECK(f_open(&ctx.files[0],"1:/DSF00011.DVA",FA_READ)==FR_OK);
    struct stat info; CHECK(vfs_fat_fstat(&ctx,0,&info)==0 && info.st_size==sizeof(data));
    CHECK(f_close(&ctx.files[0])==FR_OK);
    fail_allocation=true; CHECK(f_stat("1:/MEADOW.JPG",&item)==FR_NOT_ENOUGH_CORE);
    fail_allocation=false; CHECK(f_stat("1:/MEADOW.JPG",&item)==FR_OK && item.fsize==4097);
    CHECK(f_mount(NULL,"1:",0)==FR_OK);
    CHECK(f_mount(&fs,"1:",1)==FR_OK);
    verify_payload("1:/DSF00011.DVA",data,sizeof(data));
    verify_payload("1:/Long scene caf\xc3\xa9.txt",data,777);
    if (format==FM_EXFAT) {
        CHECK(f_mount(NULL,"1:",0)==FR_OK); inject_large_exfat_metadata(&fs);
        CHECK(f_mount(&fs,"1:",1)==FR_OK);
        CHECK(f_stat("1:/BIG.BIN",&item)==FR_OK && item.fsize==UINT64_C(0x100000011));
        errno=0; CHECK(vfs_fat_stat(&ctx,"/BIG.BIN",&info)==-1 && errno==EOVERFLOW);
        CHECK(f_open(&ctx.files[0],"1:/BIG.BIN",FA_READ)==FR_OK);
        errno=0; CHECK(vfs_fat_fstat(&ctx,0,&info)==-1 && errno==EOVERFLOW);
        CHECK(f_close(&ctx.files[0])==FR_OK);
    }
    CHECK(f_mount(NULL,"1:",0)==FR_OK); CHECK(fclose(media)==0); media=NULL;
    CHECK(live_allocation==0 && trims==0);
    printf("PASS %s MBR start=%u sectors=%u: byte-equal files, UTF8, interleaved stat/readdir, rename collision, remount, allocation failure%s\n",
           format==FM_EXFAT?"exFAT":"FAT32",START,SECTORS-START,format==FM_EXFAT?", actual >4GiB metadata rejected by VFS":"");
}
int main(int argc, char** argv) {
    CHECK(argc==3); CHECK(sizeof(LBA_t)==4 && sizeof(FSIZE_t)==8);
    CHECK(FF_USE_LFN==3 && FF_MAX_LFN==128 && FF_LFN_UNICODE==2 && FF_USE_TRIM==0);
    size_boundaries(); exercise(argv[1],FM_FAT32); exercise(argv[2],FM_EXFAT);
    printf("PASS FatFs checks=%u reads=%u writes=%u trimRequests=%u peakNameScratch=%zuB hostFATFS=%zuB hostFIL=%zuB\n",
           checks,reads,writes,trims,peak_allocation,sizeof(FATFS),sizeof(FIL));
    return 0;
}
