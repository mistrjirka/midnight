/* Camera bitmap rendering of an astro_scene. GPL-2.0-or-later. */
#include <dryos.h>
#include <bmp.h>
#include <string.h>
#include "astro_draw.h"

/* 8x12 lowercase Greek (alpha..omega), MSB = left pixel. ML fonts are ASCII
 * only; Bayer letters arrive from STARS.NAM as bytes 0x01..0x18. */
static const uint8_t greek_glyphs[24][12]={
    {0x00,0x00,0x00,0x00,0x62,0x92,0x8a,0x84,0x8a,0x71,0x00,0x00} /* alpha */,
    {0x00,0x38,0x44,0x44,0x58,0x44,0x42,0x42,0x62,0x5c,0x40,0x40} /* beta */,
    {0x00,0x00,0x00,0x00,0x82,0x42,0x24,0x14,0x08,0x08,0x10,0x10} /* gamma */,
    {0x00,0x3c,0x40,0x20,0x18,0x44,0x82,0x82,0x82,0x7c,0x00,0x00} /* delta */,
    {0x00,0x00,0x00,0x00,0x3c,0x40,0x40,0x38,0x40,0x40,0x3c,0x00} /* epsilon */,
    {0x00,0x7c,0x08,0x10,0x20,0x40,0x40,0x40,0x3c,0x02,0x04,0x00} /* zeta */,
    {0x00,0x00,0x00,0x00,0xb8,0xc4,0x84,0x84,0x84,0x84,0x04,0x04} /* eta */,
    {0x00,0x38,0x44,0x82,0x82,0xfe,0x82,0x82,0x44,0x38,0x00,0x00} /* theta */,
    {0x00,0x00,0x00,0x00,0x30,0x10,0x10,0x10,0x10,0x0c,0x00,0x00} /* iota */,
    {0x00,0x00,0x00,0x00,0x46,0x48,0x50,0x60,0x50,0x4c,0x00,0x00} /* kappa */,
    {0x00,0x40,0x20,0x20,0x10,0x10,0x28,0x28,0x44,0x86,0x00,0x00} /* lambda */,
    {0x00,0x00,0x00,0x00,0x44,0x44,0x44,0x44,0x4c,0x76,0x40,0x40} /* mu */,
    {0x00,0x00,0x00,0x00,0x82,0x82,0x44,0x48,0x28,0x10,0x00,0x00} /* nu */,
    {0x00,0x7c,0x40,0x38,0x40,0x80,0x80,0x7c,0x02,0x04,0x08,0x00} /* xi */,
    {0x00,0x00,0x00,0x00,0x38,0x44,0x82,0x82,0x44,0x38,0x00,0x00} /* omicron */,
    {0x00,0x00,0x00,0x00,0xfe,0x44,0x44,0x44,0x44,0x46,0x00,0x00} /* pi */,
    {0x00,0x00,0x00,0x00,0x38,0x44,0x42,0x42,0x64,0x58,0x40,0x40} /* rho */,
    {0x00,0x00,0x00,0x00,0x3f,0x44,0x82,0x82,0x44,0x38,0x00,0x00} /* sigma */,
    {0x00,0x00,0x00,0x00,0xfe,0x10,0x10,0x10,0x10,0x0c,0x00,0x00} /* tau */,
    {0x00,0x00,0x00,0x00,0x82,0x42,0x42,0x42,0x44,0x38,0x00,0x00} /* upsilon */,
    {0x00,0x10,0x10,0x38,0x54,0x92,0x92,0x54,0x38,0x10,0x10,0x00} /* phi */,
    {0x00,0x00,0x00,0x00,0x82,0x44,0x28,0x10,0x28,0x44,0x82,0x00} /* chi */,
    {0x00,0x00,0x10,0x92,0x92,0x92,0x54,0x38,0x10,0x10,0x10,0x00} /* psi */,
    {0x00,0x00,0x00,0x00,0x44,0x82,0x92,0x92,0x92,0x6c,0x00,0x00} /* omega */
};
/* cos(15 deg * i), Q14, for 24-segment ellipses. */
static const int16_t cos24[24]={16384,15826,14189,11585,8192,4240,0,-4240,-8192,-11585,
    -14189,-15826,-16384,-15826,-14189,-11585,-8192,-4240,0,4240,8192,11585,14189,15826};
