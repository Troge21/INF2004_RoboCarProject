"""Actual read-only allocator snapshot function, modelled free-list metadata."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]
text=(root/'kernel/tkernel/memory.c').read_text(encoding='utf-8')
start=text.index('EXPORT ER knl_Imeminfo(')
code=text[start:text.index('/* ------------------------------------------------------------------------ */',start)]
assert 'BEGIN_CRITICAL_SECTION' in code and 'END_CRITICAL_SECTION' in code
assert 'DI(' not in code, 'Local interrupt masking does not exclude the other core'
prefix=r'''
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#define EXPORT
typedef uint32_t UW,UINT;
typedef int ER,W;
typedef struct queue {struct queue *next,*prev;} QUEUE;
typedef struct {W memsz;QUEUE areaque,freeque;} IMACB;
#define E_PAR (-17)
#define E_OBJ (-41)
#define E_OK 0
#define FreeSize(q) ((W)(uintptr_t)((q)+1)->prev)
static IMACB *knl_imacb;
static int held;
#define BEGIN_CRITICAL_SECTION {assert(!held);held=1;
#define END_CRITICAL_SECTION assert(held);held=0;}
'''
main=r'''
int main(void){
    UW free=999,largest=999;IMACB pool={0};QUEUE small[2]={{0}},large[2]={{0}},a={0},b={0},c={0};
    assert(knl_Imeminfo(NULL,&largest)==E_PAR&&!held);
    assert(knl_Imeminfo(&free,&largest)==E_OBJ&&!held&&free==999);
    knl_imacb=&pool;pool.freeque.next=&pool.freeque;
    assert(knl_Imeminfo(&free,&largest)==0&&free==0&&largest==0&&!held);
    pool.freeque.next=small;small[0].next=large;large[0].next=&pool.freeque;
    small[1].prev=(QUEUE *)(uintptr_t)32;large[1].prev=(QUEUE *)(uintptr_t)64;
    small[1].next=&a;a.next=&b;large[1].next=&c;
    {QUEUE before_small[2],before_large[2];memcpy(before_small,small,sizeof(small));memcpy(before_large,large,sizeof(large));
        assert(knl_Imeminfo(&free,&largest)==0&&free==224&&largest==64&&!held);
        assert(!memcmp(before_small,small,sizeof(small))&&!memcmp(before_large,large,sizeof(large)));
    }
    puts("PASS: actual heap snapshot includes equal-size blocks, handles empty/uninitialized pools and changes no allocator metadata.");
}
'''
with tempfile.TemporaryDirectory(prefix='robocar-memory-stats-') as folder:
    p=Path(folder);source=p/'test.c';exe=p/'test.exe';source.write_text(prefix+code+main,encoding='utf-8')
    subprocess.run(['gcc','-std=c11','-Wall','-Wextra','-Werror',str(source),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
