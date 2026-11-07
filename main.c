#include <pspkernel.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspctrl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

PSP_MODULE_INFO("ArrowSurvival", 0, 1, 1);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER);

#define SW 480
#define SH 272
#define LINE 512
#define GROUND 220
#define PX 40
#define RGB(r,g,b) (0xFF000000u | ((b)<<16) | ((g)<<8) | (r))
#define SAVEFILE "ms0:/PSP/GAME/ArrowSurvival/save.dat"

static u32 *fbuf[2];
static int back = 0;

/* ---------- cizim ---------- */
static void rect(int x,int y,int w,int h,u32 c){
    if(x<0){w+=x;x=0;} if(y<0){h+=y;y=0;}
    if(x+w>SW) w=SW-x; if(y+h>SH) h=SH-y;
    if(w<=0||h<=0) return;
    u32 *b = fbuf[back];
    for(int j=0;j<h;j++){ u32 *p=b+(y+j)*LINE+x; for(int i=0;i<w;i++) p[i]=c; }
}
static void text(int x,int y,u32 c,const char *s){
    pspDebugScreenSetBase(fbuf[back]);
    for(;*s;s++,x+=8) pspDebugScreenPutChar(x,y,c,(u8)*s);
}
static void textf(int x,int y,u32 c,const char *fmt,int a,int b){
    char t[64]; snprintf(t,sizeof(t),fmt,a,b); text(x,y,c,t);
}
static void flip(void){
    sceDisplayWaitVblankStart();
    sceDisplaySetFrameBuf(fbuf[back],LINE,PSP_DISPLAY_PIXEL_FORMAT_8888,0);
    back^=1;
}

/* ---------- kayit ---------- */
typedef struct {
    int magic, gold, diamonds, record;
    int hpLv, powLv, spdLv, bombOwned, skin;
    int skinOwned[4];
    int kills, bosses;
} Save;
static Save sv;
static const char *skinName[4]={"Kirmizi","Mavi","Altin","Yesil"};
static const int skinPrice[4]={0,220,380,295};
static const u32 skinCol[4]={RGB(255,77,77),RGB(41,182,246),RGB(255,215,0),RGB(102,187,106)};

static void defSave(void){
    memset(&sv,0,sizeof(sv)); sv.magic=0xA770; sv.gold=500; sv.diamonds=50; sv.skinOwned[0]=1;
}
static void loadSave(void){
    FILE *f=fopen(SAVEFILE,"rb"); defSave();
    if(f){ Save t; if(fread(&t,sizeof(t),1,f)==1 && t.magic==0xA770) sv=t; fclose(f); }
}
static void writeSave(void){
    FILE *f=fopen(SAVEFILE,"wb"); if(f){ fwrite(&sv,sizeof(sv),1,f); fclose(f); }
}

/* ---------- oyun verisi ---------- */
typedef struct { float x,y,vx,vy; int bomb,on; } Arrow;
typedef struct { float x; int hp,maxhp,type,on,cd,flash; float spd; } Enemy; /* 0 normal 1 tank 2 okcu 3 boss */
typedef struct { float x,y; int on; } EArrow;

#define MA 32
#define ME 20
#define MEA 24
static Arrow ar[MA]; static Enemy en[ME]; static EArrow ea[MEA];
static int score,level,hp,maxhp,spawnT,charge,charging,bombMode,power,lastBossLv;
static int combo,comboT,superCh,hitFlash,sessGold,paused;
static float ang;
static char msg[48]; static int msgT;
static float shake;

enum { ST_MENU, ST_PLAY, ST_SHOP, ST_OVER };
static int state=ST_MENU, shopSel=0;

static float frand(float a,float b){ return a+(float)rand()/RAND_MAX*(b-a); }
static void say(const char *s){ strncpy(msg,s,47); msg[47]=0; msgT=90; }

