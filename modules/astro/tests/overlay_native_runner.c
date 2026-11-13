/* Desktop harness: astro_overlay.c with stdio file readers. */
#include "../astro_overlay.h"
#include <stdio.h>
#include <string.h>
static int rd(void *ctx,uint32_t pos,void *dst,uint32_t bytes){FILE*f=ctx;return !fseek(f,pos,SEEK_SET)&&fread(dst,1,bytes,f)==bytes;}
static astro_sky_files files;
static void open_one(const char *root,const char *name,astro_file *out){
    char path[1024];snprintf(path,sizeof(path),"%s/%s",root,name);
    FILE *f=fopen(path,"rb");out->read=f?rd:0;out->ctx=f;
}
int open_files(const char *root){
    memset(&files,0,sizeof(files));
    open_one(root,"STARS.BIN",&files.stars);open_one(root,"STARS.IDX",&files.stars_idx);
    open_one(root,"STARS.NAM",&files.stars_nam);open_one(root,"DSO.BIN",&files.dso);
    open_one(root,"DSO.KEY",&files.dso_key);open_one(root,"DSO.NAM",&files.dso_nam);
    open_one(root,"CONST.BIN",&files.constellations);
    return files.stars.read && files.dso.read && files.constellations.read;
}
int lookup(int cat,int num,astro_target *t){return astro_dso_lookup(&files,cat,num,t);}
void common_name(const astro_dso *o,char *out,int size){astro_dso_common_name(&files,o,out,size);}
int scene(const astro_wcs *w,int width,int height,const astro_overlay_opts *o,const astro_target *t,astro_scene *s){
    return astro_build_scene(w,width,height,o,t,&files,s);
}
