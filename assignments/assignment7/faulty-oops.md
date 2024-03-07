# Analysis of Kernel Oops

The kernel oops was caused due to writing to "faulty" device:
```bash
# echo “hello_world” > /dev/faulty
```

The output from the kernel is as follows:

```=
Unable to handle kernel NULL pointer dereference at virtual address 0000000000000000
Mem abort info:
  ESR = 0x96000045
  EC = 0x25: DABT (current EL), IL = 32 bits
  SET = 0, FnV = 0
  EA = 0, S1PTW = 0
  FSC = 0x05: level 1 translation fault
Data abort info:
  ISV = 0, ISS = 0x00000045
  CM = 0, WnR = 1
user pgtable: 4k pages, 39-bit VAs, pgdp=0000000042611000
[0000000000000000] pgd=0000000000000000, p4d=0000000000000000, pud=0000000000000000
Internal error: Oops: 96000045 [#1] SMP
Modules linked in: hello(O) faulty(O) scull(O)
CPU: 0 PID: 148 Comm: sh Tainted: G           O      5.15.18 #2
Hardware name: linux,dummy-virt (DT)
pstate: 80000005 (Nzcv daif -PAN -UAO -TCO -DIT -SSBS BTYPE=--)
pc : faulty_write+0x14/0x20 [faulty]
lr : vfs_write+0xa8/0x2b0
sp : ffffffc008d23d80
x29: ffffffc008d23d80 x28: ffffff800269cc80 x27: 0000000000000000
x26: 0000000000000000 x25: 0000000000000000 x24: 0000000000000000
x23: 0000000040000000 x22: 0000000000000012 x21: 000000556e682a80
x20: 000000556e682a80 x19: ffffff8002626700 x18: 0000000000000000
x17: 0000000000000000 x16: 0000000000000000 x15: 0000000000000000
x14: 0000000000000000 x13: 0000000000000000 x12: 0000000000000000
x11: 0000000000000000 x10: 0000000000000000 x9 : 0000000000000000
x8 : 0000000000000000 x7 : 0000000000000000 x6 : 0000000000000000
x5 : 0000000000000001 x4 : ffffffc0006f7000 x3 : ffffffc008d23df0
x2 : 0000000000000012 x1 : 0000000000000000 x0 : 0000000000000000
Call trace:
 faulty_write+0x14/0x20 [faulty]
 ksys_write+0x68/0x100
 __arm64_sys_write+0x20/0x30
 invoke_syscall+0x54/0x130
 el0_svc_common.constprop.0+0x44/0xf0
 do_el0_svc+0x40/0xa0
 el0_svc+0x20/0x60
 el0t_64_sync_handler+0xe8/0xf0
 el0t_64_sync+0x1a0/0x1a4
Code: d2800001 d2800000 d503233f d50323bf (b900003f) 
---[ end trace 6ea39b3c430097f9 ]---
```

Line #1 says that the cause to this oops is NULL pointer dereference at virtual address 0000000000000000.
Address 0000000000000000 is an invalid address used to initialize pointer as NULL.
Line #18 (pc - program counter register) as well as line #32 in the call trace, 
tell us that the problem occured in function *faulty_write* which belongs to module *faulty*, 
at offset 0x14 (20) bytes from function start.
The total size of the function is 0x20 (32) bytes.

Using objdump for the target objects, we disassemble *faulty* module - faulty.ko:
```bash
./output/host/bin/aarch64-linux-objdump -S ./output/target/lib/modules/5.15.18/extra/faulty.ko
```

(partial output, only the part relevant to function *faulty_write*)
```=
./output/target/lib/modules/5.15.18/extra/faulty.ko:     file format elf64-littleaarch64


Disassembly of section .text:

0000000000000000 <faulty_write>:
   0:   d503245f        bti     c
   4:   d2800001        mov     x1, #0x0                        // #0
   8:   d2800000        mov     x0, #0x0                        // #0
   c:   d503233f        paciasp
  10:   d50323bf        autiasp
  14:   b900003f        str     wzr, [x1]
  18:   d65f03c0        ret
  1c:   d503201f        nop
```
Line #8, offset 0xa4, sets register x1 to 0.
Line #12, offset 0x14, the offset mentioned in the oops` output,
tries to store into the address specified in register x1, which is 0.
Since address 0 is invalid and cannot be used other than initializing variables to NULL,
this causes the oops.

Now we look at source code of *faulty_write* from *faulty* module, in file faulty.c:
```c=
ssize_t faulty_write (struct file *filp, const char __user *buf, size_t count,
		loff_t *pos)
{
	/* make a simple fault by dereferencing a NULL pointer */
	*(int *)0 = 0;
	return 0;
}
```
 
Line #5 tries to assign 0 to address 0 (NULL), by casting 0 to int pointer and dereferencing it.
That is the NULL pointer derefernce that caused the kernal oops.
