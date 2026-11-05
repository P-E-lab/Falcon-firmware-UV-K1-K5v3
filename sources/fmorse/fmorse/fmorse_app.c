/* FMorse (formerly Morse Light / Radio), 1.7. Apache-2.0.
 * Falcon ABI 1 / API 2, PY32F071 UV-K1 and UV-K5 V3.
 * No resident symbols, no flash writes while executing. */
#include <stdint.h>
#include <stdbool.h>
#include "../app_api.h"
#include "morse_assets.h"
#define A g.api
#define MAX 32
#define LED_BSRR (*(volatile uint32_t *)0x50000818u)
static struct {
 const app_api_t *api;
 char custom[33], edit[33], msg[33];
 uint32_t pendingAt, heldAt, batteryAt;
 uint8_t row, choice, speed, repeat, output, previous, cursor, tapKey, tapIndex, original;
 bool running, editing, adjusting, help, pending, longDone, tx;
 bool sql, css, rxOpen;
 uint8_t codeKind;
} g;
static uint8_t len(const char *s){uint8_t n=0;while(s[n])n++;return n;}
static void copy(char *d,const char *s){while((*d++=*s++));}
static void txt(uint16_t off,char *s){A->asset_read(off,s,TXT_SIZE);}
static void line(uint16_t off,uint8_t row){char s[TXT_SIZE];txt(off,s);A->print_normal(s,2,0,row);}
static void light(bool on){LED_BSRR=on?(1u<<13):(1u<<29);}
static void off(void){light(false);if(g.tx)A->tx_mute(true);}
static void rxClose(void){A->audio_path(false);A->led(false);g.rxOpen=false;}
static void end(void){off();if(g.tx){A->tx_end();g.tx=false;g.sql=false;g.css=false;}}
/* The loader has already tuned the selected VFO, its filters and code decoder.
 * Service the same BK squelch/CSS interrupts as the resident receive loop.
 * On BK4829, LOST denotes opening the squelch (same convention as F4HWN). */
