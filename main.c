#include <pspkernel.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspctrl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>

PSP_MODULE_INFO("ArrowSurvival", 0, 1, 1);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER);

#define SW 480
#define SH 272
#define LINE 512
#define GROUND 214
#define RGB(r,g,b) (0xFF000000u | ((u32)(b)<<16) | ((u32)(g)<<8) | (u32)(r))
#define HEX(c) (0xFF000000u | (((u32)(c)&0xFF)<<16) | ((u32)(c)&0xFF00) | (((u32)(c)>>16)&0xFF))
#define SAVEFILE "ms0:/PSP/GAME/ArrowSurvival/save.dat"

static u32 *fbuf[2];
static int back = 0;

/* ================= cizim ================= */
static void rect(int x,int y,int w,int h,u32 c){
    if(x<0){ w+=x; x=0; }
    if(y<0){ h+=y; y=0; }
    if(x+w>SW) w=SW-x;
    if(y+h>SH) h=SH-y;
    if(w<=0||h<=0) return;
    u32 *b=fbuf[back];
    for(int j=0;j<h;j++){ u32 *p=b+(y+j)*LINE+x; for(int i=0;i<w;i++) p[i]=c; }
}
static void disc(int cx,int cy,int r,u32 c){
    for(int j=-r;j<=r;j++){ int w=(int)sqrtf((float)(r*r-j*j)); rect(cx-w,cy+j,2*w+1,1,c); }
}
static void text(int x,int y,u32 c,const char *s){
    pspDebugScreenSetBase(fbuf[back]);
    for(;*s;s++,x+=8){ if(x<0||x>SW-8) continue; pspDebugScreenPutChar(x,y,c,(u8)*s); }
}
static void textf(int x,int y,u32 c,const char *fmt,...){
    char t[96]; va_list ap; va_start(ap,fmt); vsnprintf(t,sizeof(t),fmt,ap); va_end(ap); text(x,y,c,t);
}
static void textc(int y,u32 c,const char *s){ text(SW/2-(int)strlen(s)*4,y,c,s); }
static void flip(void){
    sceDisplayWaitVblankStart();
    sceDisplaySetFrameBuf(fbuf[back],LINE,PSP_DISPLAY_PIXEL_FORMAT_8888,0);
    back^=1;
}
static float frand(float a,float b){ return a+(float)rand()/(float)RAND_MAX*(b-a); }
static int irand(int a,int b){ return a+rand()%(b-a+1); }
static u32 mix(int a,int b,int t,int n){
    int r=((a>>16)&255)*(n-t)/n+((b>>16)&255)*t/n;
    int g=((a>>8)&255)*(n-t)/n+((b>>8)&255)*t/n;
    int bl=(a&255)*(n-t)/n+(b&255)*t/n;
    return RGB(r,g,bl);
}

/* ================= veri ================= */
typedef struct {
    int magic,gold,diamonds,record,kills,bosses,runs,bestCombo,bestSurv;
    int hpLv,powLv,spdLv,arrLv;
    int bombOwned,frostOwned,mode;
    int skin,skinOwned[4];
    int theme,themeOwned[7];
    int crit,goldMul,startHp,luck,second;
    int comp[5];
    int diff;
    unsigned ach;
} Save;
typedef struct { int magic,gold,diamonds,record,hpLv,powLv,spdLv,bombOwned,skin,skinOwned[4],kills,bosses; } OldSave;
static Save sv;

static const char *skinName[4]={"Kirmizi","Mavi","Altin","Yesil"};
static const int skinPrice[4]={0,220,380,295};
static const u32 skinCol[4]={RGB(255,77,77),RGB(41,182,246),RGB(255,215,0),RGB(102,187,106)};
typedef struct { const char *name; int price; int s1,s2,gr,mt,pc,ac; } Theme;
static const Theme TH[7]={
 {"Cehennem",0,  0x250000,0x7a1a00,0x2a0d08,0x160707,0xff8a00,0xffd54f},
 {"Radyasyon",240,0x04170a,0x163d16,0x0f1f0d,0x0a1a08,0x9dff4f,0x9dff4f},
 {"Cennet",345,  0xbfe3ff,0xffe9b8,0xfff3d6,0xdce9ff,0xffffff,0xffb300},
 {"Korku",295,   0x050505,0x14100f,0x0a0808,0x050505,0x8a0000,0xff4d4d},
 {"Zombi",315,   0x0c1408,0x243318,0x1c220f,0x0f1509,0x6ea637,0x8bff4d},
 {"Uzay",390,    0x00010a,0x160a2e,0x161225,0x0c081a,0xffffff,0x7de8ff},
 {"Buz",330,     0xbfe9ff,0xeaf7ff,0xdff3ff,0xa9d9f5,0xffffff,0x7de8ff}
};
static const char *diffName[4]={"Kolay","Normal","Zor","Kabus"};
static const float diffMul[4]={0.82f,1.2f,1.62f,2.15f};
static const char *compName[5]={"Okcu","Sifaci","Kalkan","Buyucu","Gozcu"};
static const int compBase[5]={24,28,34,30,22}, compInc[5]={15,18,20,16,14}, compMax[5]={5,3,3,4,3};

#define NACH 11
static const char *achName[NACH]={"Caylak Avci (25 oldur)","Cehennem Celladi (150)","Kiyamet Avcisi (500)","Iblis Avcisi (3 boss)","Iblis Kabusu (10 boss)","Kombo Ustasi (x10)","Efsane (300 skor)","Besli Kabile (5 yoldas)","Buz Ustasi (buzlu ok)","Dayanikli (3 dakika)","Kabus Avcisi (500+kabus)"};
static const int achRew[NACH]={12,32,70,28,65,20,40,55,16,28,35};

static int allComp(void){ for(int i=0;i<5;i++) if(sv.comp[i]<=0) return 0; return 1; }
static int achCond(int i){
    switch(i){
        case 0: return sv.kills>=25;   case 1: return sv.kills>=150;  case 2: return sv.kills>=500;
        case 3: return sv.bosses>=3;   case 4: return sv.bosses>=10;  case 5: return sv.bestCombo>=10;
        case 6: return sv.record>=300; case 7: return allComp();      case 8: return sv.frostOwned;
        case 9: return sv.bestSurv>=180; case 10: return sv.diff==3 && sv.record>=500;
    }
    return 0;
}

static void defSave(void){
    memset(&sv,0,sizeof(sv)); sv.magic=0xA771; sv.gold=500; sv.diamonds=50;
    sv.skinOwned[0]=1; sv.themeOwned[0]=1; sv.diff=1;
}
static void writeSave(void){
    FILE *f=fopen(SAVEFILE,"wb"); if(f){ fwrite(&sv,sizeof(sv),1,f); fclose(f); }
}
static void loadSave(void){
    defSave();
    FILE *f=fopen(SAVEFILE,"rb"); if(!f) return;
    Save t;
    if(fread(&t,sizeof(t),1,f)==1 && t.magic==0xA771){ sv=t; }
    else {
        OldSave o; fseek(f,0,SEEK_SET);
        if(fread(&o,sizeof(o),1,f)==1 && o.magic==0xA770){
            sv.gold=o.gold; sv.diamonds=o.diamonds; sv.record=o.record; sv.hpLv=o.hpLv; sv.powLv=o.powLv;
            sv.spdLv=o.spdLv; sv.bombOwned=o.bombOwned; sv.skin=o.skin; for(int i=0;i<4;i++) sv.skinOwned[i]=o.skinOwned[i];
            sv.kills=o.kills; sv.bosses=o.bosses;
        }
    }
    fclose(f);
}

