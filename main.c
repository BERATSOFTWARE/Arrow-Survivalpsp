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
int main(void){
    int th=sceKernelCreateThread("cb",cb_thread,0x11,0xFA0,0,NULL);
    if(th>=0) sceKernelStartThread(th,0,NULL);
    pspDebugScreenInit();
    sceDisplaySetMode(0,SW,SH);
    fbuf[0]=(u32*)0x44000000; fbuf[1]=(u32*)(0x44000000+LINE*SH*4);
    sceCtrlSetSamplingCycle(0); sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    srand(sceKernelLibcTime(NULL));
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