static void startRun(void){
    memset(ar,0,sizeof(ar)); memset(en,0,sizeof(en)); memset(ea,0,sizeof(ea));
    score=0; level=1; maxhp=100+sv.hpLv*20; hp=maxhp; spawnT=60; charge=0; charging=0;
    bombMode=0; power=1+sv.powLv; lastBossLv=0; combo=0; comboT=0; superCh=0;
    hitFlash=0; sessGold=0; ang=0.15f; msgT=0; shake=0; paused=0; state=ST_PLAY;
}
static void hurt(int d){
    hp-=d; hitFlash=14; shake=6;
    if(hp<=0){
        hp=0; state=ST_OVER;
        if(score>sv.record) sv.record=score;
        writeSave();
    }
}
static void addDrops(int type){
    int g = type==3 ? 12+rand()%6 : (type==1 ? 2+rand()%3 : 1+rand()%2);
    g += combo/3; sv.gold+=g; sessGold+=g;
    int dc = type==3 ? 100 : 6;
    if(rand()%100<dc) sv.diamonds+= type==3 ? 3 : 1;
}
static void killEnemy(Enemy *e){
    e->on=0; addDrops(e->type);
    int pts = e->type==3?200 : e->type==1?18 : e->type==2?14 : 10;
    score+=pts; sv.kills++; if(e->type==3){ sv.bosses++; say("BOSS YENILDI!"); }
    superCh+=e->type==3?25:4; if(superCh>100) superCh=100;
    int nl=1+score/100; if(nl>level){ level=nl; char t[32]; snprintf(t,32,"DALGA %d",level); say(t); }
}
static void damageEnemy(Enemy *e,int d){
    e->hp-=d; e->flash=5;
    if(e->hp<=0) killEnemy(e);
}
static void explode(float x,float y,int d){
    shake=10;
    for(int i=0;i<ME;i++) if(en[i].on){
        float ey=GROUND-20; float dx=en[i].x-x, dy=ey-y;
        if(sqrtf(dx*dx+dy*dy)<60) damageEnemy(&en[i],d);
    }
    for(int i=0;i<24;i++){ int px=(int)(x+frand(-30,30)), py=(int)(y+frand(-30,30)); rect(px,py,3,3,RGB(255,120,0)); }
}
static void spawnEnemy(void){
    int idx=-1; for(int i=0;i<ME;i++) if(!en[i].on){ idx=i; break; } if(idx<0) return;
    Enemy *e=&en[idx]; memset(e,0,sizeof(*e)); e->on=1; e->x=SW+20;
    float eff=level*1.2f;
    float r=frand(0,1);
    if(r<0.15f+eff*0.015f){ e->type=1; e->hp=e->maxhp=4+level/4; e->spd=0.5f+eff*0.02f; }
    else if(r<0.35f+eff*0.02f){ e->type=2; e->hp=e->maxhp=2; e->spd=0.6f+eff*0.02f; e->cd=60; }
    else { e->type=0; e->hp=e->maxhp=(rand()%4==0)?2:1; e->spd=0.7f+eff*0.04f+frand(0,0.5f); }
}
static void spawnBoss(void){
    int idx=-1; for(int i=0;i<ME;i++) if(!en[i].on){ idx=i; break; } if(idx<0) return;
    Enemy *e=&en[idx]; memset(e,0,sizeof(*e)); e->on=1; e->type=3; e->x=SW+30;
    e->hp=e->maxhp=26+(int)(level*4.4f); e->spd=0.35f+level*0.01f; e->cd=70;
    lastBossLv=level; say("BOSS GELDI!"); shake=14;
}
static void shoot(void){
    int idx=-1; for(int i=0;i<MA;i++) if(!ar[i].on){ idx=i; break; } if(idx<0) return;
    float p=5.0f+charge*0.22f+sv.spdLv*0.7f;
    Arrow *a=&ar[idx]; a->on=1; a->x=PX+14; a->y=GROUND-30; a->vx=cosf(-ang)*p; a->vy=sinf(-ang)*p; a->bomb=bombMode;
}
static void superPower(void){
    if(superCh<100){ say("Guc dolmadi!"); return; }
    superCh=0;
    for(int k=0;k<10;k++){
        int idx=-1; for(int i=0;i<MA;i++) if(!ar[i].on){ idx=i; break; } if(idx<0) break;
        float an=frand(-0.5f,0.3f); Arrow *a=&ar[idx];
        a->on=1; a->x=PX+14; a->y=GROUND-30; a->vx=cosf(an)*9; a->vy=sinf(an)*9; a->bomb=0;
    }
    say("OK AKINI!");
}

