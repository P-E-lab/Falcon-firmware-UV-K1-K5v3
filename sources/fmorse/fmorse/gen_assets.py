from pathlib import Path
import struct
root=Path(__file__).parent
blob=bytearray(); defs=['#define TXT_SIZE 40']
def texts(name,items):
 defs.append(f'#define {name} {len(blob)}u')
 for s in items:
  b=s.encode('ascii'); assert len(b)<40;blob.extend(b+b'\0'*(40-len(b)))
texts('PRESETS',['OK','SOS','STOP','NO','CUSTOM'])
texts('LABELS',['MESSAGE: ','SPEED: ','SEND: ','OUTPUT: ',''])
texts('SPEEDS',['SLOW','NORMAL','FAST'])
texts('MODES',['ONCE','REPEAT'])
texts('OUTPUTS',['LIGHT','RADIO'])
texts('KEYMAP',[' 0','1.,-/?','ABC2','DEF3','GHI4','JKL5','MNO6','PQRS7','TUV8','WXYZ9'])
texts('EDIT_HELP',['2-9: ABC 0: SPACE','1: SYMBOLS','< >: CURSOR','*: DEL HOLD: CLEAR','M: OK EXIT: CANCEL'])
texts('MAIN_HELP',['< >: SELECT/VALUE','M: EDIT / CONFIRM','START + M: SEND','M: STOP EXIT: QUIT'])
for name,s in {'T_TITLE':'FMORSE','T_KEYS':'KEY INFOS','T_EDIT':'MESSAGE','T_GO':'START','T_KEYS_FOOT':'# Key Infos','T_SENDING':'SENDING','T_STOP':'M: STOP  EXIT: QUIT','T_DENIED':'TX NOT ALLOWED'}.items():texts(name,[s])
defs.append(f'#define MORSE {len(blob)}u')
codes=['.-','-...','-.-.','-..','.','..-.','--.','....','..','.---','-.-','.-..','--','-.','---','.--.','--.-','.-.','...','-','..-','...-','.--','-..-','-.--','--..','-----','.----','..---','...--','....-','.....','-....','--...','---..','----.','.-.-.-','--..--','-....-','-..-.','..--..']
for code in codes:
 v=1
 for c in code:v=(v<<1)|(c=='-')
 blob.append(v)
(root/'morse_assets.h').write_text('\n'.join(defs)+'\n');(root/'morse_assets.bin').write_bytes(blob)