typedef struct { float x,y,vx,vy,dmg; int bomb,frost,crit,on; } Arrow;
typedef struct { float x,spd; int hp,maxhp,type,on,cd,flash,slow,dying,mode,tmr; } Enemy; /* 0 normal 1 tank 2 okcu 3 saman 4 intihar 5 suikastci */
typedef struct { float x,y,vx,vy; int on,dmg; u32 col; } EArrow;
typedef struct { float x,y,wob; int hp,on,flash,diveCd,diving,diveT; } Flyer;
typedef struct { float x; int type,life,on; } Pow;
typedef struct { float x; int life,on; } Chest;
typedef struct { float x,y,vx,vy; int life; u32 col; } Pt;
typedef struct { float x; int on,hp,maxhp,elite,rage,cd,dash,slow,flash; float spd; } Boss;

#define MA 40
#define ME 16
#define MEA 24
#define MF 3
#define MP 3
#define MC 2
#define NP 80
static Arrow ar[MA]; static Enemy en[ME]; static EArrow ea[MEA]; static Flyer fl[MF]; static Pow pw[MP]; static Chest ch[MC]; static Pt pt[NP]; static Boss boss;

static int score,level,hp,maxhp,hitFlash,combo,comboT,superCh,charge,charging,spawnT,flyT,powT,lastBossLv;
static int bShield,bRage,bHaste,bMagnet,bMulti,bGold;
static int runFrames,killsChest,secondUsed,sessGold,paused,autoT,msgT,runKills;
static int cdA,cdH,cdG,cdM,shopPg,shopSel,menuSel,resetArm,achSel;
static float px,ang,shake;
static char msg[56];
static float wx[40],wy[40];
enum { ST_MENU, ST_PLAY, ST_SHOP, ST_ACH, ST_OVER };
static int state=ST_MENU;

static void say(const char *s){ strncpy(msg,s,55); msg[55]=0; msgT=100; }
static void fx(float x,float y,int n,u32 c){
    for(int i=0;i<n;i++) for(int k=0;k<NP;k++) if(pt[k].life<=0){
        pt[k].x=x; pt[k].y=y; pt[k].vx=frand(-2,2); pt[k].vy=frand(-3,0.5f); pt[k].life=irand(10,22); pt[k].col=c; break;
    }
}
static void checkAch(void){
    for(int i=0;i<NACH;i++) if(!(sv.ach&(1u<<i)) && achCond(i)){
        sv.ach|=(1u<<i); sv.gold+=achRew[i];
        char t[56]; snprintf(t,56,"BASARIM: %s +%d",achName[i],achRew[i]); say(t);
    }
}
static void initWeather(void){ for(int i=0;i<40;i++){ wx[i]=frand(0,SW); wy[i]=frand(0,GROUND); } }