static void receive(void){
 if(g.tx)return;
 for(uint8_t n=0;n<4;n++){
  uint16_t status=A->bk_read(0x0c);if(!(status&1))break;
  A->bk_write(0x02,0);uint16_t irq=A->bk_read(0x02);
  if(irq&4)g.sql=true;
  if(irq&8){g.sql=false;g.css=false;}
  if(g.codeKind==1){if(irq&0x40)g.css=true;if(irq&0x80)g.css=false;}
  if(g.codeKind==2){if(irq&0x100)g.css=((status>>14)&3)==1;if(irq&0x200)g.css=false;}
  if(irq&0x400){g.sql=false;g.css=false;}
 }
 bool open=g.sql && (!g.codeKind || g.css);
 if(open!=g.rxOpen){g.rxOpen=open;A->audio_path(open);A->led(open);}
}
static uint8_t *setting(void){return g.row==0?&g.choice:g.row==1?&g.speed:g.row==2?&g.repeat:&g.output;}
static void commit(void){if(g.pending){g.pending=false;if(g.cursor<MAX)g.cursor++;}}
static void message(void){if(g.choice==4)copy(g.msg,g.custom);else {char s[TXT_SIZE];txt(PRESETS+g.choice*TXT_SIZE,s);copy(g.msg,s);}}
static void service(void){receive();uint32_t now=A->ticks_ms();A->backlight_update();if(now-g.batteryAt>=500){g.batteryAt=now;A->battery_sample();}}
static void footer(void){char s[TXT_SIZE];txt(T_KEYS_FOOT,s);A->print_normal(s,126-len(s)*7,0,6);}
static void draw(void){
 char s[TXT_SIZE];A->display_clear();A->status_clear();A->draw_battery();
 txt(g.help?T_KEYS:g.editing?T_EDIT:T_TITLE,s);A->print_bold(s,g.help?0:2,g.help?127:0,0);
 if(g.help){
  for(uint8_t i=0;i<(g.editing?5:4);i++){txt((g.editing?EDIT_HELP:MAIN_HELP)+i*TXT_SIZE,s);A->print_normal(s,2,0,i+(g.editing?1:2));}
  /* Five 8px rows start at y=12; shift the body by four pixels,
   * preserving the centered title in framebuffer page zero. */
  if(g.editing)for(uint8_t r=6;r>0;r--)for(uint8_t x=0;x<128;x++)A->fb[r][x]=(A->fb[r][x]<<4)|(r>1?A->fb[r-1][x]>>4:0);
 }
 else if(g.editing){
  uint8_t n=len(g.edit);for(uint8_t r=0;r<2;r++){uint8_t k=0;while(k<16 && r*16+k<n){s[k]=g.edit[r*16+k];k++;}s[k]=0;A->print_normal(s,2,0,r+2);}
  uint8_t row=g.cursor<16?2:3,x=2+(g.cursor<16?g.cursor:g.cursor-16)*7;
  A->draw_line(A->fb,x,row*8+7,x+5,row*8+7,true);
  s[0]='0'+n/10;s[1]='0'+n%10;s[2]='/';s[3]='3';s[4]='2';s[5]=0;A->print_normal(s,2,0,5);
  footer();
 }else{
  for(uint8_t i=0;i<5;i++){
   txt(LABELS+i*TXT_SIZE,s);uint8_t n=len(s);char value[TXT_SIZE];
   uint16_t v=i==0?PRESETS+g.choice*TXT_SIZE:i==1?SPEEDS+g.speed*TXT_SIZE:i==2?MODES+g.repeat*TXT_SIZE:i==3?OUTPUTS+g.output*TXT_SIZE:T_GO;
   txt(v,value);copy(s+n,value);if(i==4)A->print_bold(s,9,0,i+1);else A->print_normal(s,9,0,i+1);
   if(i==g.row)A->print_normal(g.adjusting?"*":">",0,0,i+1);
  }footer();
 }A->blit_status();A->blit_full();
}
static void progress(uint8_t pos){
 char s[17];A->display_clear();A->status_clear();A->draw_battery();line(T_SENDING,0);
 uint8_t n=len(g.msg);for(uint8_t r=0;r<2;r++){uint8_t k=0;while(k<16 && r*16+k<n){s[k]=g.msg[r*16+k];k++;}s[k]=0;A->print_normal(s,2,0,r+1);}
 s[0]=pos<n?g.msg[pos]:' ';s[1]=0;A->print_string(s,56,0,3,8);line(T_STOP,6);A->blit_status();A->blit_full();
}
/* Return 1 = stop and keep app, 2 = stop and leave. Poll every 10 ms. */
static uint8_t wait(uint16_t ms){uint32_t start=A->ticks_ms();while(A->ticks_ms()-start<ms){
 A->delay_ms(10);service();uint8_t k=A->get_key();if(k!=g.previous){g.previous=k;if(k==APP_KEY_MENU)return 1;if(k==APP_KEY_EXIT)return 2;}
 if(g.tx && A->tx_state())return 1;
 }return 0;}