#define MAX_PLACED 64
static int placed[MAX_PLACED][4];
static int nplaced;

static int char_w(void){return fontspec_width(FONT_SMALL);}
static int char_h(void){return fontspec_height(FONT_SMALL);}
int astro_label_width(const char *t){return (int)strlen(t)*char_w();}
void astro_draw_label(int x,int y,const char *t,int fg,int bg){
    int cw=char_w(),ch=char_h();
    char run[24];
    while(*t){
        int n=0;
        while(t[n] && (uint8_t)t[n]>24 && n<23){run[n]=t[n];n++;}
        if(n){
            run[n]=0;
            bmp_printf(FONT(FONT_SMALL,fg,bg),x,y,"%s",run);
            x+=n*cw;t+=n;continue;
        }
        const uint8_t *g=greek_glyphs[(uint8_t)*t-1];
        int gx=x+(cw-8)/2,gy=y+(ch-12)/2;
        bmp_fill(bg,x,y,cw,ch);
        for(int r=0;r<12;r++)for(int c=0;c<8;c++)
            if(g[r]&(0x80>>c))bmp_putpixel(gx+c,gy+r,(uint8_t)fg);
        x+=cw;t++;
    }
}
static int sx(const astro_map *m,int v){return m->ox+v*m->num/(m->den*16);}
static int sy(const astro_map *m,int v){return m->oy+v*m->num/(m->den*16);}
static int code(const astro_map *m,int x,int y){
    return (x<m->x0)|((x>m->x1)<<1)|((y<m->y0)<<2)|((y>m->y1)<<3);
}
static void line(const astro_map *m,int x1,int y1,int x2,int y2,int color){
    int c1=code(m,x1,y1),c2=code(m,x2,y2);
    for(int guard=0;guard<8 && (c1|c2);guard++){
        if(c1&c2)return;
        int c=c1?c1:c2,x,y;
        if(c&8){x=x1+(x2-x1)*(m->y1-y1)/(y2-y1);y=m->y1;}
        else if(c&4){x=x1+(x2-x1)*(m->y0-y1)/(y2-y1);y=m->y0;}
        else if(c&2){y=y1+(y2-y1)*(m->x1-x1)/(x2-x1);x=m->x1;}
        else {y=y1+(y2-y1)*(m->x0-x1)/(x2-x1);x=m->x0;}
        if(c==c1){x1=x;y1=y;c1=code(m,x1,y1);}else{x2=x;y2=y;c2=code(m,x2,y2);}
    }
    if(!(c1|c2))draw_line(x1,y1,x2,y2,color);
}
static void ellipse(const astro_map *m,const astro_ellipse *e,int color){
    int cx=sx(m,e->x),cy=sy(m,e->y);
    int ax=e->ax*m->num/(m->den*16),ay=e->ay*m->num/(m->den*16);
    int bx=e->bx*m->num/(m->den*16),by=e->by*m->num/(m->den*16);
    if(ax*ax+ay*ay<9 && bx*bx+by*by<9){
        if(cx>=m->x0+4 && cx<=m->x1-4 && cy>=m->y0+4 && cy<=m->y1-4)draw_circle(cx,cy,3,color);
        return;
    }
    int px=0,py=0;
    for(int i=0;i<=24;i++){
        int c=cos24[i%24],s=cos24[(i+18)%24];      /* sin = cos(t-90) */
        int x=cx+(ax*c+bx*s)/16384,y=cy+(ay*c+by*s)/16384;
        if(i)line(m,px,py,x,y,color);
        px=x;py=y;
    }
}
/* Four candidate corners around the anchor; skip if it overlaps another label. */
static int place(const astro_map *m,int x,int y,int w,int h,int gap,int *tx,int *ty){
    for(int choice=0;choice<4;choice++){
        *tx=(choice&1)?x-w-gap:x+gap;
        *ty=(choice&2)?y-h-gap/2:y+gap/2;
        if(*tx<m->x0 || *tx+w>m->x1 || *ty<m->y0 || *ty+h>m->y1)continue;
        int overlap=0;
        for(int j=0;j<nplaced && !overlap;j++)
            overlap=*tx<placed[j][2]+3 && *tx+w>placed[j][0]-3 && *ty<placed[j][3]+2 && *ty+h>placed[j][1]-2;
        if(!overlap)return 1;
    }
    return 0;
}
static int label_color(int kind){
    return kind==ASTRO_LABEL_TARGET?COLOR_RED:kind==ASTRO_LABEL_DSO?COLOR_GREEN1:
           kind==ASTRO_LABEL_CONSTELLATION?COLOR_LIGHT_BLUE:COLOR_YELLOW;
}
void astro_draw_scene(const astro_scene *s,const astro_map *m,int erase){
    int empty=COLOR_EMPTY;
    for(int i=0;i<s->nlines;i++)
        line(m,sx(m,s->lines[i].x1),sy(m,s->lines[i].y1),sx(m,s->lines[i].x2),sy(m,s->lines[i].y2),
             erase?empty:COLOR_LIGHT_BLUE);
    for(int i=0;i<s->nellipses;i++)
        ellipse(m,&s->ellipses[i],erase?empty:label_color(s->ellipses[i].kind));
    nplaced=0;
    int h=char_h();
    if(s->target==1){
        int x=sx(m,s->target_x),y=sy(m,s->target_y),c=erase?empty:COLOR_RED;
        line(m,x-14,y,x-5,y,c);line(m,x+5,y,x+14,y,c);
        line(m,x,y-14,x,y-5,c);line(m,x,y+5,x,y+14,c);
    } else if(s->target==2 && (s->target_dx || s->target_dy)){
        /* Arrow from the frame centre towards the off-frame target. */
        int cx=(m->x0+m->x1)/2,cy=(m->y0+m->y1)/2;
        int len=((m->y1-m->y0)<(m->x1-m->x0)?(m->y1-m->y0):(m->x1-m->x0))*2/5;
        int ex=cx+s->target_dx*len/16384,ey=cy+s->target_dy*len/16384;
        int c=erase?empty:COLOR_RED;
        for(int t=-1;t<=1;t++){
            line(m,cx+t,cy,ex+t,ey,c);line(m,cx,cy+t,ex,ey+t,c);
        }
        int bx=-s->target_dx*14/16384,by=-s->target_dy*14/16384;
        line(m,ex,ey,ex+bx-by/2,ey+by+bx/2,c);
        line(m,ex,ey,ex+bx+by/2,ey+by-bx/2,c);
        char text[40];
        snprintf(text,sizeof(text),"%s %d.%d deg",s->target_label,
                 (int)(s->target_sep/10000),(int)(s->target_sep/1000%10));
        int w=astro_label_width(text),tx,ty;
        if(place(m,ex,ey,w,h,10,&tx,&ty)){
            if(erase)bmp_fill(empty,tx,ty,w,h);
            else astro_draw_label(tx,ty,text,COLOR_RED,COLOR_BLACK);
            placed[nplaced][0]=tx;placed[nplaced][1]=ty;placed[nplaced][2]=tx+w;placed[nplaced][3]=ty+h;nplaced++;
        }
    }
    for(int i=0;i<s->nlabels && nplaced<MAX_PLACED;i++){
        const astro_label *l=&s->labels[i];
        int x=sx(m,l->x),y=sy(m,l->y),w=astro_label_width(l->text),tx,ty;
        if(l->kind==ASTRO_LABEL_STAR && x>m->x0+5 && x<m->x1-5 && y>m->y0+5 && y<m->y1-5)
            draw_circle(x,y,4,erase?empty:COLOR_YELLOW);
        int gap=l->kind==ASTRO_LABEL_CONSTELLATION?0:8;
        if(l->kind==ASTRO_LABEL_CONSTELLATION){x-=w/2;y-=h/2;}
        if(!place(m,x,y,w,h,gap,&tx,&ty))continue;
        if(erase)bmp_fill(empty,tx,ty,w,h);
        else astro_draw_label(tx,ty,l->text,label_color(l->kind),COLOR_BLACK);
        placed[nplaced][0]=tx;placed[nplaced][1]=ty;placed[nplaced][2]=tx+w;placed[nplaced][3]=ty+h;nplaced++;
    }
}
