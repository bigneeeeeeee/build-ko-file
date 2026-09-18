# build-ko-file

GitHub Actions se **hideproc.ko** build karta hai — simple process hider kernel module (LKM).

## Kya hai

`src/hideproc.c` — kretprobe-based process hider:
- `find_vpid` hook → `/proc/<pid>`, `kill`, `pidfd_open`, `waitpid` sab "not found"
- `has_pid_permissions` hook → `/proc` listing se entry skip (`ps`, `ls /proc`)

## Build — abhi: sirf current device (5.10.226)

```
Actions tab → "Build hideproc.ko" → Run workflow
```
Ya bas push kar do (push pe bhi auto-run hota hai).

Output: Artifacts me `hideproc-5.10-a13323220e07-ab12931418.zip` — andar `hideproc.ko`.

## Phone pe load
```
adb push hideproc.ko /data/local/tmp/
adb shell "su -c 'insmod /data/local/tmp/hideproc.ko hide_comm=stranger.sh'"   # comm se hide
adb shell "su -c 'insmod /data/local/tmp/hideproc.ko hide_pid=12345'"          # pid se hide
adb shell "su -c 'rmmod hideproc'"                                              # unhide
```

## Baad me: doosre kernels ke liye
Workflow me matrix add karke doosre GKI kernels (6.1/6.6/6.12) bhi build ho sakte hain — abhi sirf 5.10.226 configured hai.

## Build kya use karta hai
- GKI common kernel: android.googlesource.com/kernel/common
- Branch: android12-5.10, commit a13323220e07 (device ke uname se liya)
- Config: MODULES + KPROBES + KALLSYMS, MODULE_SIG off, LOCALVERSION="-ab12931418" (device release suffix)