static uint8_t morse(char c){uint8_t i,code=0;if(c>='A'&&c<='Z')i=c-'A';else if(c>='0'&&c<='9')i=26+c-'0';else {const char *p=".,-/?";i=36;while(*p && *p!=c){p++;i++;}if(!*p)return 0;}A->asset_read(MORSE+i,&code,1);return code;}
static uint8_t send(void){
 message();if(!g.msg[0])return 0;uint16_t unit=g.speed==0?240:g.speed==1?120:80;uint8_t stop=0;
 do{
  if(g.output){A->battery_sample();if(A->tx_state()){A->display_clear();line(T_DENIED,2);A->blit_full();stop=wait(1500);return stop;}
   rxClose();g.tx=true;A->tx_set_params();A->tx_tone(1000);A->tx_mute(true);stop=wait(1000);
  }
  for(uint8_t i=0;g.msg[i]&&!stop;i++){
   progress(i);uint8_t code=morse(g.msg[i]);
   if(!code){stop=wait(unit*4);continue;}
   uint8_t bit=128;while(!(code&bit))bit>>=1;
   for(bit>>=1;bit&&!stop;bit>>=1){if(g.output)A->tx_mute(false);else light(true);stop=wait(unit*((code&bit)?3:1));off();if(!stop)stop=wait(unit);}
   if(!stop && !(g.choice==1 && i<2))stop=wait(unit*2);
  }end();if(!stop&&g.repeat){progress(len(g.msg));stop=wait(3000);}
 }while(g.repeat&&!stop);return stop;
}
static void erase(void){commit();if(!g.cursor)return;g.cursor--;uint8_t i=g.cursor;do{g.edit[i]=g.edit[i+1];}while(g.edit[i++] && i<MAX);}
static void digit(uint8_t key){
 char chars[TXT_SIZE];txt(KEYMAP+key*TXT_SIZE,chars);uint8_t count=len(chars);
 if(g.pending && g.tapKey==key){if(++g.tapIndex>=count)g.tapIndex=0;}
 else {commit();if(g.cursor>=MAX)return;g.tapIndex=0;g.tapKey=key;}
 uint8_t n=len(g.edit);g.edit[g.cursor]=chars[g.tapIndex];if(g.cursor>=n)g.edit[g.cursor+1]=0;
 g.pending=true;g.pendingAt=A->ticks_ms();
}
__attribute__((section(".text.entry"),used)) void app_main(const app_api_t *api){
 A=api;g.running=true;g.previous=APP_KEY_INVALID;g.speed=0;uint8_t cfg[5];A->cfg_load(cfg,5);
 if(((cfg[0]==0x4e && cfg[1]<5) || (cfg[0]==0x4d && cfg[1]<7)) && cfg[2]<3 && cfg[3]<2 && cfg[4]<2){
  g.choice=cfg[0]==0x4e?cfg[1]:cfg[1]==1?1:cfg[1]==4?3:cfg[1]==6?4:0;
  g.speed=cfg[2];g.repeat=cfg[3];g.output=cfg[4];
 }
 uint16_t mask=A->bk_read(0x3f);g.codeKind=(mask&0xc0)?1:(mask&0x300)?2:0;
 rxClose();A->backlight_on();light(false);draw();
 while(g.running){
  A->delay_ms(10);service();uint32_t now=A->ticks_ms();uint8_t k=A->get_key();bool dirty=false;
  if(g.pending && now-g.pendingAt>=1000){commit();dirty=true;}
  if(g.editing&&!g.help && k==APP_KEY_STAR && g.previous==k && !g.longDone && now-g.heldAt>=600){g.edit[0]=0;g.cursor=0;g.pending=false;g.longDone=true;dirty=true;}
  if(k!=g.previous){
   if(g.editing&&!g.help && g.previous==APP_KEY_STAR && !g.longDone){erase();dirty=true;}
   g.previous=k;
   if(k!=APP_KEY_INVALID && k!=APP_KEY_WAKE && k!=APP_KEY_SAVER){
    A->backlight_on();dirty=true;
    if(g.help){if(k==APP_KEY_EXIT || k==APP_KEY_F)g.help=false;}
    else if(k==APP_KEY_F)g.help=true;
    else if(g.editing){
     if(k<=APP_KEY_9)digit(k);
     else if(k==APP_KEY_STAR){g.heldAt=now;g.longDone=false;}
     else if(k==APP_KEY_MENU){commit();copy(g.custom,g.edit);g.editing=false;}
     else if(k==APP_KEY_EXIT){g.pending=false;g.editing=false;}
     else if(k==APP_KEY_UP || k==APP_KEY_DOWN){bool pending=g.pending;commit();if(k==APP_KEY_UP){if(g.cursor)g.cursor--;}else if(!pending && g.cursor<len(g.edit))g.cursor++;}
    }else if(k==APP_KEY_EXIT){if(g.adjusting){*setting()=g.original;g.adjusting=false;}else g.running=false;}
    else if(k==APP_KEY_MENU){
     if(g.row==4){if(send()==2)g.running=false;}
     else if(g.adjusting){g.adjusting=false;if(g.row==0 && g.choice==4){copy(g.edit,g.custom);g.cursor=len(g.edit);g.editing=true;}}
     else {g.original=*setting();g.adjusting=true;}
    }else if(k==APP_KEY_UP || k==APP_KEY_DOWN){int8_t dir=A->nav_dir(k);if(g.adjusting){uint8_t *p=setting();uint8_t max=g.row==0?5:g.row==1?3:2;*p=dir>0?(*p+1>=max?0:*p+1):(*p?*p-1:max-1);}else g.row=k==APP_KEY_DOWN?(g.row==4?0:g.row+1):(g.row?g.row-1:4);}
   }
  }if(dirty&&g.running)draw();
 }end();rxClose();cfg[0]=0x4e;cfg[1]=g.choice;cfg[2]=g.speed;cfg[3]=g.repeat;cfg[4]=g.output;A->cfg_save(cfg,5);
}
