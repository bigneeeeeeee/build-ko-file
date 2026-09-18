# build-ko-file

GitHub Actions se **hideproc.ko** build karta hai — ek simple process hider kernel module (LKM).

## Kya hai

`src/hideproc.c` — kretprobe-based process hider:
- `find_vpid` hook → `/proc/<pid>`, `kill`, `pidfd_open`, `waitpid` sab "not found"
- `has_pid_permissions` hook → `/proc` listing se entry skip (`ps`, `ls /proc`)
- Kernel 5.10 / 6.1 / 6.6 / 6.12 (Android GKI) sab pe supported

## Build kaise chale

### Abhi — sirf current device
```
Actions tab → "Build hideproc.ko" → Run workflow → scope: current
```
Sirf 5.10.226 (tumhara device) build hoga.

### Baad me — saare 48 kernels
```
Actions tab → Run workflow → scope: all
```
Sab 6.1/6.6/6.12 + 5.10 kernels ke liye alag `.ko` bane ga.

## Output
Har job ke Artifacts me zip milega: `hideproc-<kmi>-<commit><suffix>.zip` andar `hideproc.ko`.

## Phone pe load
```
insmod hideproc.ko hide_comm=stranger.sh   # comm se hide
insmod hideproc.ko hide_pid=12345          # pid se hide
rmmod hideproc                              # unhide
```

## Build kya use karta hai
- GKI common kernel: android.googlesource.com/kernel/common
- Branch map: 5.10→android12-5.10 | 6.1→android14-6.1 | 6.6→android15-6.6 | 6.12→android16-6.12
- Exact commit per kernel (matrix me)
- config: MODULES + KPROBES + KALLSYMS, MODULE_SIG off, LOCALVERSION = device suffix

## Note
Vendor-heavy kernels (`-abogki`/`-4k` suffix) me pehli baar build fail ho sakta hai agar commit GKI common repo me nahi milta — log se dekho, taab vendor source URL add hoga.