/* ================= oyun mantigi ================= */
static float diffM(void){ return diffMul[sv.diff]; }
static void giveGold(float reward){
    int coins=(int)reward+combo/3; if(coins<1) coins=1;
    int u=(int)(0.6f*(1+0.1f*sv.goldMul)+0.5f); if(u<1) u=1;
    float m=1;
    if(px>SW*0.55f) m*=1.3f;
    if(bGold>0) m*=2;
    if(sv.comp[4]>0) m*=1+0.12f*sv.comp[4];
    if(sv.diff==3) m*=1.25f;
    int g=(int)(coins*u*m+0.5f); if(g<1) g=1;
    sv.gold+=g; sessGold+=g;
}
static void comboHit(void){
    combo++; comboT=240; if(combo>sv.bestCombo) sv.bestCombo=combo;
    if(combo==3||combo==5||combo==8||combo==12||combo==18){ char t[40]; snprintf(t,40,"KOMBO x%d!",combo); say(t); }
}
static void endRun(void){
    state=ST_OVER;
    if(score>sv.record) sv.record=score;
    int s=runFrames/60; if(s>sv.bestSurv) sv.bestSurv=s;
    sv.runs++; checkAch(); writeSave();
}
static void hurtPlayer(int d){
    if(bShield>0){ fx(px,GROUND-30,6,RGB(79,195,247)); return; }
    if(sv.comp[2]>0 && frand(0,1)<0.1f+0.09f*sv.comp[2]){ say("Yoldas hasari engelledi!"); fx(px+20,GROUND-30,8,RGB(255,213,79)); return; }
    hp-=d; hitFlash=14; shake=6;
    if(hp<=0){
        if(sv.second && !secondUsed){ secondUsed=1; hp=maxhp*3/10; say("IKINCI SANS! Hayata dondun"); fx(px,GROUND-30,16,RGB(125,232,255)); return; }
        hp=0; endRun();
    }
}
static void addScore(int pts){
    score+=pts; if(score>sv.record) sv.record=score;
    int nl=1+score/100; if(nl>level){ level=nl; char t[32]; snprintf(t,32,"DALGA %d",level); say(t); }
}
static void killEnemy(Enemy *e){
    if(e->dying) return;
    float rw = e->type==1?frand(1.5f,3.2f):((e->type==2||e->type==3)?frand(1.1f,2.2f):(e->type==5?frand(1.3f,2.6f):frand(0.6f,1.6f)));
    giveGold(rw);
    if(frand(0,1)<0.05f+0.02f*sv.luck) sv.diamonds++;
    fx(e->x,GROUND-16,10,RGB(120,60,40));
    e->dying=12;
    addScore(e->type==1?18:(e->type==2?14:(e->type==5?20:10)));
    superCh+= e->type==1?7:(e->type==2?5:3); if(superCh>100) superCh=100;
    sv.kills++; runKills++; killsChest++; checkAch();
}
static void damageEnemy(Enemy *e,int d){
    if(e->dying) return;
    e->hp-=d; e->flash=5; if(e->hp<=0) killEnemy(e);
}
static void killBoss(void){
    addScore(boss.elite?320:180);
    for(int i=0;i<3;i++) giveGold(5);
    sv.diamonds+=(boss.elite?4:2)+irand(0,2); superCh=100; sv.bosses++;
    fx(boss.x,GROUND-40,30,RGB(255,90,0)); boss.on=0; say("BOSS YENILDI!"); checkAch();
}
static void damageBoss(int d){
    boss.hp-=d; boss.flash=5; if(boss.hp<=boss.maxhp*45/100) boss.rage=1;
    if(boss.hp<=0) killBoss();
}
static void explode(float x,float y,float r,int d){
    shake=10; fx(x,y,18,RGB(255,120,0));
    for(int i=0;i<ME;i++) if(en[i].on && !en[i].dying){
        float dx=en[i].x-x, dy=(GROUND-16)-y;
        if(sqrtf(dx*dx+dy*dy)<r) damageEnemy(&en[i],d);
    }
    for(int i=0;i<MF;i++) if(fl[i].on){
        float dx=fl[i].x-x, dy=fl[i].y-y;
        if(sqrtf(dx*dx+dy*dy)<r){ fl[i].hp-=d; fl[i].flash=5; }
    }
    if(boss.on){ float dx=boss.x-x, dy=(GROUND-36)-y; if(sqrtf(dx*dx+dy*dy)<r+20) damageBoss((int)(d*1.2f)); }
}
static void spawnArrow(float a,float p,int bomb,int frost,float dmg,int crit){
    for(int i=0;i<MA;i++) if(!ar[i].on){
        ar[i].on=1; ar[i].x=px+14; ar[i].y=GROUND-30; ar[i].vx=cosf(a)*p; ar[i].vy=-sinf(a)*p;
        ar[i].bomb=bomb; ar[i].frost=frost; ar[i].dmg=dmg; ar[i].crit=crit; return;
    }
}
static void shoot(void){
    float p=5.0f+charge*0.22f+sv.arrLv*0.7f;
    int bomb=(sv.mode==1&&sv.bombOwned), frost=(sv.mode==2&&sv.frostOwned);
    float cc=0.06f+0.03f*sv.crit+0.025f*sv.comp[4]+(bRage>0?0.25f:0); if(cc>0.85f) cc=0.85f;
    int crit=frand(0,1)<cc;
    float dmg=bomb?(3+1.5f*sv.powLv):(1+sv.powLv);
    if(bRage>0) dmg*=1.6f;
    if(crit) dmg*=2;
    if(allComp()) dmg*=1.1f;
    int n=bMulti>0?3:1;
    for(int i=0;i<n;i++) spawnArrow(ang+(i-(n-1)/2.0f)*0.16f,p,bomb,frost,dmg,crit);
}
static void superPower(void){
    if(superCh<100){ say("Guc dolmadi!"); return; }
    superCh=0;
    if(sv.mode==1&&sv.bombOwned){ for(int k=0;k<5;k++) spawnArrow(frand(0.3f,0.9f),frand(5,7),1,0,4+2.0f*sv.powLv,0); say("BOMBA YAGMURU!"); }
    else { for(int k=0;k<12;k++) spawnArrow(frand(-0.2f,0.55f),frand(7,9),0,0,1.8f+sv.powLv,0); say("OK AKINI!"); }
    shake=8;
}
static void startRun(void){
    memset(ar,0,sizeof(ar)); memset(en,0,sizeof(en)); memset(ea,0,sizeof(ea)); memset(fl,0,sizeof(fl));
    memset(pw,0,sizeof(pw)); memset(ch,0,sizeof(ch)); memset(pt,0,sizeof(pt)); memset(&boss,0,sizeof(boss));
    score=0; level=1; maxhp=100+20*sv.hpLv+15*sv.startHp; hp=maxhp; spawnT=60; flyT=500; powT=600;
    charge=0; charging=0; lastBossLv=0; combo=0; comboT=0; superCh=0; hitFlash=0; sessGold=0; runKills=0;
    bShield=bRage=bHaste=bMagnet=bMulti=bGold=0; runFrames=0; secondUsed=0; killsChest=0; autoT=0;
    cdA=40; cdH=240; cdG=100; cdM=150; px=70; ang=0.15f; msgT=0; shake=0; paused=0; state=ST_PLAY;
    initWeather();
}
static void spawnEnemy(void){
    int idx=-1; for(int i=0;i<ME;i++) if(!en[i].on){ idx=i; break; } if(idx<0) return;
    float eff=level*diffM();
    float tk=fminf(0.42f,0.12f+eff*0.018f), ac=fminf(0.5f,0.17f+eff*0.025f), sh=fminf(0.22f,0.07f+eff*0.017f);
    float su=fminf(0.35f,0.1f+eff*0.024f), as=level>=6?fminf(0.18f,0.04f+eff*0.011f):0;
    float r=frand(0,1); int t=0;
    if(r<tk) t=1; else if(r<tk+ac) t=2; else if(r<tk+ac+sh) t=3; else if(r<tk+ac+sh+su) t=4; else if(r<tk+ac+sh+su+as) t=5;
    Enemy *e=&en[idx]; memset(e,0,sizeof(*e)); e->on=1; e->type=t; e->x=SW+frand(10,90);
    float sp=frand(1.7f+eff*0.115f,2.85f+eff*0.165f)*0.4f;
    switch(t){
        case 1: e->hp=e->maxhp=4+level/4; e->spd=sp; break;
        case 2: e->hp=e->maxhp=2; e->spd=sp; e->cd=irand(60,110); break;
        case 3: e->hp=e->maxhp=2; e->spd=sp*0.9f; e->cd=irand(70,120); break;
        case 4: e->hp=e->maxhp=1; e->spd=sp*2.3f; break;
        case 5: e->hp=e->maxhp=3; e->spd=sp*1.6f; e->cd=irand(90,150); break;
        default: e->hp=e->maxhp=(rand()%4==0)?2:1; e->spd=sp; break;
    }
}
static void spawnBoss(void){
    float eff=level*diffM(); int elite=((level/5)%2==0);
    float base=26+eff*3.7f;
    memset(&boss,0,sizeof(boss)); boss.on=1; boss.x=SW+30; boss.elite=elite;
    boss.maxhp=boss.hp=(int)(base*(elite?1.55f:1)); boss.spd=(0.45f+eff*0.012f)*(elite?1.18f:1);
    boss.cd=elite?55:65; boss.dash=elite?95:115; lastBossLv=level;
    say(elite?"KABUS IBLISI GELDI!":"BOSS IBLIS GELDI!"); shake=14;
}
static const char powLetter[7]={'K','O','H','M','S','C','A'};
static const u32 powCol[7]={RGB(79,195,247),RGB(255,82,82),RGB(255,238,88),RGB(186,104,200),RGB(102,187,106),RGB(255,159,67),RGB(255,213,79)};
static const char *powName[7]={"KALKAN","OFKE","HIZ","MIKNATIS","SIFA","COKLU OK","ALTIN COSKUSU"};
static void applyPow(int t){
    say(powName[t]);
    switch(t){
        case 0: bShield=360; break; case 1: bRage=360; break; case 2: bHaste=360; break; case 3: bMagnet=420; break;
        case 4: hp+=35; if(hp>maxhp) hp=maxhp; break; case 5: bMulti=420; break; case 6: bGold=420; break;
    }
}
static void spawnPow(int type,float x){
    for(int i=0;i<MP;i++) if(!pw[i].on){ pw[i].on=1; pw[i].type=type; pw[i].x=x; pw[i].life=480; return; }
}
static void spawnChest(void){
    for(int i=0;i<MC;i++) if(!ch[i].on){ ch[i].on=1; ch[i].x=frand(SW*0.3f,SW*0.85f); ch[i].life=700; say("Hazine sandigi belirdi!"); return; }
}
static Enemy *nearestEnemy(float *tx,float *ty){
    Enemy *b=NULL; float bd=9999;
    for(int i=0;i<ME;i++) if(en[i].on&&!en[i].dying){ float d=fabsf(en[i].x-px); if(d<bd){ bd=d; b=&en[i]; *tx=en[i].x; *ty=GROUND-18; } }
    return b;
}