static void updatePlay(SceCtrlData *pad,u32 pressed){
    if(pressed&PSP_CTRL_START){ paused=!paused; }
    if(paused) return;
    int ay=pad->Ly-128;
    if(abs(ay)>30) ang-=ay/128.0f*0.03f;
    if(pad->Buttons&PSP_CTRL_UP) ang+=0.02f;
    if(pad->Buttons&PSP_CTRL_DOWN) ang-=0.02f;
    if(ang>1.0f) ang=1.0f; if(ang<-0.2f) ang=-0.2f;
    if(pad->Buttons&PSP_CTRL_CROSS){ charging=1; if(charge<40) charge++; }
    else if(charging){ shoot(); charging=0; charge=0; }
    if((pressed&PSP_CTRL_SQUARE) && sv.bombOwned){ bombMode=!bombMode; say(bombMode?"Bombali ok":"Normal ok"); }
    if(pressed&PSP_CTRL_TRIANGLE) superPower();
    if(hitFlash>0) hitFlash--; if(comboT>0 && --comboT==0) combo=0; if(msgT>0) msgT--; shake*=0.85f;

    spawnT--;
    int alive=0, bossOn=0; for(int i=0;i<ME;i++) if(en[i].on){ alive++; if(en[i].type==3) bossOn=1; }
    int maxE = 4+level/2; if(maxE>10) maxE=10;
    if(!bossOn && spawnT<=0 && alive<maxE){ spawnEnemy(); spawnT=(int)(90-level*3); if(spawnT<25) spawnT=25; }
    if(!bossOn && level%5==0 && lastBossLv!=level) spawnBoss();

    for(int i=0;i<MA;i++) if(ar[i].on){
        Arrow *a=&ar[i]; a->x+=a->vx; a->y+=a->vy; a->vy+=0.09f;
        if(a->x>SW+10||a->x<-10||a->y>GROUND){
            if(a->bomb && a->y>GROUND) explode(a->x,GROUND,power*3);
            else combo=0;
            a->on=0; continue;
        }
        for(int j=0;j<ME;j++) if(en[j].on){
            Enemy *e=&en[j]; int w=e->type==3?34:(e->type==1?18:12), h=e->type==3?70:(e->type==1?38:30);
            if(a->x>e->x-w && a->x<e->x+w && a->y>GROUND-h && a->y<GROUND+4){
                a->on=0; combo++; comboT=180;
                if(a->bomb) explode(a->x,a->y,power*3); else damageEnemy(e,power);
                break;
            }
        }
    }
    for(int i=0;i<ME;i++) if(en[i].on){
        Enemy *e=&en[i]; if(e->flash>0) e->flash--;
        if(e->type==2 && e->x<PX+220) { /* okcu durur ve atar */ }
        else e->x-=e->spd;
        if(e->type==2||e->type==3){
            if(--e->cd<=0){
                for(int k=0;k<MEA;k++) if(!ea[k].on){ ea[k].on=1; ea[k].x=e->x-10; ea[k].y=GROUND-30-(e->type==3?10:0); break; }
                e->cd = e->type==3 ? 55 : 110;
            }
        }
        if(e->x<PX+14){
            if(e->type==3){ hurt(25); e->x+=90; }
            else { hurt(e->type==1?25:15); e->on=0; }
        }
    }
    for(int i=0;i<MEA;i++) if(ea[i].on){
        ea[i].x-=3.2f;
        if(ea[i].x<PX+10){ ea[i].on=0; hurt(9); }
    }
    if(hp>0 && (rand()%1500==0) && hp<maxhp) { hp++; }
}

