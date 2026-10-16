#include <linux/module.h>
#include <linux/mm.h>
extern unsigned long mv;
int h1(struct vm_area_struct *v) { unsigned long s = v->vm_start, e = v->vm_end; unsigned long d = e - s; __builtin_assume(d != 0); return mv >= (int)d; }
int h2(struct vm_area_struct *v) { unsigned long s = v->vm_start, e = v->vm_end; unsigned long d = e - s; if (!d) __builtin_unreachable(); return mv >= (int)d; }
int h3(struct vm_area_struct *v) { unsigned long s = v->vm_start, e = v->vm_end; unsigned long d = e - s; return mv >= (int)d; }
int h4(struct vm_area_struct *v) { unsigned long s = v->vm_start, e = v->vm_end; unsigned long d = e - s; barrier(); return mv >= (int)d; }
int h5(struct vm_area_struct *v) { unsigned long s = v->vm_start, e = v->vm_end; unsigned long d = e - s; asm volatile("" :: "r"(d)); return mv >= (int)d; }
int h6(struct vm_area_struct *v) { unsigned long s = v->vm_start; unsigned long d = READ_ONCE(v->vm_end) - s; return mv >= (int)d; }
int h7(struct vm_area_struct *v) { unsigned long d = READ_ONCE(v->vm_end) - READ_ONCE(v->vm_start); return mv >= (int)d; }
int h8(struct vm_area_struct *v) { unsigned long d = (v->vm_end - v->vm_start) & ~0UL; return mv >= (int)d; }
