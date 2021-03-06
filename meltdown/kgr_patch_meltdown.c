#include <linux/kernel.h>
#include <linux/kallsyms.h>
#include <linux/smp.h>
#include <asm/hypervisor.h>
#include <asm/cpufeature.h>
#include <asm/processor.h>
#include "kgraft_hooks_kallsyms.h"
#include "kgraft_hooks.h"
#include "entry_64_kallsyms.h"
#include "entry_64_compat_kallsyms.h"
#include "patch_entry.h"
#include "patch_entry_kallsyms.h"
#include "schedule_tail_kallsyms.h"
#include "context_switch_mm.h"
#include "context_switch_mm_kallsyms.h"
#include "shared_data.h"
#include "shared_data_kallsyms.h"
#include "kaiser.h"
#include "kaiser_kallsyms.h"
#include "pcid.h"
#include "fork.h"
#include "fork_kallsyms.h"
#include "ldt_kallsyms.h"
#include "perf_event_intel_ds.h"
#include "perf_event_intel_ds_kallsyms.h"
#include "exec_kallsyms.h"
#include "efi_64_kallsyms.h"
#include "memory_kallsyms.h"
#include "pgtable.h"
#include "pgtable_kallsyms.h"
#include "tlb_kallsyms.h"

static struct {
	char *name;
	char **addr;
} kgr_funcs[] = {
	KGRAFT_HOOKS_KALLSYMS
	ENTRY_64_KALLSYMS
	ENTRY_64_COMPAT_KALLSYMS
	PATCH_ENTRY_KALLSYMS
	SCHEDULE_TAIL_KALLSYMS
	CONTEXT_SWITCH_MM_KALLSYMS
	SHARED_DATA_KALLSYMS
	KAISER_KALLSYMS
	FORK_KALLSYMS
	LDT_KALLSYMS
	PERF_EVENT_INTEL_DS_KALLSYMS
	EXEC_KALLSYMS
	EFI_64_KALLSYMS
	MEMORY_KALLSYMS
	PGTABLE_KALLSYMS
	TLB_KALLSYMS
};

static int __init kgr_patch_meltdown_kallsyms(void)
{
	unsigned long addr;
	int i;

	for (i = 0; i < ARRAY_SIZE(kgr_funcs); i++) {
		addr = kallsyms_lookup_name(kgr_funcs[i].name);
		if (!addr) {
			pr_err("kgraft-patch: symbol %s not resolved\n",
				kgr_funcs[i].name);
			return -ENOENT;
		}

		*(kgr_funcs[i].addr) = (void *)addr;
	}

	return 0;
}

static void __install_idt_table_repl(struct work_struct *w)
{
	patch_entry_apply_finish_cpu();
	kgr_pcid_enable_cpu();
}

static void __uninstall_idt_table_repl(struct work_struct *w)
{
	kgr_kaiser_set_kern_cr3(0);
	kgr_kaiser_set_user_cr3(0);
	kgr_pcid_disable_cpu();
	patch_entry_unapply_finish_cpu();
}


void kgr_post_patch_callback(void)
{
	int ret;
	enum patch_state ps = kgr_meltdown_patch_state();

	pr_debug("kgr_post_patch_callback\n");

	if (!ps) {
		return;

	} else if (ps == ps_enabled) {
		if (kgr_meltdown_shared_data->dirty) {
			/*
			 * Unclean handover: there has been a revert inbetween
			 * us and our predecessor.
			 */
			if (kgr_meltdown_shared_data_reset()) {
				/*
				 * In theory, this can't happen,
				 * c.f. kgr_kaiser_reset_shadow_pgd()).
				 */
				pr_err("failed to reset shared data, Meltdown unfixed\n");
				return;
			}
		}

		kgr_meltdown_set_patch_state(ps_activating);
	}

	patch_entry_apply_start(!kgr_meltdown_shared_data->orig_idt.idt ?
				&kgr_meltdown_shared_data->orig_idt : NULL);

	/*
	 * Load the new idt on all cpus. This also makes sure that the
	 * above kgr_meltdown_set_patch_state() is visible everywhere.
	 */
	kgr_schedule_on_each_cpu(__install_idt_table_repl);

	if (ps == ps_active) {
		/* Clean handover */
		kgr_meltdown_shared_data->prev_patch_entry_drain_start();
		kgr_meltdown_shared_data->prev_patch_entry_drain_start = NULL;
	} else {
		ret = kgr_kaiser_map_all_thread_stacks();
		if (ret) {
			pr_err("failed to map thread stacks: %d, Meltdown unfixed\n",
			       ret);
			return;
		}

		ret = kgr_perf_event_intel_map_all_ds_buffers();
		if (ret) {
			pr_err("failed to map Intel DS buffers: %d, Meltdown unfixed\n",
			       ret);
			return;
		}

		kgr_meltdown_set_patch_state(ps_active);
	}
}