/* ---------- cizim ---------- */
static void drawPlay(void){
    int sx=(int)frand(-shake,shake), sy=(int)frand(-shake,shake);
    rect(0,0,SW,GROUND,RGB(60,16,8)); rect(0,GROUND-60,SW,60,RGB(120,34,10));
    rect(0,GROUND,SW,SH-GROUND,RGB(42,13,8));
    for(int i=0;i<6;i++){ int mx=i*90+20; for(int k=0;k<40;k++) rect(mx+k/2+sx,GROUND-70+k+sy,70-k,1,RGB(22,7,7)); }
    rect(PX-6+sx,GROUND-50+sy,12,40,RGB(61,36,20)); rect(PX-5+sx,GROUND-64+sy,10,12,RGB(241,196,155));
    rect(PX-5+sx,GROUND-10+sy,4,10,RGB(43,26,16)); rect(PX+1+sx,GROUND-10+sy,4,10,RGB(43,26,16));
    for(int i=0;i<18;i++){ rect(PX+8+(int)(cosf(-ang)*i)+sx, GROUND-30-(int)(sinf(-ang)*i)+sy,2,2,RGB(150,100,40)); }
    if(charging) rect(PX-20,GROUND-80,charge,4,RGB(255,213,79));
    u32 ac=bombMode?RGB(255,154,0):skinCol[sv.skin];
    for(int i=0;i<MA;i++) if(ar[i].on){ Arrow *a=&ar[i]; rect((int)a->x-3+sx,(int)a->y+sy,8,2,ac); rect((int)a->x+5+sx,(int)a->y-1+sy,3,4,RGB(220,220,220)); }
    for(int i=0;i<ME;i++) if(en[i].on){
        Enemy *e=&en[i]; int w,h; u32 c;
        if(e->type==3){ w=34;h=70;c=RGB(120,10,10); }
        else if(e->type==1){ w=18;h=38;c=RGB(90,60,60); }
        else if(e->type==2){ w=12;h=30;c=RGB(120,70,60); }
        else { w=12;h=30;c=RGB(50,50,50); }
        if(e->flash>0) c=RGB(255,110,110);
        rect((int)e->x-w/2+sx,GROUND-h+sy,w,h,c);
        rect((int)e->x-w/2+2+sx,GROUND-h+4+sy,3,3,RGB(255,50,50));
        if(e->type==3||e->type==1){ int bw=w; rect((int)e->x-bw/2+sx,GROUND-h-6+sy,bw,3,RGB(40,0,0)); rect((int)e->x-bw/2+sx,GROUND-h-6+sy,bw*e->hp/e->maxhp,3,RGB(255,60,30)); }
    }
    for(int i=0;i<MEA;i++) if(ea[i].on) rect((int)ea[i].x+sx,(int)ea[i].y+sy,8,2,RGB(255,179,71));
    if(hitFlash>0){ rect(0,0,SW,6,RGB(200,0,0)); rect(0,SH-6,SW,6,RGB(200,0,0)); }
    /* HUD */
    rect(0,0,SW,14,RGB(0,0,0));
    textf(4,3,RGB(255,255,255),"Skor:%d  Lv:%d",score,level);
    textf(150,3,RGB(255,213,79),"Altin:%d  Elmas:%d",sv.gold,sv.diamonds);
    textf(360,3,RGB(255,255,255),"Rekor:%d",sv.record,0);
    rect(4,SH-14,104,8,RGB(60,0,0)); rect(5,SH-13,102*hp/maxhp,6,RGB(255,60,30));
    textf(112,SH-15,RGB(255,255,255),"Can %d/%d",hp,maxhp);
    textf(250,SH-15,RGB(125,232,255),"Guc %d%%",superCh,0);
    if(combo>=3) textf(380,SH-15,RGB(255,200,0),"KOMBO x%d",combo,0);
    if(bombMode) text(4,18,RGB(255,154,0),"[BOMBALI OK]");
    if(msgT>0) text(SW/2-(int)strlen(msg)*4,50,RGB(255,213,79),msg);
    if(paused) text(SW/2-40,120,RGB(255,255,255),"DURAKLATILDI");
}