static void updatePlay(SceCtrlData *pad,u32 pressed){
    if(pressed&PSP_CTRL_START) paused=!paused;
    if(paused) return;
    runFrames++; autoT++; if(autoT>600){ autoT=0; writeSave(); }
    float spd=1.95f+0.19f*sv.spdLv; if(bHaste>0) spd*=1.55f;
    int ax=pad->Lx-128, ay=pad->Ly-128;
    if(abs(ax)>30) px+=ax/128.0f*spd;
    if(pad->Buttons&PSP_CTRL_LEFT) px-=spd;
    if(pad->Buttons&PSP_CTRL_RIGHT) px+=spd;
    if(px<24) px=24; if(px>SW-70) px=SW-70;
    if(abs(ay)>30) ang-=ay/128.0f*0.03f;
    if(pad->Buttons&PSP_CTRL_UP) ang+=0.02f;
    if(pad->Buttons&PSP_CTRL_DOWN) ang-=0.02f;
    if(ang>1.0f) ang=1.0f; if(ang<-0.2f) ang=-0.2f;
    if(pad->Buttons&PSP_CTRL_CROSS){ charging=1; if(charge<40) charge++; }
    else if(charging){ shoot(); charging=0; charge=0; }
    if(pressed&PSP_CTRL_SQUARE){
        int m=sv.mode;
        for(int k=0;k<3;k++){ m=(m+1)%3; if(m==0||(m==1&&sv.bombOwned)||(m==2&&sv.frostOwned)) break; }
        sv.mode=m; say(m==0?"Normal ok":(m==1?"Bombali ok":"Buzlu ok"));
    }
    if(pressed&PSP_CTRL_TRIANGLE) superPower();
    if(hitFlash>0) hitFlash--;
    if(comboT>0 && --comboT==0) combo=0;
    if(msgT>0) msgT--;
    shake*=0.85f;
    if(bShield>0) bShield--; if(bRage>0) bRage--; if(bHaste>0) bHaste--; if(bMagnet>0) bMagnet--; if(bMulti>0) bMulti--; if(bGold>0) bGold--;

    /* dogma */
    spawnT--;
    int alive=0; for(int i=0;i<ME;i++) if(en[i].on) alive++;
    float eff=level*diffM();
    int maxE=4+(int)(eff/1.6f); if(maxE>10) maxE=10; if(boss.on) maxE=3;
    if(!boss.on && spawnT<=0 && alive<maxE){ spawnEnemy(); spawnT=(int)fmaxf(25,90-eff*3.4f); }
    if(!boss.on && level>=5 && level%5==0 && lastBossLv!=level) spawnBoss();
    if(--flyT<=0){ flyT=irand(420,700); if(level>=2) for(int i=0;i<MF;i++) if(!fl[i].on){ fl[i].on=1; fl[i].x=SW+20; fl[i].y=frand(40,110); fl[i].hp=2+level/6; fl[i].diveCd=irand(110,200); fl[i].diving=0; fl[i].wob=frand(0,6); break; } }
    if(--powT<=0){ powT=irand(650,1100); spawnPow(irand(0,6),frand(SW*0.3f,SW*0.85f)); }
    if(killsChest>=18){ killsChest-=18; spawnChest(); }

    /* oklar */
    for(int i=0;i<MA;i++) if(ar[i].on){
        Arrow *a=&ar[i]; a->x+=a->vx; a->y+=a->vy; a->vy+=0.09f;
        if(a->x>SW+20||a->x<-20||a->y>GROUND){
            if(a->bomb && a->y>GROUND) explode(a->x,GROUND,50,(int)a->dmg);
            else if(!a->bomb) combo=0;
            a->on=0; continue;
        }
        int hit=0;
        if(boss.on && a->x>boss.x-34 && a->x<boss.x+34 && a->y>GROUND-76 && a->y<GROUND+4){
            comboHit(); a->on=0; hit=1;
            if(a->bomb) explode(a->x,a->y,50,(int)a->dmg); else { damageBoss((int)a->dmg); if(a->frost) boss.slow=180; fx(a->x,a->y,6,RGB(255,160,60)); }
        }
        if(hit) continue;
        for(int j=0;j<ME&&!hit;j++) if(en[j].on&&!en[j].dying){
            Enemy *e=&en[j]; int w=e->type==1?18:(e->type==4?10:12), h=e->type==1?40:(e->type==4?24:32);
            if(a->x>e->x-w && a->x<e->x+w && a->y>GROUND-h && a->y<GROUND+4){
                comboHit(); a->on=0; hit=1;
                if(a->bomb) explode(a->x,a->y,50,(int)a->dmg);
                else if(e->type==4){ explode(e->x,GROUND-14,40,2); e->on=0; }
                else { damageEnemy(e,(int)(a->dmg+0.5f)); if(a->frost) e->slow=180; fx(a->x,a->y,5,RGB(255,160,60)); }
            }
        }
        if(hit) continue;
        for(int j=0;j<MF&&!hit;j++) if(fl[j].on && fabsf(a->x-fl[j].x)<14 && fabsf(a->y-fl[j].y)<12){
            comboHit(); a->on=0; hit=1;
            if(a->bomb) explode(a->x,a->y,50,(int)a->dmg); else { fl[j].hp-=(int)(a->dmg+0.5f); fl[j].flash=5; fx(a->x,a->y,5,RGB(192,77,255)); }
        }
    }
    /* yaratik */
    float lowR=1; Enemy *low=NULL;
    for(int i=0;i<ME;i++) if(en[i].on&&!en[i].dying){ float r=(float)en[i].hp/en[i].maxhp; if(r<lowR){ lowR=r; low=&en[i]; } }
    for(int i=0;i<ME;i++) if(en[i].on){
        Enemy *e=&en[i];
        if(e->dying){ if(--e->dying<=0) e->on=0; continue; }
        if(e->flash>0) e->flash--;
        float sm=1; if(e->slow>0){ e->slow--; sm=0.45f; }
        float dist=e->x-px; e->cd--; e->tmr++;
        switch(e->type){
            case 2: if(dist>200) e->x-=e->spd*sm; else if(dist<110) e->x+=e->spd*0.6f*sm;
                if(e->cd<=0 && dist>80 && dist<360){ for(int k=0;k<MEA;k++) if(!ea[k].on){ ea[k].on=1; ea[k].x=e->x-8; ea[k].y=GROUND-28; ea[k].vx=-(2.6f+level*0.03f); ea[k].vy=0; ea[k].dmg=10; ea[k].col=RGB(255,179,71); break; } e->cd=irand(90,150); } break;
            case 3: if(dist>230) e->x-=e->spd*sm; else if(dist<150) e->x+=e->spd*0.6f*sm;
                if(e->cd<=0 && low && lowR<0.75f){ low->hp++; if(low->hp>low->maxhp) low->hp=low->maxhp; fx(low->x,GROUND-30,5,RGB(142,255,143)); e->cd=irand(100,160); } break;
            case 5: if(dist>190) e->x-=e->spd*sm;
                else if(e->cd<=0 && e->slow<=0){ fx(e->x,GROUND-16,8,RGB(142,36,170)); e->x=px+frand(28,50); fx(e->x,GROUND-16,8,RGB(142,36,170)); e->cd=irand(130,210); } break;
            case 4: e->x-=e->spd*sm; break;
            default: e->x-=e->spd*sm; if(dist<90 && (e->tmr/20)%5==0) e->x-=e->spd*0.4f*sm; break;
        }
        if(e->x<px+16 && e->type!=2 && e->type!=3){
            if(e->type==4){ explode(e->x,GROUND-14,36,1); hurtPlayer(24); }
            else hurtPlayer(e->type==1?25:(e->type==5?22:18));
            fx(px,GROUND-30,8,RGB(255,119,119)); e->on=0;
        } else if(e->x<px+16){ e->x=px+16; }
    }
    /* boss */
    if(boss.on){
        float sm=1; if(boss.slow>0){ boss.slow--; sm=0.5f; }
        if(boss.flash>0) boss.flash--;
        float dist=boss.x-px; boss.cd--; boss.dash--;
        if(dist>170) boss.x-=boss.spd*sm; else if(dist<110) boss.x+=boss.spd*0.55f*sm;
        if(boss.cd<=0){
            for(int k=0;k<MEA;k++) if(!ea[k].on){ ea[k].on=1; ea[k].x=boss.x-20; ea[k].y=GROUND-44; ea[k].vx=-(boss.rage?3.4f:2.8f); ea[k].vy=0; ea[k].dmg=boss.rage?18:14; ea[k].col=boss.rage?RGB(255,42,42):RGB(255,138,0); break; }
            boss.cd=boss.rage?45:65;
        }
        if(boss.dash<=0 && dist>100){ boss.x-=(boss.rage?22:15)*sm; boss.dash=boss.rage?80:120; shake=6; }
        if(boss.x<px+34){ boss.x+=70; hurtPlayer(boss.rage?28:20); }
    }
    /* ucan */
    for(int i=0;i<MF;i++) if(fl[i].on){
        Flyer *f=&fl[i]; if(f->flash>0) f->flash--; f->diveCd--;
        float d=f->x-px;
        if(!f->diving && f->diveCd<=0 && d<220 && d>30){ f->diving=1; f->diveT=48; }
        if(f->diving){ f->y+=((GROUND-50)-f->y)*0.09f; f->x-=1.8f; if(--f->diveT<=0){ f->diving=0; f->diveCd=irand(160,260); } }
        else { f->x-=1.1f; f->wob+=0.07f; f->y+=sinf(f->wob)*0.5f; }
        if(fabsf(f->x-(px+8))<14 && fabsf(f->y-(GROUND-40))<22){ f->on=0; fx(px,GROUND-40,8,RGB(192,77,255)); hurtPlayer(14); continue; }
        if(f->hp<=0){ f->on=0; giveGold(1.5f); addScore(16); superCh+=5; if(superCh>100) superCh=100; sv.kills++; killsChest++; fx(f->x,f->y,10,RGB(192,77,255)); checkAch(); continue; }
        if(f->x<-30) f->on=0;
    }
    /* dusman oklari */
    for(int i=0;i<MEA;i++) if(ea[i].on){
        ea[i].x+=ea[i].vx; ea[i].y+=ea[i].vy;
        if(ea[i].x<px+8 && ea[i].x>px-10 && ea[i].y>GROUND-62){ ea[i].on=0; hurtPlayer(ea[i].dmg); continue; }
        if(ea[i].x<-10) ea[i].on=0;
    }
    /* guc kuresi ve sandik */
    for(int i=0;i<MP;i++) if(pw[i].on){
        if(bMagnet>0) pw[i].x+=(pw[i].x<px?1.8f:-1.8f);
        if(--pw[i].life<=0){ pw[i].on=0; continue; }
        if(fabsf(pw[i].x-px)<18){ applyPow(pw[i].type); pw[i].on=0; checkAch(); }
    }
    for(int i=0;i<MC;i++) if(ch[i].on){
        if(bMagnet>0) ch[i].x+=(ch[i].x<px?1.8f:-1.8f);
        if(--ch[i].life<=0){ ch[i].on=0; continue; }
        if(fabsf(ch[i].x-px)<20){
            ch[i].on=0; int g=(int)(irand(9,22)*(1+0.1f*sv.goldMul)); int d=irand(1,2);
            sv.gold+=g; sv.diamonds+=d; sessGold+=g; fx(ch[i].x,GROUND-16,16,RGB(255,213,79));
            char t[56]; snprintf(t,56,"Sandik! +%d altin +%d elmas",g,d); say(t);
            if(frand(0,1)<0.35f) applyPow(irand(0,6));
        }
    }
    /* yoldaslar */
    float tx=0,ty=0;
    if(sv.comp[0]>0 && --cdA<=0){ cdA=(int)fmaxf(30,95-sv.comp[0]*14); if(nearestEnemy(&tx,&ty)){
        float a=atan2f(-(ty-(GROUND-34)),tx-(px-14)); float sx=px; px-=28; spawnArrow(a,7.5f,0,0,1+sv.comp[0]*0.9f,0); px=sx; } }
    if(sv.comp[1]>0 && --cdH<=0){ cdH=(int)fmaxf(260,720-sv.comp[1]*150); if(hp<maxhp){ hp+=6+sv.comp[1]*5; if(hp>maxhp) hp=maxhp; fx(px-20,GROUND-30,8,RGB(140,255,152)); say("Sifaci seni iyilestirdi"); } }
    if(sv.comp[2]>0 && --cdG<=0){ cdG=(int)fmaxf(70,170-sv.comp[2]*30); for(int i=0;i<ME;i++) if(en[i].on&&!en[i].dying&&fabsf(en[i].x-px)<70){ damageEnemy(&en[i],2+sv.comp[2]*2); fx(en[i].x,GROUND-20,6,RGB(255,213,79)); break; } }
    if(sv.comp[3]>0 && --cdM<=0){ cdM=(int)fmaxf(110,260-sv.comp[3]*32); if(nearestEnemy(&tx,&ty)) explode(tx,ty,40+sv.comp[3]*6,3+sv.comp[3]*2); }
    /* parcacik */
    for(int i=0;i<NP;i++) if(pt[i].life>0){ pt[i].x+=pt[i].vx; pt[i].y+=pt[i].vy; pt[i].vy+=0.12f; pt[i].life--; }
    for(int i=0;i<40;i++){ wy[i]+=(sv.theme==5?0.1f:0.6f+(i%3)*0.2f); if(wy[i]>GROUND){ wy[i]=0; wx[i]=frand(0,SW); } }
}

