#include "../astro_sky.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int rd(void *ctx,uint32_t pos,void *dst,uint32_t bytes){FILE*f=ctx;return !fseek(f,pos,SEEK_SET)&&fread(dst,1,bytes,f)==bytes;}
int run_sky(const char *root,const astro_star *obs,int n,int w,int h,sky_solution *result){
 char path[1024];uint32_t header[4];
 snprintf(path,sizeof(path),"%s/SKY.DIR",root);FILE*f=fopen(path,"rb");if(!f)return -10;
 if(fread(header,4,4,f)!=4 || memcmp(header,"SDIR",4)){fclose(f);return -11;}
 unsigned keys=header[2],patterns=header[3];sky_dir_entry *dir=malloc((keys+1)*sizeof(*dir));
 if(!dir || fread(dir,sizeof(*dir),keys+1,f)!=keys+1){fclose(f);free(dir);return -12;}fclose(f);
 snprintf(path,sizeof(path),"%s/SKY.FLD",root);FILE*fld=fopen(path,"rb");if(!fld){free(dir);return -13;}
 if(fread(header,4,4,fld)!=4 || memcmp(header,"SFLD",4)){fclose(fld);free(dir);return -14;}
 unsigned fields=header[2];
 snprintf(path,sizeof(path),"%s/SKY.PAT",root);FILE*pat=fopen(path,"rb");if(!pat){fclose(fld);free(dir);return -15;}
 int r=astro_sky_solve(obs,n,w,h,dir,keys,patterns,fields,rd,pat,rd,fld,NULL,NULL,result);
 fclose(pat);fclose(fld);free(dir);return r;
}