/* ---------- market ---------- */
#define NSHOP 8
static int price(int i){
    switch(i){
        case 0: return 85+65*sv.hpLv;
        case 1: return 120+78*sv.powLv;
        case 2: return 105+72*sv.spdLv;
        case 3: return sv.bombOwned?0:450;
        case 4: { int n=(sv.skin+1)%4; return sv.skinOwned[n]?0:skinPrice[n]; }
        case 5: return 900;
        case 6: return 12;
    }
    return 0;
}
static void buy(int i){
    int p=price(i);
    switch(i){
        case 0: if(sv.gold<p){say("Yetersiz altin");break;} sv.gold-=p; sv.hpLv++; say("Can artti"); break;
        case 1: if(sv.gold<p){say("Yetersiz altin");break;} sv.gold-=p; sv.powLv++; say("Ok gucu artti"); break;
        case 2: if(sv.gold<p){say("Yetersiz altin");break;} sv.gold-=p; sv.spdLv++; say("Ok hizi artti"); break;
        case 3: if(sv.bombOwned){say("Zaten var");break;} if(sv.gold<p){say("Yetersiz altin");break;} sv.gold-=p; sv.bombOwned=1; say("Bombali ok acildi"); break;
        case 4: { int n=(sv.skin+1)%4; if(!sv.skinOwned[n]){ if(sv.gold<p){say("Yetersiz altin");break;} sv.gold-=p; sv.skinOwned[n]=1; } sv.skin=n; say("Skin secildi"); break; }
        case 5: if(sv.gold<p){say("Yetersiz altin");break;} sv.gold-=p; sv.diamonds+=5; say("+5 elmas"); break;
        case 6: if(sv.diamonds<p){say("Yetersiz elmas");break;} sv.diamonds-=p; sv.gold+=180; say("+180 altin"); break;
        case 7: defSave(); say("Kayit sifirlandi"); break;
    }
    writeSave();
}
static void drawShop(void){
    rect(0,0,SW,SH,RGB(12,6,4));
    text(170,8,RGB(255,213,79),"=== MARKET ===");
    textf(20,26,RGB(255,213,79),"Altin: %d   Elmas: %d",sv.gold,sv.diamonds);
    char t[64]; const char *nm[NSHOP]={"Can +20","Ok Gucu +1","Ok Hizi +1","Bombali Ok","Sonraki Skin","Altin->5 Elmas","12 Elmas->180 Altin","Kayitlari Sifirla"};
    for(int i=0;i<NSHOP;i++){
        u32 c = i==shopSel?RGB(255,255,255):RGB(160,160,160);
        if(i==shopSel) rect(14,46+i*24-3,452,18,RGB(70,30,10));
        int lv[3]={sv.hpLv,sv.powLv,sv.spdLv};
        if(i<3) snprintf(t,64,"%s  (Sv %d)",nm[i],lv[i]);
        else if(i==3) snprintf(t,64,"%s %s",nm[i],sv.bombOwned?"[SAHIPSIN]":"");
        else if(i==4){ int n=(sv.skin+1)%4; snprintf(t,64,"%s: %s (simdi %s)",nm[i],skinName[n],skinName[sv.skin]); }
        else snprintf(t,64,"%s",nm[i]);
        text(20,46+i*24,c,t);
        if(i<=6 && price(i)>0){ snprintf(t,64,"%d",price(i)); text(420,46+i*24,RGB(255,213,79),t); }
    }
    text(20,SH-30,RGB(180,180,180),"Yukari/Asagi: sec   X: al   O: geri");
    if(msgT>0) text(20,SH-16,RGB(255,213,79),msg);
}
static void drawMenu(void){
    rect(0,0,SW,SH,RGB(30,8,4));
    rect(0,SH/2,SW,SH/2,RGB(70,20,6));
    text(150,40,RGB(255,213,79),"A R R O W   S U R V I V A L");
    text(205,56,RGB(125,232,255),"PSP EDITION");
    textf(150,100,RGB(255,255,255),"Rekor: %d    Oldurme: %d",sv.record,sv.kills);
    textf(150,116,RGB(255,213,79),"Altin: %d    Elmas: %d",sv.gold,sv.diamonds);
    text(150,160,RGB(255,255,255),"X      : Oyuna Basla");
    text(150,176,RGB(255,255,255),"KARE   : Market");
    text(30,SH-24,RGB(160,160,160),"Oyunda: Analog/D-pad nisan, X ger+birak, KARE ok modu, UCGEN super guc");
}
static void drawOver(void){
    drawPlay();
    rect(110,70,260,110,RGB(0,0,0)); rect(112,72,256,106,RGB(40,10,10));
    text(200,84,RGB(255,80,80),"OYUN BITTI");
    textf(150,106,RGB(255,255,255),"Skor: %d   Rekor: %d",score,sv.record);
    textf(150,122,RGB(255,213,79),"Bu kosuda kazanilan altin: %d",sessGold,0);
    text(150,150,RGB(255,255,255),"X : Menuye don");
}