/* ================= ciz ================= */
static void drawScene(int t){
    const Theme *th=&TH[t];
    for(int k=0;k<9;k++) rect(0,k*GROUND/9,SW,GROUND/9+1,mix(th->s1,th->s2,k,8));
    disc(70,46,13,HEX(th->ac)); disc(70,46,19,mix(th->s2,th->ac,1,4));
    for(int i=0;i<6;i++){ int cx=i*95+20, h=44+((i*37)%30);
        for(int k=0;k<h;k++){ int w=k*60/h; rect(cx-w,GROUND-h+k,2*w,1,HEX(th->mt)); } }
    rect(0,GROUND,SW,SH-GROUND,HEX(th->gr)); rect(0,GROUND,SW,2,mix(th->gr,0x000000,1,3));
    for(int i=0;i<40;i++) rect((int)wx[i],(int)wy[i],2,2,HEX(th->pc));
}
static void drawFigure(int x,int body,int acc,int s){
    rect(x-3*s/2,GROUND-14*s,3*s,10*s,HEX(body)); rect(x-s,GROUND-18*s,2*s+1,4*s,RGB(241,196,155)); rect(x-1,GROUND-22*s,2,2,HEX(acc));
}
static void drawPlay(void){
    int sx=(int)frand(-shake,shake), sy=(int)frand(-shake,shake);
    drawScene(sv.theme);
    int X=(int)px+sx;
    for(int i=0;i<5;i++) if(sv.comp[i]>0){
        static const int off[5]={-16,-26,24,-36,34}, bc[5]={0x274b5c,0x1b4d22,0x5c3a10,0x4a1560,0x1b3a1b}, acc[5]={0x7de8ff,0x8cff98,0xffd54f,0xff5500,0x8bc34a};
        drawFigure(X+off[i],bc[i],acc[i],2);
    }
    rect(X-6,GROUND-52+sy,12,38,RGB(61,36,20)); rect(X-5,GROUND-66+sy,10,12,RGB(241,196,155));
    rect(X-5,GROUND-14+sy,4,14,RGB(43,26,16)); rect(X+1,GROUND-14+sy,4,14,RGB(43,26,16));
    if(bShield>0) disc(X,GROUND-34,26,RGB(79,195,247)), rect(X-20,GROUND-34,40,1,RGB(255,255,255));
    for(int i=0;i<18;i++) rect(X+8+(int)(cosf(ang)*i),GROUND-32-(int)(sinf(ang)*i)+sy,2,2,RGB(160,110,50));
    if(charging) rect(X-20,GROUND-84,charge,4,RGB(255,213,79));
    for(int i=0;i<MP;i++) if(pw[i].on){ disc((int)pw[i].x,GROUND-14,8,powCol[pw[i].type]); char c[2]={powLetter[pw[i].type],0}; text((int)pw[i].x-4,GROUND-18,RGB(0,0,0),c); }
    for(int i=0;i<MC;i++) if(ch[i].on){ rect((int)ch[i].x-9,GROUND-12,18,12,RGB(122,74,26)); rect((int)ch[i].x-9,GROUND-16,18,5,RGB(165,105,31)); rect((int)ch[i].x-2,GROUND-10,4,4,RGB(255,213,79)); }
    for(int i=0;i<ME;i++) if(en[i].on){
        Enemy *e=&en[i]; int w=e->type==1?18:(e->type==4?10:12), h=e->type==1?40:(e->type==4?24:32);
        int col; switch(e->type){ case 1: col=0x3a2a2a; break; case 2: col=0x6d4a40; break; case 3: col=0x1b4d22; break; case 4: col=0xb32400; break; case 5: col=0x4d1a66; break; default: col=0x363636; }
        u32 c=HEX(col); if(e->slow>0) c=RGB(79,168,255); if(e->flash>0) c=RGB(255,107,107);
        int hh=h; if(e->dying) hh=h*e->dying/12;
        int ex=(int)e->x+sx;
        rect(ex-w/2,GROUND-hh,w,hh,c);
        if(!e->dying){
            rect(ex-w/2+2,GROUND-h+5,3,3,e->type==5?RGB(192,77,255):RGB(255,50,50));
            if(e->type==1){ rect(ex-w/2-3,GROUND-h+8,4,8,RGB(130,130,130)); rect(ex+w/2-1,GROUND-h+8,4,8,RGB(130,130,130)); }
            if(e->type==2){ rect(ex-w/2-5,GROUND-h+10,2,14,RGB(110,70,30)); }
            if(e->type==3){ rect(ex-w/2-4,GROUND-h-4,2,h,RGB(80,50,20)); disc(ex-w/2-3,GROUND-h-6,3,RGB(142,255,143)); }
            if(e->type==4) disc(ex,GROUND-h/2,w,RGB(255,80,20));
            if(e->type==5) rect(ex+w/2,GROUND-h+6,5,h-10,RGB(30,10,40));
            if(e->maxhp>2){ rect(ex-w/2,GROUND-h-5,w,2,RGB(40,0,0)); rect(ex-w/2,GROUND-h-5,w*e->hp/e->maxhp,2,RGB(255,60,30)); }
        }
    }
    if(boss.on){
        u32 c=boss.slow>0?RGB(61,111,168):(boss.flash>0?RGB(255,110,110):(boss.elite?(boss.rage?HEX(0x7a0f6f):HEX(0x3a0838)):(boss.rage?HEX(0x8f0f0f):HEX(0x4a0808))));
        int bx=(int)boss.x+sx;
        rect(bx-34,GROUND-72,68,72,c); rect(bx-22,GROUND-92,44,22,HEX(0x301010));
        rect(bx-16,GROUND-84,6,6,RGB(255,85,0)); rect(bx+8,GROUND-84,6,6,RGB(255,85,0));
        if(boss.elite) for(int k=0;k<5;k++) rect(bx-20+k*9,GROUND-102,5,10,RGB(255,213,79));
    }
    for(int i=0;i<MF;i++) if(fl[i].on){ int fx_=(int)fl[i].x, fy=(int)fl[i].y; rect(fx_-9,fy-4,18,8,fl[i].flash?RGB(255,120,160):HEX(0x3a1030)); rect(fx_-14,fy-8+(int)(sinf(fl[i].wob*6)*4),8,4,HEX(0x3a1030)); rect(fx_+6,fy-8+(int)(sinf(fl[i].wob*6)*4),8,4,HEX(0x3a1030)); rect(fx_-6,fy-2,3,3,RGB(255,42,107)); }
    u32 ac=sv.mode==1?RGB(255,154,0):(sv.mode==2?RGB(125,232,255):skinCol[sv.skin]);
    for(int i=0;i<MA;i++) if(ar[i].on){ rect((int)ar[i].x-4+sx,(int)ar[i].y+sy,9,2,ac); rect((int)ar[i].x+5+sx,(int)ar[i].y-1+sy,3,4,RGB(220,220,220)); }
    for(int i=0;i<MEA;i++) if(ea[i].on) rect((int)ea[i].x,(int)ea[i].y,9,2,ea[i].col);
    for(int i=0;i<NP;i++) if(pt[i].life>0) rect((int)pt[i].x,(int)pt[i].y,2,2,pt[i].col);
    if(hitFlash>0){ rect(0,14,SW,5,RGB(200,0,0)); rect(0,SH-6,SW,6,RGB(200,0,0)); }
    /* HUD */
    rect(0,0,SW,14,RGB(0,0,0));
    textf(4,3,RGB(255,255,255),"Skor:%d Sv:%d",score,level);
    textf(150,3,RGB(255,213,79),"Altin:%d",sv.gold);
    textf(250,3,RGB(125,232,255),"Elmas:%d",sv.diamonds);
    textf(350,3,RGB(255,255,255),"Rekor:%d",sv.record);
    if(boss.on){ int bw=200; rect(SW/2-bw/2,18,bw,6,RGB(40,0,0)); rect(SW/2-bw/2,18,bw*boss.hp/boss.maxhp,6,RGB(255,60,30)); textc(26,RGB(255,174,66),boss.elite?"KABUS IBLISI":"BOSS IBLIS"); }
    rect(0,SH-16,SW,16,RGB(0,0,0));
    rect(4,SH-12,100,8,RGB(60,0,0)); rect(5,SH-11,98*hp/maxhp,6,RGB(255,60,30));
    textf(110,SH-13,RGB(255,255,255),"%d/%d",hp,maxhp);
    textf(180,SH-13,RGB(125,232,255),"Guc %d%%",superCh);
    if(combo>=3) textf(250,SH-13,RGB(255,200,0),"x%d",combo);
    textf(290,SH-13,RGB(160,160,160),"%s %s",sv.mode==1?"BOMBA":(sv.mode==2?"BUZ":"OK"),diffName[sv.diff]);
    char b[16]; int n=0; if(bShield>0) b[n++]='K'; if(bRage>0) b[n++]='O'; if(bHaste>0) b[n++]='H'; if(bMagnet>0) b[n++]='M'; if(bMulti>0) b[n++]='C'; if(bGold>0) b[n++]='A'; if(px>SW*0.55f) b[n++]='+'; b[n]=0;
    text(440,SH-13,RGB(255,238,88),b);
    if(msgT>0) textc(40,HEX(TH[sv.theme].ac),msg);
    if(paused) textc(120,RGB(255,255,255),"DURAKLATILDI");
}

