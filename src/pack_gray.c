#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
int main(int argc,char**argv){
 if(argc==4&&!strcmp(argv[1],"--align-audio")){uint32_t h[5];FILE*f=fopen(argv[2],"rb");if(!f||fread(h,1,20,f)!=20)return 3;fclose(f);if(h[0]!=0x31563143||!h[4])return 4;int fd=open(argv[3],O_RDWR|O_CREAT,0600);if(fd<0)return 5;int rc=ftruncate(fd,(off_t)h[4]*3200)||fsync(fd);close(fd);return rc?6:0;}
 if(argc!=2)return 2;FILE*f=fopen(argv[1],"wb+");if(!f)return 3;
 uint32_t h[5]={0x31563143,296,152,30,0};uint8_t b[296*152],lut[256];
 for(unsigned i=0;i<256;i++)lut[i]=((255-i)*31+127)/255;
 if(fwrite(h,1,20,f)!=20)return 4;
 for(;;){size_t n=fread(b,1,sizeof(b),stdin);if(!n){if(ferror(stdin))return 5;break;}if(n!=sizeof(b))return 6;
 for(unsigned i=0;i<sizeof(b);i++)b[i]=lut[b[i]];
 if(fwrite(b,1,sizeof(b),f)!=sizeof(b))return 7;h[4]++;if(h[4]%1800==0)fprintf(stderr,"PACK frames=%u seconds=%u\n",h[4],h[4]/30);
 }
 if(!h[4]||fseek(f,0,SEEK_SET)||fwrite(h,1,20,f)!=20||fflush(f)||fsync(fileno(f))||fclose(f))return 8;
 fprintf(stderr,"PACK_DONE frames=%u bytes=%llu\n",h[4],20ull+(unsigned long long)h[4]*sizeof(b));return 0;
}
