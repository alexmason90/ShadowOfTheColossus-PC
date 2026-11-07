/* Exercise actual guest file operations on a linked overlay and writable saves. */
#include "../src/runtime_file.c"
#include <assert.h>
static int32_t guest_errno;
int32_t *runtime_errno(void) { return &guest_errno; }
int32_t runtime_guest_errno(int e) { return e; }
uintptr_t runtime_lookup(const RuntimeExport *table,size_t n,const char *name) {
    for (size_t i=0;i<n;++i) if (!strcmp(table[i].name,name)) return (uintptr_t)table[i].function;
    return 0;
}
int main(void) {
    char root[]="/tmp/bbport-mod-files-XXXXXX";
    assert(mkdtemp(root));
    char game[512],user[512],source[512],link[512];
    snprintf(game,sizeof(game),"%s/game",root);
    snprintf(user,sizeof(user),"%s/user",root);
    snprintf(source,sizeof(source),"%s/mod.dcx",root);
    snprintf(link,sizeof(link),"%s/game/asset.dcx",root);
    assert(!mkdir(game,0755));
    FILE *f=fopen(source,"w"); assert(f); assert(fputs("modded",f)>=0); assert(!fclose(f));
    assert(!symlink(source,link));
    runtime_file_configure(game,user);
    for (int i=0;i<3;++i) {
        const char *path=i==0 ? "/app0/asset.dcx" : i==1 ? "/hostapp/asset.dcx" : "asset.dcx";
        int fd=(int)do_open(path,0,0); assert(fd>=3);
        char content[8]={0}; assert(do_read(fd,content,6)==6 && !strcmp(content,"modded"));
        GuestStat info; assert(!do_stat(path,&info) && info.size==6);
        assert(!do_close(fd));
        assert(do_open(path,2,0)==-EROFS);
        assert(do_open(path,0x400,0)==-EROFS);
        assert(do_truncate(path,0)==-EROFS);
        assert(path_op(path,2,0)==-EROFS);
        assert(do_rename(path,"/data/moved")==-EROFS);
    }
#ifndef _WIN32
    char mixed_dir[1024], mixed_file[1100];
    snprintf(mixed_dir,sizeof(mixed_dir),"%s/MiXeD",game);
    snprintf(mixed_file,sizeof(mixed_file),"%s/CacheD.psarc",mixed_dir);
    assert(!mkdir(mixed_dir,0755));
    f=fopen(mixed_file,"w"); assert(f); fputs("archive",f); fclose(f);
    int folded=(int)do_open("/app0/mixed/cached.psarc",0,0); assert(folded>=3);
    char archive[8]={0}; assert(do_read(folded,archive,7)==7 && !strcmp(archive,"archive"));
    assert(!do_close(folded));
    GuestStat folded_info; assert(!do_stat("/hostapp/MIXED/CACHED.PSARC",&folded_info) && folded_info.size==7);
    char update[512], update_dir[1024], update_file[1100];
    snprintf(update,sizeof(update),"%s/update",root);
    snprintf(update_dir,sizeof(update_dir),"%s/MIXED",update);
    snprintf(update_file,sizeof(update_file),"%s/CACHED.PSARC",update_dir);
    assert(!mkdir(update,0755) && !mkdir(update_dir,0755));
    f=fopen(update_file,"w"); assert(f); fputs("updated",f); fclose(f);
    strcpy(update_root,update);
    folded=(int)do_open("/app0/mixed/cached.psarc",0,0); assert(folded>=3);
    memset(archive,0,sizeof(archive));
    assert(do_read(folded,archive,7)==7 && !strcmp(archive,"updated"));
    assert(!do_close(folded));
    update_root[0]=0;
    assert(!unlink(update_file) && !rmdir(update_dir) && !rmdir(update));
    assert(!unlink(mixed_file) && !rmdir(mixed_dir));
    int case_save=(int)do_open("/data/SaveName",0x202,0644); assert(case_save>=3);
    assert(!do_close(case_save));
    assert(do_open("/data/savename",0,0)==-ENOENT);
    assert(!path_op("/data/SaveName",2,0));
#endif
    int dir=(int)do_open("/app0",0x20000,0); assert(dir>=3);
    char entries[1024]; int64_t count=do_getdents(dir,entries,sizeof(entries),NULL); assert(count>0);
    int found=0;
    for (int64_t p=0;p<count;) {
        uint16_t length; memcpy(&length,entries+p+4,2);
        assert(length);
        if (!strcmp(entries+p+8,"asset.dcx")) { assert(entries[p+6]==8); found=1; }
        p+=length;
    }
    assert(found && !do_close(dir));
    int save=(int)do_open("/data/test-save",0x202,0644); assert(save>=3);
    assert(do_write(save,"save",4)==4 && !do_close(save));
    assert(!path_op("/data/test-save",2,0));
    assert(!unlink(link) && !unlink(source) && !rmdir(game));
    const char *dirs[]={"temp0","download0","data"};
    for (int i=0;i<3;++i) { char p[1024]; snprintf(p,sizeof(p),"%s/%s",user,dirs[i]); assert(!rmdir(p)); }
    assert(!rmdir(user) && !rmdir(root));
    puts("Guest mod files: reads, stat, merged listing, readonly assets and writable saves PASS");
}
