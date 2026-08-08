#include <hal/cpu.h>
#include <hal/x86_64/descriptor_defs.h>
#include <hal/x86_64/gdt.h>
#include <hal/x86_64/idt.h>

#include <plane/smp.h>

#include <klib/string.h>

_Static_assert(X86_64_DESC_GDT_NR <= X86_64_DESC_GDT_MAX_ENTRIES,
	       "gdt descriptors exceed hardware limit!");

struct x86_64_cpu_desc_context {
	struct x86_64_desc_gdt_entry gdt[X86_64_DESC_GDT_NR];
	struct x86_64_desc_ptr gdtr;
	struct x86_64_desc_tss64 tss;
	bool prepared;
};

static struct x86_64_cpu_desc_context cpu_desc_contexts[PLANE_MAX_CPUS];

/* in gdt_flush.S */
extern void x86_64_gdt_flush(uint64_t gdtr_addr);
extern void x86_64_tss_flush(void);

static bool cpu_data_valid_index(const struct plane_cpu_data *data)
{
	return data != NULL && data->self == data &&
	       data->logical_id < PLANE_MAX_CPUS;
}

static struct x86_64_cpu_desc_context *
desc_context_for_data(const struct plane_cpu_data *data)
{
	if (!cpu_data_valid_index(data)) {
		return NULL;
	}

	return &cpu_desc_contexts[data->logical_id];
}

static void set_flat_descriptor(struct x86_64_cpu_desc_context *ctx,
				uint8_t slot,
				uint8_t dpl,
				uint8_t type,
				bool long_mode,
				bool default_big)
{
	x86_64_desc_set_gdt_entry(
		&ctx->gdt[slot], 0, 0xfffff,
		x86_64_desc_access(true, dpl, true, type),
		x86_64_desc_flags(true, default_big, long_mode, false));
}

static void x86_64_desc_context_build(struct x86_64_cpu_desc_context *ctx,
				      uintptr_t rsp0)
{
	memset(ctx, 0, sizeof(*ctx));

	ctx->gdtr.limit = sizeof(ctx->gdt) - 1;
	ctx->gdtr.base = (uint64_t)&ctx->gdt;

	set_flat_descriptor(ctx, X86_64_DESC_GDT_KERNEL_CODE,
			    X86_64_DESC_DPL_KERNEL,
			    X86_64_DESC_TYPE_CODE_XR, true, false);
	set_flat_descriptor(ctx, X86_64_DESC_GDT_KERNEL_DATA,
			    X86_64_DESC_DPL_KERNEL,
			    X86_64_DESC_TYPE_DATA_RW, false, true);
	set_flat_descriptor(ctx, X86_64_DESC_GDT_USER_DATA,
			    X86_64_DESC_DPL_USER,
			    X86_64_DESC_TYPE_DATA_RW, false, true);
	set_flat_descriptor(ctx, X86_64_DESC_GDT_USER_CODE,
			    X86_64_DESC_DPL_USER,
			    X86_64_DESC_TYPE_CODE_XR, true, false);

	/*
	 * Plane currently uses the 64-bit TSS only for rsp0 and the I/O map
	 * boundary. IST, syscall/sysenter stacks, and user transitions are
	 * future descriptor milestones.
	 */
	ctx->tss.rsp0 = rsp0;
	ctx->tss.iopb_offset = sizeof(ctx->tss);
	x86_64_desc_set_tss_entry(
		(struct x86_64_desc_tss_entry *)
			&ctx->gdt[X86_64_DESC_GDT_TSS],
		(uintptr_t)&ctx->tss,
		sizeof(ctx->tss) - 1);
	ctx->prepared = true;
}

void x86_64_gdt_init(void)
{
	struct x86_64_cpu_desc_context *ctx = &cpu_desc_contexts[0];

	x86_64_desc_context_build(ctx, 0);
	x86_64_gdt_flush((uint64_t)&ctx->gdtr);
	x86_64_tss_flush();
}

void x86_64_tss_set_kernel_stack(uintptr_t stack)
{
	cpu_desc_contexts[0].tss.rsp0 = stack;
}

bool hal_cpu_prepare_ap_startup_context(struct plane_cpu_data *data)
{
	struct x86_64_cpu_desc_context *ctx = desc_context_for_data(data);

	if (ctx == NULL || data->is_bsp || !data->present ||
	    plane_vaddr_is_null(data->ap_stack_top)) {
		return false;
	}

	x86_64_desc_context_build(ctx,
				  (uintptr_t)plane_vaddr_raw(data->ap_stack_top));
	return true;
}

bool hal_cpu_install_ap_startup_context(struct plane_cpu_data *data)
{
	struct x86_64_cpu_desc_context *ctx = desc_context_for_data(data);

	if (ctx == NULL || data->is_bsp || !ctx->prepared ||
	    plane_cpu_startup_state(data->logical_id) !=
		    PLANE_CPU_STARTUP_STARTING) {
		return false;
	}

	x86_64_gdt_flush((uint64_t)&ctx->gdtr);
	x86_64_tss_flush();
	x86_64_idt_load_current();
	return true;
}
