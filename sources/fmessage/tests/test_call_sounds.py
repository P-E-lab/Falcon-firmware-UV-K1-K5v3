#!/usr/bin/env python3
"""Build first; checks blobs, exact tone playback and menu/configuration logic.
Native C harnesses exercise the production source with a mocked radio API.
"""
import os, re, struct, subprocess, sys, tempfile, zlib
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from call_sounds import SOUNDS,USER_MAGIC,DEFAULT_SOUND

def defs(p):
    return {k:int(v) for k,v in re.findall(r'^#define (\w+) (\d+)u',p.read_text(),re.M)}
md=defs(ROOT/'fmessage/fmessage_assets.h'); od=defs(ROOT/'fmsgoption/fmsgoption_assets.h')
ma=(ROOT/'fmessage/fmessage_assets.bin').read_bytes(); oa=(ROOT/'fmsgoption/fmsgoption_assets.bin').read_bytes()
assert len(SOUNDS)==17
assert struct.unpack_from('<I',ma)[0]==USER_MAGIC==struct.unpack_from('<I',oa,24)[0]
assert ma[7]==list(SOUNDS).index(DEFAULT_SOUND)
assert re.search(r'#define FMU_SOUNDS\s+17u',(ROOT/'fmessage_user.h').read_text())
for i,(name,notes) in enumerate(SOUNDS.items()):
    off,n=struct.unpack_from('BB',ma,md['MELODY_INDEX']+2*i)
    assert n*2<=md['MELODY_MAX']
    assert list(struct.unpack_from('<'+'H'*(n*2),ma,md['MELODY']+4*off))==notes
    at=od['STR']+(od['X_SOUND']+i)*od['STR_STRIDE']
    assert oa[at:at+od['STR_STRIDE']].split(b'\0')[0].decode()=='%02u %s'%(i+1,name)
    assert 127-6*len(name)>=1+6*len('CALL SOUND:')

for folder,codepath,out in [('fmessage','fmessage','FMessage'),('fmsgoption','fmsgoption','FMessageOption')]:
    data=(ROOT/folder/(codepath+'.bin')).read_bytes(); assets=(ROOT/folder/(codepath+'_assets.bin')).read_bytes()
    blob=(ROOT/folder/(out+'.app')).read_bytes()
    h=struct.unpack('<4sHBBIIHH16s16sIIHH',blob[:64])
    assert h[0]==b'FAP1' and h[3]==3 and h[4]==len(data)<=4096
    assert h[5]==zlib.crc32(data) and h[-2]==len(assets)<=3840
    assert h[-1]==zlib.crc32(assets)&65535
    assert blob[64:]==data+assets

common=r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned char assets[3840];
static unsigned asset_len;
static uint16_t read_asset(uint16_t off,void *buf,uint16_t len) {
    assert((unsigned)off+len<=asset_len); memcpy(buf,assets+off,len); return len;
}
static void load(const char *path) {
    FILE *f=fopen(path,"rb");assert(f);asset_len=fread(assets,1,sizeof assets,f);fclose(f);
}
static void nop(void) {}
static void boolean(bool b) {(void)b;}
static void af(uint8_t b) {(void)b;}
static void regwrite(uint8_t r,uint16_t v) {(void)r;(void)v;}
static void delay(uint32_t t) {(void)t;}
'''
melody=r'''
static unsigned tones,tx_begin,tx_done;
static uint16_t *expected;
static void tone(uint16_t hz,uint16_t ms) {
    assert(expected[2*tones]==hz && expected[2*tones+1]==ms); tones++;
}
static void txstart(void) {tx_begin++;}
static void txend(void) {tx_done++;}
int main(int argc,char **argv) {
    assert(argc==2);load(argv[1]);
    app_api_t api={.asset_read=read_asset,.tx_set_params=txstart,.tx_end=txend,
       .tx_mute=boolean,.set_af=af,.bk_write=regwrite,.delay_ms=delay,.play_tone_raw=tone};
    static struct data data;
    gapi=&api;D=&data; /* FMessage 3.0: the state lives with the data */
    for(unsigned i=0;i<=FMU_SOUNDS;i++) {
        unsigned j=i<FMU_SOUNDS?i:0;
        uint8_t *ix=assets+MELODY_INDEX+2*j;
        uint16_t aligned[MELODY_MAX];
        memcpy(aligned,assets+MELODY+4*ix[0],4*ix[1]);expected=aligned;
        tones=0;g.user[FMU_CALL]=i;melody();assert(tones==ix[1]);
    }
    assert(tx_begin==FMU_SOUNDS+1 && tx_done==tx_begin);
    return 0;
}
'''
option=r'''
static uint8_t fb[8][128],status[128],config[8];
static unsigned keypos,saves;
static void cfgload(uint8_t *b,uint8_t n) {assert(n==8);memcpy(b,config,n);}
static void cfgsave(const uint8_t *b,uint8_t n) {assert(n==8);memcpy(config,b,n);saves++;}
static uint8_t key(void) {
    g.sel=2;
    static const uint8_t keys[]={APP_KEY_MENU,APP_KEY_INVALID,APP_KEY_EXIT};
    assert(keypos<3);return keys[keypos++];
}
static uint32_t ticks(void) {return keypos*100;}
static void bold(const char *s,uint8_t a,uint8_t b,uint8_t c) {(void)s;(void)a;(void)b;(void)c;}
static void tiny(const char *s,uint8_t a,uint8_t b,bool c,bool d) {(void)s;(void)a;(void)b;(void)c;(void)d;}
int main(int argc,char **argv) {
    assert(argc==2);load(argv[1]);
    app_api_t api={.abi_major=1,.api_level=3,.asset_read=read_asset,.cfg_load=cfgload,.cfg_save=cfgsave,
       .get_key=key,.ticks_ms=ticks,.backlight_on=nop,.backlight_update=nop,.delay_ms=delay,
       .fb=fb,.status_line=status,.display_clear=nop,.blit_status=nop,.blit_full=nop,.print_bold=bold,.print_tiny=tiny};
    for(unsigned i=0;i<FMU_SOUNDS;i++) {
        memset(&g,0,sizeof g);keypos=saves=0;
        uint32_t magic=FMU_MAGIC;memcpy(config,&magic,4);config[4]='A';config[5]='A';config[6]=1;config[7]=i;
        app_main(&api);assert(saves==1 && config[7]==(i+1)%FMU_SOUNDS);
    }
    memset(&g,0,sizeof g);keypos=saves=0;
    uint32_t old=0x31534d46;memcpy(config,&old,4);
    app_main(&api);assert(!g.ok && saves==0);
    return 0;
}
'''
with tempfile.TemporaryDirectory() as tmp:
    for name,body in [('fmessage',melody),('fmsgoption',option)]:
        src=Path(tmp)/(name+'.c');exe=Path(tmp)/name
        src.write_text('#include "'+str(ROOT/name/(name+'_app.c'))+'"\n'+common+body)
        subprocess.run(['gcc','-std=gnu11','-O0','-fno-builtin','-D_Static_assert(...)=',str(src),'-o',str(exe)],check=True)
        subprocess.run([str(exe),str(ROOT/name/(name+'_assets.bin'))],check=True)
print('OK: 17 names/tones, selection wrap, save, old-version rejection, invalid-index fallback, app sizes and CRCs')
