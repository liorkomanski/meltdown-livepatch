#ifndef _KGR_PATCH_MELTDOWN_H
#define _KGR_PATCH_MELTDOWN_H

#include "tlb.h"
#include "kaiser.h"
#include "fork.h"
#include "ldt.h"
#include "perf_event_intel_ds.h"
#include "exec.h"
#include "efi_64.h"

struct work_struct;

int kgr_patch_meltdown_init(void);
void kgr_patch_meltdown_cleanup(void);

void kgr_kgr_work_fn(struct work_struct *work);
int kgr_kgr_modify_kernel(struct kgr_patch *patch, bool revert);

void kgr_schedule_tail(struct task_struct *prev);

#define KGR_PATCH_MELTDOWN_FUNCS				\
	KGR_PATCH(kgr_work_fn, kgr_kgr_work_fn),		\
	KGR_PATCH(kgr_modify_kernel, kgr_kgr_modify_kernel),	\
	KGR_PATCH(schedule_tail, kgr_schedule_tail),		\
	KGR_PATCH(native_flush_tlb, kgr_native_flush_tlb),	\
	KGR_PATCH(native_flush_tlb_global,			\
		  kgr_native_flush_tlb_global),		\
	KGR_PATCH(native_flush_tlb_single,			\
		  kgr_native_flush_tlb_single),		\
	KGR_PATCH(native_set_pgd, kgr_native_set_pgd),		\
	KGR_PATCH(free_task, kgr_free_task),			\
	KGR_PATCH(copy_process, kgr_copy_process),		\
	KGR_PATCH(alloc_ldt_struct, kgr_alloc_ldt_struct),	\
	KGR_PATCH(destroy_context_ldt,				\
			kgr_destroy_context_ldt),		\
	KGR_PATCH(write_ldt, kgr_write_ldt),			\
	KGR_PATCH(release_pebs_buffer,				\
			kgr_release_pebs_buffer),		\
	KGR_PATCH(release_bts_buffer, kgr_release_bts_buffer),	\
	KGR_PATCH(reserve_ds_buffers, kgr_reserve_ds_buffers),	\
	KGR_PATCH(flush_old_exec, kgr_flush_old_exec),		\
	KGR_PATCH(efi_thunk_set_virtual_address_map,		\
		kgr_efi_thunk_set_virtual_address_map),	\
	KGR_PATCH(efi_thunk_get_time, kgr_efi_thunk_get_time),	\
	KGR_PATCH(efi_thunk_set_time, kgr_efi_thunk_set_time),	\
	KGR_PATCH(efi_thunk_get_wakeup_time,			\
			kgr_efi_thunk_get_wakeup_time),	\
	KGR_PATCH(efi_thunk_set_wakeup_time,			\
			kgr_efi_thunk_set_wakeup_time),	\
	KGR_PATCH(efi_thunk_get_variable,			\
			kgr_efi_thunk_get_variable),		\
	KGR_PATCH(efi_thunk_set_variable,			\
			kgr_efi_thunk_set_variable),		\
	KGR_PATCH(efi_thunk_get_next_variable,			\
			kgr_efi_thunk_get_next_variable),	\
	KGR_PATCH(efi_thunk_get_next_high_mono_count,		\
		kgr_efi_thunk_get_next_high_mono_count),	\
	KGR_PATCH(efi_thunk_reset_system,			\
			kgr_efi_thunk_reset_system),		\
	KGR_PATCH(efi_thunk_query_variable_info,		\
			kgr_efi_thunk_query_variable_info),	\

#endif
