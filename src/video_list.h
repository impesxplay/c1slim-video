#include <dirent.h>
#include <strings.h>
#include "font5x7.h"
#define BASE "/storage/c1-video"
static int selected,action=-1,dirty=1;
static long long last_key;
static unsigned count,capacity;
static char **files;
static unsigned char screen[N];
static uint16_t *cjk;
static const char*video_root(void){const char*p=getenv("C1_VIDEO_DIR");return p?p:"/storage/mtp/video";}
static int add_file(const char*path){if(count==capacity){unsigned c=capacity?capacity*2:64;char**v=realloc(files,c*sizeof(*v));if(!v)return -1;files=v;capacity=c;}files[count]=strdup(path);if(!files[count])return -1;count++;return 0;}
static int supported(const char*p){const char*e=strrchr(p,'.');if(!e)return 0;const char*ext[]={".mp4",".mkv",".mov",".avi",".webm",".m4v",".ts",".mpg",".mpeg"};for(unsigned i=0;i<sizeof(ext)/sizeof(*ext);i++)if(!strcasecmp(e,ext[i]))return 1;return 0;}
static void scan_dir(const char*path,int depth){if(depth>4)return;DIR*d=opendir(path);if(!d)return;struct dirent*e;while((e=readdir(d))){if(e->d_name[0]=='.'||strchr(e->d_name,'\n')||strchr(e->d_name,'\r'))continue;char full[1024];if(snprintf(full,sizeof(full),"%s/%s",path,e->d_name)>=(int)sizeof(full))continue;struct stat st;if(lstat(full,&st))continue;if(S_ISDIR(st.st_mode))scan_dir(full,depth+1);else if(S_ISREG(st.st_mode)&&supported(full))if(add_file(full))break;}closedir(d);}
static int cmp_file(const void*a,const void*b){return strcasecmp(*(char*const*)a,*(char*const*)b);}
static void scan_videos(void){scan_dir(video_root(),0);qsort(files,count,sizeof(*files),cmp_file);}
static void load_font(void){FILE*f=fopen(BASE "/device-cache/cjk12.bin","rb");if(!f)return;size_t n=(0xa000-0x4e00)*24;cjk=malloc(n);if(cjk&&fread(cjk,1,n,f)!=n){free(cjk);cjk=0;}fclose(f);}
static void dot(int x,int y,int black){if(x<0||x>=W||y<0||y>=H)return;unsigned char bit=1u<<(7-y%8);if(black)screen[(y/8)*W+x]|=bit;else screen[(y/8)*W+x]&=~bit;}
static void box(int x,int y,int w,int h,int b){for(int j=y;j<y+h;j++)for(int i=x;i<x+w;i++)dot(i,j,b);}
static unsigned utf8(const char**p){const unsigned char*s=(const unsigned char*)*p;unsigned c=*s++;if(c>=0xf0&&c<0xf8&&s[0]&&s[1]&&s[2]){s+=3;c=0xfffd;}else if(c>=0xe0&&c<0xf0&&s[0]&&s[1]){c=((c&15)<<12)|((s[0]&63)<<6)|(s[1]&63);s+=2;}else if(c>=0xc0&&c<0xe0&&s[0]){c=((c&31)<<6)|(s[0]&63);s++;}*p=(const char*)s;return c;}
static const char*label(int x,int y,const char*s,int inverse){while(*s){const char*next=s;unsigned c=utf8(&next);int w=c<128?6:12;if(x+w>286)break;for(int j=0;j<12;j++)for(int i=0;i<w;i++){int v=c<128?(j>=2&&j<10&&i<5?((font5x7[c][i]>>(j-2))&1):0):(cjk&&c>=0x4e00&&c<0xa000?((cjk[(c-0x4e00)*12+j]>>i)&1):(i==1||i==10||j==1||j==10));dot(x+i,y+j,v^inverse);}x+=w;s=next;}return s;}
static void draw_list(void){memset(screen,0,N);box(0,0,W,18,1);label(8,3,"选择视频",1);char b[64];snprintf(b,sizeof(b),"%u / %u",count?selected+1:0,count);label(208,3,b,1);
 if(!count){label(12,43,"video 目录中没有视频",0);label(12,65,"请复制视频后重新进入",0);}
 for(unsigned i=(selected/3)*3;i<count&&i<(unsigned)(selected/3+1)*3;i++){int y=23+(i%3)*33;int on=i==(unsigned)selected;if(on){box(3,y-1,290,31,1);}const char*name=files[i]+strlen(video_root());if(*name=='/')name++;snprintf(b,sizeof(b),"%u",i+1);label(7,y+7,b,on);const char*rest=label(25,y+1,name,on);if(*rest)label(25,y+15,rest,on);}
 box(0,124,W,1,1);label(6,129,"上下选择  回车播放  返回退出",0);}
static void restore_selection(void){char path[1024];FILE*f=fopen(BASE "/selected-video.txt","r");if(!f)return;if(fgets(path,sizeof(path),f)){path[strcspn(path,"\r\n")]=0;for(unsigned i=0;i<count;i++)if(!strcmp(path,files[i]))selected=i;}fclose(f);}
static int save_selection(void){FILE*f=fopen(BASE "/selected-video.tmp","w");if(!f)return -1;const char*path=files[selected];uint64_t hash=14695981039346656037ull;for(const unsigned char*s=(const unsigned char*)path;*s;s++)hash=(hash^*s)*1099511628211ull;
 fprintf(f,"%s\n",path);if(!strcmp(path,"/storage/mtp/video/compose_video_296x152_gray_30fps.mp4"))fprintf(f,BASE "/device-cache\n");else fprintf(f,BASE "/device-cache/items/%016llx\n",(unsigned long long)hash);
 int ok=!fclose(f);return ok?rename(BASE "/selected-video.tmp",BASE "/selected-video.txt"):-1;}