void kgr_pre_revert_callback(void)
{
	pr_debug("kgr_pre_revert_callback\n");

	if (!kgr_meltdown_patch_state())
		return;

	kgr_meltdown_set_patch_state(ps_deactivating);
	patch_entry_unapply_start(&kgr_meltdown_shared_data->orig_idt);
	kgr_schedule_on_each_cpu(__uninstall_idt_table_repl);
	kgr_free_all_user_pgds();
	kgr_meltdown_set_patch_state(ps_enabled);
	kgr_meltdown_shared_data_mark_dirty();
	patch_entry_drain_start();
}

void kgr_pre_replace_callback(struct module *new_mod)
{
	pr_debug("kgr_pre_replace_callback\n");

	if (!kgr_meltdown_patch_state())
		return;

	/*
	 * We have to decide whether what follows is a livepatch which
	 * fixes meltdown or not: depending on that, the replacement
	 * has to be treated either as a handover or as a revert.
	 */
	if (kgr_is_meltdown_patcher(new_mod)) {
		/*
		 * The KGraft module stacked on top will install its
		 * own IDT replacement from its
		 * kgr_post_patch_callback(). All we have to do is to
		 * tell the new patch to start draining us when it has
		 * done that.
		 */
		patch_entry_draining = true;
		kgr_meltdown_shared_data->prev_patch_entry_drain_start =
			patch_entry_drain_start;
	} else {
		/*
		 * Ok, we're about to get replaced by a replace_all
		 * livepatch which won't patch meltdown. Treat this
		 * like a revert.
		 */
		kgr_pre_revert_callback();
	}
}


static struct meltdown_patcher this_meltdown_patcher = {
	.mod = THIS_MODULE,
};


int __init kgr_patch_meltdown_init(void)
{
	int ret;

	ret = kgr_patch_meltdown_kallsyms();
	if (ret)
		return ret;

	pr_debug("module core: 0x%016lx, %u\n",
		(unsigned long)THIS_MODULE->module_core,
		THIS_MODULE->core_size);
	if (x86_hyper == &x86_hyper_xen) {
		kgr_meltdown_local_disabled = true;
		pr_info("Disabling Meltdown patch: XEN guest\n");
	} else if (!boot_cpu_has(X86_FEATURE_PCID)) {
		kgr_meltdown_local_disabled = true;
		pr_warn("Disabling Meltdown patch: lack of PCID support\n");
	} else if (boot_cpu_data.x86_vendor == X86_VENDOR_AMD) {
		kgr_meltdown_local_disabled = true;
		pr_info("Disabling Meltdown patch: AMD CPU\n");
	}
	if (kgr_meltdown_local_disabled)
		return 0;

	ret = kgr_meltdown_shared_data_init();
	if (ret)
		return ret;

	ret = context_switch_mm_init();
	if (ret) {
		kgr_meltdown_shared_data_cleanup();
		return ret;
	}

	ret = kgr_kaiser_init();
	if (ret) {
		context_switch_mm_cleanup();
		kgr_meltdown_shared_data_cleanup();
		return ret;
	}

	ret = patch_entry_init();
	if (ret) {
		context_switch_mm_cleanup();
		kgr_meltdown_shared_data_cleanup();
		return ret;
	}

	kgr_meltdown_shared_data_lock();
	if (kgr_meltdown_patch_state() == ps_disabled)
		__kgr_meltdown_set_patch_state(ps_enabled);
	__kgr_meltdown_register_patcher(&this_meltdown_patcher);
	kgr_meltdown_shared_data_unlock();

	return 0;
}

void kgr_patch_meltdown_cleanup(void)
{
	if (kgr_meltdown_local_disabled)
		return;
	kgr_meltdown_unregister_patcher(&this_meltdown_patcher);
	patch_entry_cleanup();
	context_switch_mm_cleanup();
	kgr_meltdown_shared_data_cleanup();
	rcu_barrier();
}