/* ---------- ana ---------- */
static int exit_cb(int a,int b,void *c){ sceKernelExitGame(); return 0; }
static int cb_thread(SceSize args,void *argp){
    int id=sceKernelCreateCallback("exit",exit_cb,NULL);
    sceKernelRegisterExitCallback(id); sceKernelSleepThreadCB(); return 0;
}

int main(void){
    int th=sceKernelCreateThread("cb",cb_thread,0x11,0xFA0,0,NULL);
    if(th>=0) sceKernelStartThread(th,0,NULL);
    pspDebugScreenInit();
    sceDisplaySetMode(0,SW,SH);
    fbuf[0]=(u32*)0x44000000; fbuf[1]=(u32*)(0x44000000+LINE*SH*4);
    sceCtrlSetSamplingCycle(0); sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    srand(sceKernelGetSystemTimeLow());
    loadSave();
    SceCtrlData pad; u32 old=0;
    while(1){
        sceCtrlPeekBufferPositive(&pad,1);
        u32 pressed=pad.Buttons & ~old; old=pad.Buttons;
        if(state==ST_MENU){
            if(pressed&PSP_CTRL_CROSS) startRun();
            if(pressed&PSP_CTRL_SQUARE){ state=ST_SHOP; shopSel=0; msgT=0; }
            drawMenu();
        } else if(state==ST_SHOP){
            if(pressed&PSP_CTRL_UP) shopSel=(shopSel+NSHOP-1)%NSHOP;
            if(pressed&PSP_CTRL_DOWN) shopSel=(shopSel+1)%NSHOP;
            if(pressed&PSP_CTRL_CROSS) buy(shopSel);
            if(pressed&PSP_CTRL_CIRCLE) state=ST_MENU;
            if(msgT>0) msgT--;
            drawShop();
        } else if(state==ST_PLAY){
            updatePlay(&pad,pressed);
            if(state==ST_PLAY) drawPlay(); else drawOver();
        } else {
            if(pressed&PSP_CTRL_CROSS) state=ST_MENU;
            drawOver();
        }
        flip();
    }
    return 0;
}