/* ================= market ================= */
static const char *pageName[5]={"Guc","Stil","Elmas","Yoldas","Doviz"};
static const int pageCount[5]={6,11,5,5,3};
static int itemInfo(int pg,int i,char *lb,int *dia){
    *dia=0; int pr=0; lb[0]=0;
    if(pg==0){
        switch(i){
            case 0: pr=85+65*sv.hpLv; snprintf(lb,64,"Can +20       Sv %d",sv.hpLv); break;
            case 1: pr=120+78*sv.powLv; snprintf(lb,64,"Ok Gucu +1    Sv %d",sv.powLv); break;
            case 2: pr=105+72*sv.spdLv; snprintf(lb,64,"Hiz +0.5      Sv %d",sv.spdLv); break;
            case 3: pr=95+68*sv.arrLv; snprintf(lb,64,"Ok Hizi +1    Sv %d",sv.arrLv); break;
            case 4: if(sv.bombOwned){ pr=-1; snprintf(lb,64,"Bombali Ok    VAR"); } else { pr=450; snprintf(lb,64,"Bombali Ok"); } break;
            case 5: if(sv.frostOwned){ pr=-1; snprintf(lb,64,"Buzlu Ok      VAR"); } else { pr=380; snprintf(lb,64,"Buzlu Ok (yavaslatir)"); } break;
        }
    } else if(pg==1){
        if(i<4){ if(sv.skinOwned[i]){ pr=-1; snprintf(lb,64,"Ok: %s %s",skinName[i],sv.skin==i?"[SECILI]":""); } else { pr=skinPrice[i]; snprintf(lb,64,"Ok: %s",skinName[i]); } }
        else { int t=i-4; if(sv.themeOwned[t]){ pr=-1; snprintf(lb,64,"Tema: %s %s",TH[t].name,sv.theme==t?"[SECILI]":""); } else { pr=TH[t].price; snprintf(lb,64,"Tema: %s",TH[t].name); } }
    } else if(pg==2){
        *dia=1;
        switch(i){
            case 0: pr=9+4*sv.crit; snprintf(lb,64,"Kritik +3%%    Sv %d",sv.crit); break;
            case 1: pr=12+6*sv.goldMul; snprintf(lb,64,"Altin +10%%    Sv %d",sv.goldMul); break;
            case 2: pr=9+4*sv.startHp; snprintf(lb,64,"Bas.Can +15   Sv %d",sv.startHp); break;
            case 3: pr=6+4*sv.luck; snprintf(lb,64,"Sans +2%%      Sv %d",sv.luck); break;
            case 4: if(sv.second){ pr=-1; snprintf(lb,64,"Ikinci Sans   VAR"); } else { pr=35; snprintf(lb,64,"Ikinci Sans (%%30 can)"); } break;
        }
    } else if(pg==3){
        *dia=1;
        if(sv.comp[i]>=compMax[i]){ pr=-1; snprintf(lb,64,"%s Sv %d MAKS",compName[i],sv.comp[i]); }
        else { pr=compBase[i]+compInc[i]*sv.comp[i]; snprintf(lb,64,"%s Sv %d/%d",compName[i],sv.comp[i],compMax[i]); }
    } else {
        if(i==0){ pr=900; snprintf(lb,64,"900 Altin -> 5 Elmas"); }
        else if(i==1){ *dia=1; pr=12; snprintf(lb,64,"12 Elmas -> 180 Altin"); }
        else { pr=0; snprintf(lb,64,"KAYITLARI SIFIRLA (2 kez)"); }
    }
    return pr;
}
static void buyItem(int pg,int i){
    char lb[64]; int dia; int pr=itemInfo(pg,i,lb,&dia);
    if(pg==4&&i==2){ if(resetArm){ defSave(); writeSave(); resetArm=0; say("Kayit sifirlandi"); } else { resetArm=1; say("Tekrar X: sifirla"); } return; }
    resetArm=0;
    if(pr==-1){
        if(pg==1&&i<4){ sv.skin=i; say("Skin secildi"); }
        else if(pg==1){ sv.theme=i-4; initWeather(); say("Tema secildi"); }
        else say("Zaten var / maksimum");
        writeSave(); return;
    }
    int have=dia?sv.diamonds:sv.gold;
    if(have<pr){ say(dia?"Yetersiz elmas":"Yetersiz altin"); return; }
    if(dia) sv.diamonds-=pr; else sv.gold-=pr;
    if(pg==0){ switch(i){ case 0: sv.hpLv++; break; case 1: sv.powLv++; break; case 2: sv.spdLv++; break; case 3: sv.arrLv++; break; case 4: sv.bombOwned=1; break; case 5: sv.frostOwned=1; break; } }
    else if(pg==1){ if(i<4){ sv.skinOwned[i]=1; sv.skin=i; } else { sv.themeOwned[i-4]=1; sv.theme=i-4; initWeather(); } }
    else if(pg==2){ switch(i){ case 0: sv.crit++; break; case 1: sv.goldMul++; break; case 2: sv.startHp++; break; case 3: sv.luck++; break; case 4: sv.second=1; break; } }
    else if(pg==3){ sv.comp[i]++; }
    else { if(i==0) sv.diamonds+=5; else sv.gold+=180; }
    say("Satin alindi!"); checkAch(); writeSave();
}
static void drawShop(void){
    drawScene(sv.theme); rect(0,0,SW,SH,RGB(10,5,4));
    textf(8,6,RGB(255,213,79),"MARKET   Altin:%d  Elmas:%d",sv.gold,sv.diamonds);
    for(int p=0;p<5;p++){
        if(p==shopPg) rect(8+p*92-2,22,80,12,RGB(90,40,10));
        text(8+p*92,24,p==shopPg?RGB(255,255,255):RGB(150,150,150),pageName[p]);
    }
    for(int i=0;i<pageCount[shopPg];i++){
        char lb[64]; int dia; int pr=itemInfo(shopPg,i,lb,&dia);
        int y=44+i*16;
        if(i==shopSel) rect(6,y-3,468,14,RGB(70,30,10));
        text(14,y,i==shopSel?RGB(255,255,255):RGB(180,180,180),lb);
        if(pr>0){ int have=dia?sv.diamonds:sv.gold; textf(400,y,have>=pr?(dia?RGB(125,232,255):RGB(255,213,79)):RGB(255,90,90),"%d%s",pr,dia?"E":"A"); }
    }
    text(8,SH-30,RGB(170,170,170),"L/R: sayfa  Yukari/Asagi: sec  X: al  O: geri");
    if(msgT>0) text(8,SH-16,RGB(255,213,79),msg);
}
static void drawAch(void){
    drawScene(sv.theme); rect(0,0,SW,SH,RGB(10,5,4));
    int done=0; for(int i=0;i<NACH;i++) if(sv.ach&(1u<<i)) done++;
    textf(8,6,RGB(255,213,79),"BASARIMLAR %d/%d   O: geri",done,NACH);
    for(int i=0;i<NACH;i++){
        int d=(sv.ach&(1u<<i))!=0; int y=26+i*19;
        textf(14,y,d?RGB(140,255,152):RGB(150,150,150),"%s %s",d?"[X]":"[ ]",achName[i]);
        textf(410,y,d?RGB(140,255,152):RGB(255,213,79),"+%d",achRew[i]);
    }
}
static void drawMenu(void){
    drawScene(sv.theme); rect(0,0,SW,SH,RGB(12,5,4));
    textc(24,RGB(255,213,79),"A R R O W   S U R V I V A L");
    textc(40,RGB(125,232,255),"PSP EDITION");
    textf(120,66,RGB(255,255,255),"Rekor:%d  Oldurme:%d  Boss:%d",sv.record,sv.kills,sv.bosses);
    textf(120,82,RGB(255,213,79),"Altin:%d  Elmas:%d  Tema:%s",sv.gold,sv.diamonds,TH[sv.theme].name);
    const char *it[4]={"Oyuna Basla","Market","Basarimlar","Zorluk"}; char t[40];
    for(int i=0;i<4;i++){
        if(i==3) snprintf(t,40,"Zorluk: < %s >",diffName[sv.diff]); else snprintf(t,40,"%s",it[i]);
        if(i==menuSel) rect(SW/2-110,112+i*22-4,220,16,RGB(90,40,10));
        textc(112+i*22,i==menuSel?RGB(255,255,255):RGB(170,170,170),t);
    }
    textc(SH-46,RGB(150,150,150),"Oyun: Analog=yuru/nisan  X=ger+birak  KARE=ok modu");
    textc(SH-32,RGB(150,150,150),"UCGEN=super guc  START=duraklat");
    if(msgT>0) textc(100,RGB(255,213,79),msg);
}
static void drawOver(void){
    drawPlay(); rect(110,66,260,120,RGB(0,0,0)); rect(112,68,256,116,RGB(40,10,10));
    textc(80,RGB(255,80,80),"OYUN BITTI");
    textf(130,102,RGB(255,255,255),"Skor:%d  Rekor:%d  Sv:%d",score,sv.record,level);
    textf(130,118,RGB(255,213,79),"Kazanilan altin: %d",sessGold);
    textf(130,134,RGB(200,200,200),"Oldurme: %d  Sure: %d sn",runKills,runFrames/60);
    textc(160,RGB(255,255,255),"X: Menuye don");
}

