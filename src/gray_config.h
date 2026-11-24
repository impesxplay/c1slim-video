/* Runtime waveform parameters. Parse completely before opening /dev/mem. */
#ifndef C1_GRAY_CONFIG_H
#define C1_GRAY_CONFIG_H
#include <ctype.h>
#include <limits.h>
typedef struct { unsigned duration, vsh, groups, gate, source; } GrayStep;
static GrayStep gray_steps[15]={
    {31,67,2,17,8},{12,52,1,17,8},{10,48,1,17,8},
    {9,44,1,17,8},{8,42,1,17,8},{7,41,1,3,4},
    {6,39,1,3,4},{5,33,1,3,4},{5,26,1,3,4},
    {3,24,1,3,4},{3,19,1,3,4},{3,14,1,3,4},
    {3,11,1,3,4},{3,8,1,3,4},{2,5,1,3,4}
};
static int gray_numbers(char *line,unsigned values[6]){
    char *p=line,*end;
    for(int i=0;i<6;i++){
        while(isspace((unsigned char)*p))p++;
        if(!isdigit((unsigned char)*p))return -1;
        errno=0;unsigned long v=strtoul(p,&end,10);
        if(errno||v>UINT_MAX||(*end&&!isspace((unsigned char)*end)))return -1;
        values[i]=(unsigned)v;p=end;
    }
    while(isspace((unsigned char)*p))p++;
    return *p?-1:0;
}
static int gray_config(const char *path){
    FILE *f=fopen(path,"rb");
    if(!f){perror("config");return -1;}
    char line[256]; GrayStep candidate[15];
    if(!fgets(line,sizeof(line),f)||strcmp(line,"C1GRAYCFG1\n"))goto bad;
    for(unsigned i=0;i<15;i++){
        unsigned values[6]; GrayStep *s=&candidate[i];
        if(!fgets(line,sizeof(line),f)||gray_numbers(line,values)||values[0]!=i)goto bad;
        *s=(GrayStep){values[1],values[2],values[3],values[4],values[5]};
        if(s->duration<1||s->duration>31||s->vsh<1||s->vsh>67||s->groups<1||s->groups>2)goto bad;
        if(!((s->gate==17&&s->source==8)||(s->gate==3&&s->source==4)))goto bad;
    }
    int c;while((c=fgetc(f))!=EOF)if(!isspace((unsigned char)c))goto bad;
    if(ferror(f))goto bad;
    fclose(f);memcpy(gray_steps,candidate,sizeof(candidate));return 0;
bad:
    fclose(f);fprintf(stderr,"Invalid config: expected C1GRAYCFG1 and 15 ordered rows; duration 1..31, VSH 1..67, groups 1..2, timing 17/8 or 3/4. No hardware accessed.\n");return -1;
}
static void gray_dump(void){
    puts("C1GRAYCFG1");for(int i=0;i<15;i++){GrayStep *s=&gray_steps[i];
    printf("%d %u %u %u %u %u\n",i,s->duration,s->vsh,s->groups,s->gate,s->source);}
}
#endif
