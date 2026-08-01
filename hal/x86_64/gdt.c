#include <hal/cpu.h>
#include <hal/x86_64/gdt.h>
#include <hal/x86_64/idt.h>

#include <plane/smp.h>

#include <klib/string.h>

/* 1 null descriptor + 4 flat code/data descriptors + 2 TSS descriptors. */
#define GDT_NR_DESCRIPTORS 7
_Static_assert(GDT_NR_DESCRIPTORS <= GDT_MAX_DESCRIPTORS,
	       "gdt descriptors exceed hardware limit!");

struct x86_64_cpu_desc_context {
	struct gdt_descriptor gdt[GDT_NR_DESCRIPTORS];
	struct gdt_ptr gdtr;
	struct tss64 tss;
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

static void set_gdt_descriptor(struct x86_64_cpu_desc_context *ctx,
			       int index,
			       uint32_t base,
			       uint32_t limit,
			       uint8_t access,
			       uint8_t flags)
{
	ctx->gdt[index].base_low = (base & 0xffff);
	ctx->gdt[index].base_middle = (base >> 16) & 0xff;
	ctx->gdt[index].base_high = (base >> 24) & 0xff;

	ctx->gdt[index].limit_low = (limit & 0xffff);
	ctx->gdt[index].flags_limit = (flags & 0xf0) |
				      ((limit >> 16) & 0x0f);

	ctx->gdt[index].access = access;
}

static void set_tss_descriptor(struct x86_64_cpu_desc_context *ctx,
			       int index,
			       uintptr_t base,
			       uint32_t limit)
{
	struct tss_descriptor *tss_desc;

	set_gdt_descriptor(ctx, index, (uint32_t)base, limit,
			   GDT_ACCESS(1, DPL_KERNEL, 0, TYPE_TSS_AVAILABLE),
			   GDT_FLAGS(0, 0, 0, 0));

	tss_desc = (struct tss_descriptor *)&ctx->gdt[index];
	tss_desc->base_upper32 = (uint32_t)(base >> 32);
	tss_desc->reserved = 0;
}

static void x86_64_desc_context_build(struct x86_64_cpu_desc_context *ctx,
				      uintptr_t rsp0)
{
	memset(ctx, 0, sizeof(*ctx));

	ctx->gdtr.limit = sizeof(ctx->gdt) - 1;
	ctx->gdtr.base = (uint64_t)&ctx->gdt;

	set_gdt_descriptor(ctx, 0, 0, 0, 0, 0);
	set_gdt_descriptor(ctx, 1, 0, 0xfffff,
			   GDT_ACCESS(1, DPL_KERNEL, 1, TYPE_CODE_XR),
			   GDT_FLAGS(1, 0, 1, 0));
	set_gdt_descriptor(ctx, 2, 0, 0xfffff,
			   GDT_ACCESS(1, DPL_KERNEL, 1, TYPE_DATA_RW),
			   GDT_FLAGS(1, 1, 0, 0));
	set_gdt_descriptor(ctx, 3, 0, 0xfffff,
			   GDT_ACCESS(1, DPL_USER, 1, TYPE_DATA_RW),
			   GDT_FLAGS(1, 1, 0, 0));
	set_gdt_descriptor(ctx, 4, 0, 0xfffff,
			   GDT_ACCESS(1, DPL_USER, 1, TYPE_CODE_XR),
			   GDT_FLAGS(1, 0, 1, 0));

	ctx->tss.rsp0 = rsp0;
	ctx->tss.iopb_offset = sizeof(ctx->tss);
	set_tss_descriptor(ctx, 5, (uintptr_t)&ctx->tss,
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
	    plane_cpu_boot_state(data->logical_id) !=
		    PLANE_CPU_BOOT_STARTING) {
		return false;
	}

	x86_64_gdt_flush((uint64_t)&ctx->gdtr);
	x86_64_tss_flush();
	x86_64_idt_load_current();
	return true;
}
