/* Desktop harness for the camera's blind solver: same C code, stdio reads. */
#include "../astro_sky.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int rd(void *ctx,uint32_t pos,void *dst,uint32_t bytes){FILE*f=ctx;return !fseek(f,pos,SEEK_SET)&&fread(dst,1,bytes,f)==bytes;}
static sky_dir_entry *dir=0;static unsigned keys=0,patterns=0,fields=0;
static FILE *pat=0,*fld=0;
void close_sky(void){if(pat)fclose(pat);if(fld)fclose(fld);free(dir);pat=fld=0;dir=0;}
int open_sky(const char *root,const char *prefix){
 char path[1024];uint32_t header[4];close_sky();
 snprintf(path,sizeof(path),"%s/%s.DIR",root,prefix);FILE*f=fopen(path,"rb");if(!f)return -10;
 if(fread(header,4,4,f)!=4 || memcmp(header,"SDIR",4)){fclose(f);return -11;}
 keys=header[2];patterns=header[3];dir=malloc((keys+1)*sizeof(*dir));
 if(!dir || fread(dir,sizeof(*dir),keys+1,f)!=keys+1){fclose(f);return -12;}fclose(f);
 snprintf(path,sizeof(path),"%s/%s.FLD",root,prefix);fld=fopen(path,"rb");if(!fld)return -13;
 if(fread(header,4,4,fld)!=4 || memcmp(header,"SFLD",4))return -14;
 fields=header[2];
 snprintf(path,sizeof(path),"%s/%s.PAT",root,prefix);pat=fopen(path,"rb");if(!pat)return -15;
 return 0;
}
int run_sky(const astro_star *obs,int n,int w,int h,const sky_options *opt,sky_solution *result){
 if(!dir)return -16;
 return astro_sky_solve(obs,n,w,h,dir,keys,patterns,fields,rd,pat,rd,fld,NULL,NULL,opt,result);
}
