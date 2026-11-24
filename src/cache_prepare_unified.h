#pragma once
#include <sys/prctl.h>
static const char*cache_dir(void){const char*s=getenv("C1_VIDEO_CACHE");return s?s:"/storage/c1-video/device-cache";}
static const char*cache_source(void){const char*s=getenv("C1_VIDEO_SOURCE");return s?s:"/storage/mtp/video/compose_video_296x152_gray_30fps.mp4";}
static void cache_path(char*p,const char*name){snprintf(p,1024,"%s/%s",cache_dir(),name);}
static int cache_files(unsigned*count){
 char p[1024];struct stat st;uint32_t h[5];cache_path(p,"osmo-device.c1v");FILE*f=fopen(p,"rb");if(!f)return 0;int ok=fread(h,1,20,f)==20;fclose(f);
 if(!ok||h[0]!=0x31563143||h[1]!=296||h[2]!=152||h[3]!=30||!h[4]||stat(p,&st)||st.st_size!=20LL+44992LL*h[4])return 0;
 cache_path(p,"osmo-device.s16le");if(stat(p,&st)||llabs(st.st_size-3200LL*h[4])>96000)return 0;*count=h[4];return 1;
}
static int cache_valid(const struct stat*source){
 char p[1024];cache_path(p,"source.meta");FILE*f=fopen(p,"r");if(!f)return 0;long long size,mtime,ino;unsigned version,count,actual;
 int ok=fscanf(f,"%u %lld %lld %lld %u",&version,&size,&mtime,&ino,&count)==5;fclose(f);
 return ok&&version==2&&size==source->st_size&&mtime==source->st_mtime&&ino==(long long)source->st_ino&&cache_files(&actual)&&actual==count;
}
static int cache_commit(const struct stat*source){
 struct stat after;unsigned count;if(stat(cache_source(),&after)||after.st_size!=source->st_size||after.st_mtime!=source->st_mtime||after.st_ino!=source->st_ino||!cache_files(&count))return -1;
 char tmp[1024],dst[1024];cache_path(tmp,"source.meta.tmp");cache_path(dst,"source.meta");FILE*f=fopen(tmp,"w");if(!f)return -1;
 fprintf(f,"2 %lld %lld %lld %u\n",(long long)source->st_size,(long long)source->st_mtime,(long long)source->st_ino,count);
 int ok=!fflush(f)&&!fsync(fileno(f));if(fclose(f))ok=0;return ok?rename(tmp,dst):-1;
}
static int cache_screen(int percent,const char*status){
 memset(levels,15,sizeof(levels));text(28,25,"PREPARING VIDEO");text(28,48,status);char line[32];snprintf(line,sizeof(line),"%3d%%",percent);text(128,72,line);
 for(int y=94;y<110;y++)for(int x=24;x<272;x++)if(y==94||y==109||x==24||x==271||(x<26+244*percent/100&&y>=97&&y<=106))pixel(x,y,0);
 text(28,126,"BACK: CANCEL");unsigned char frame[N]={0};for(int y=0;y<H;y++)for(int x=0;x<W;x++)if(levels[y*W+x]==0)frame[(y/8)*W+x]|=1u<<(7-y%8);
 int rc=c1_video_ui(active_bus,frame);printf("DECODE_SCREEN percent=%d status=%s rc=%d\n",percent,status,rc);return rc;
}
static int cache_percent(void){
 char path[1024],line[512];double duration=0;cache_path(path,"decode.log");FILE*f=fopen(path,"r");if(f){while(fgets(line,sizeof(line),f)){char*s=strstr(line,"Duration:");int h,m;double sec;if(s&&sscanf(s,"Duration: %d:%d:%lf",&h,&m,&sec)==3){duration=h*3600+m*60+sec;break;}}fclose(f);}
 unsigned frames=0;cache_path(path,"progress.txt");f=fopen(path,"r");if(f){while(fgets(line,sizeof(line),f)){unsigned n;if(sscanf(line,"frame=%u",&n)==1)frames=n;}fclose(f);}
 int v=duration>0?(int)(100*frames/(duration*30)):0;return v>99?99:v;
}
static int ensure_device_cache(void){
 struct stat source;if(stat(cache_source(),&source))return -1;if(cache_valid(&source)){puts("DECODE_CACHE_HIT");return 0;}
 puts("DECODE_CACHE_MISS");if(cache_screen(0,"DECODING VIDEO + AUDIO"))return -1;if(quit||back_request)return -1;
 pid_t child=fork();if(child<0)return -1;
 if(!child){setpgid(0,0);prctl(PR_SET_PDEATHSIG,SIGTERM);signal(SIGTERM,SIG_DFL);signal(SIGINT,SIG_DFL);signal(SIGPIPE,SIG_DFL);char path[1024];cache_path(path,"prepare.log");int f=open(path,O_WRONLY|O_CREAT|O_TRUNC,0600);if(f>=0){dup2(f,1);dup2(f,2);}for(int i=3;i<256;i++)close(i);execl("/storage/c1-video/device-cache/prepare.sh","prepare.sh",(char*)0);_exit(127);}
 setpgid(child,child);int status=0,last=0;long long last_draw=ms();
 for(;;){pid_t r=waitpid(child,&status,WNOHANG);if(r==child)break;if(r<0&&errno!=EINTR)return -1;
  if(quit||back_request){kill(-child,SIGTERM);while(waitpid(child,&status,0)<0&&errno==EINTR){}return -1;}
  int percent=cache_percent();if(percent>=last+5&&ms()-last_draw>=4000){if(cache_screen(percent,"DECODING VIDEO + AUDIO")){kill(-child,SIGTERM);waitpid(child,&status,0);return -1;}last=percent;last_draw=ms();}
  idle_ms(250);
 }
 if(!WIFEXITED(status)||WEXITSTATUS(status)||cache_commit(&source)){cache_screen(last,"DECODE FAILED");idle_ms(2000);return -1;}
 if(cache_screen(100,"SAVED - STARTING"))return -1;puts("DECODE_CACHE_SAVED");return 0;
}