/* ================= ana ================= */
static int exit_cb(int a,int b,void *c){ writeSave(); sceKernelExitGame(); return 0; }
static int cb_thread(SceSize args,void *argp){
    int id=sceKernelCreateCallback("exit",exit_cb,NULL);
    sceKernelRegisterExitCallback(id); sceKernelSleepThreadCB(); return 0;
}

int main(void){
    int th=sceKernelCreateThread("cb",cb_thread,0x11,0xFA0,0,NULL);
    if(th>=0) sceKernelStartThread(th,0,NULL);
    pspDebugScreenInit();
    pspDebugScreenEnableBackColor(0);
    sceDisplaySetMode(0,SW,SH);
    fbuf[0]=(u32*)0x44000000; fbuf[1]=(u32*)(0x44000000+LINE*SH*4);
    sceCtrlSetSamplingCycle(0); sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    srand(sceKernelGetSystemTimeLow());
    loadSave(); initWeather();
    SceCtrlData pad; u32 old=0; static float ws=0;
    while(1){
        sceCtrlPeekBufferPositive(&pad,1);
        u32 pr=pad.Buttons & ~old; old=pad.Buttons; ws+=0.1f;
        for(int i=0;i<40;i++){ wy[i]+=(state==ST_PLAY?0:0.4f); if(wy[i]>GROUND){ wy[i]=0; wx[i]=frand(0,SW); } }
        if(msgT>0 && state!=ST_PLAY) msgT--;
        if(state==ST_MENU){
            if(pr&PSP_CTRL_UP) menuSel=(menuSel+3)%4;
            if(pr&PSP_CTRL_DOWN) menuSel=(menuSel+1)%4;
            if(menuSel==3 && (pr&(PSP_CTRL_LEFT|PSP_CTRL_RIGHT|PSP_CTRL_CROSS))){
                int d=(pr&PSP_CTRL_LEFT)?-1:1; int n=(sv.diff+d+4)%4;
                if(n==3 && sv.record<500){ say("Kabus icin 500 rekor gerek"); if(d>0) n=0; else n=2; }
                sv.diff=n; writeSave(); checkAch();
            } else if(pr&PSP_CTRL_CROSS){
                if(menuSel==0) startRun();
                else if(menuSel==1){ state=ST_SHOP; shopPg=0; shopSel=0; msgT=0; resetArm=0; }
                else if(menuSel==2) state=ST_ACH;
            }
            if(pr&PSP_CTRL_SQUARE){ state=ST_SHOP; shopPg=0; shopSel=0; msgT=0; }
            drawMenu();
        } else if(state==ST_SHOP){
            if(pr&PSP_CTRL_LTRIGGER){ shopPg=(shopPg+4)%5; shopSel=0; resetArm=0; }
            if(pr&PSP_CTRL_RTRIGGER){ shopPg=(shopPg+1)%5; shopSel=0; resetArm=0; }
            if(pr&PSP_CTRL_UP) shopSel=(shopSel+pageCount[shopPg]-1)%pageCount[shopPg];
            if(pr&PSP_CTRL_DOWN) shopSel=(shopSel+1)%pageCount[shopPg];
            if(pr&PSP_CTRL_CROSS) buyItem(shopPg,shopSel);
            if(pr&PSP_CTRL_CIRCLE){ state=ST_MENU; writeSave(); }
            drawShop();
        } else if(state==ST_ACH){
            if(pr&(PSP_CTRL_CIRCLE|PSP_CTRL_CROSS)) state=ST_MENU;
            drawAch();
        } else if(state==ST_PLAY){
            updatePlay(&pad,pr);
            if(state==ST_PLAY) drawPlay(); else drawOver();
        } else {
            if(pr&PSP_CTRL_CROSS) state=ST_MENU;
            drawOver();
        }
        flip();
    }
    return 0;
}
