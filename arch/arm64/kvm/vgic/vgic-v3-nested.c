#include <linux/cpu.h>
#include <linux/kvm.h>
#include <linux/kvm_host.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/uaccess.h>

#include <linux/irqchip/arm-gic-v3.h>

#include <asm/kvm_emulate.h>
#include <asm/kvm_arm.h>
#include <kvm/arm_vgic.h>

#include "vgic.h"

static inline struct vgic_v3_cpu_if *vcpu_nested_if(struct kvm_vcpu *vcpu)
{
	return &vcpu->arch.vgic_cpu.nested_vgic_v3;
}

static inline struct vgic_v3_cpu_if *vcpu_shadow_if(struct kvm_vcpu *vcpu)
{
	return &vcpu->arch.vgic_cpu.shadow_vgic_v3;
}

static inline bool lr_triggers_eoi(u64 lr)
{
	return !(lr & (ICH_LR_STATE | ICH_LR_HW)) && (lr & ICH_LR_EOI);
}

u16 vgic_v3_get_eisr(struct kvm_vcpu *vcpu)
{
	struct vgic_v3_cpu_if *cpu_if = vcpu_nested_if(vcpu);
	u16 reg = 0;
	int i;

	for (i = 0; i < kvm_vgic_global_state.nr_lr; i++) {
		if (lr_triggers_eoi(cpu_if->vgic_lr[i]))
			reg |= BIT(i);
	}

	return reg;
}

u16 vgic_v3_get_elrsr(struct kvm_vcpu *vcpu)
{
	struct vgic_v3_cpu_if *cpu_if = vcpu_nested_if(vcpu);
	u16 reg = 0;
	int i;

	for (i = 0; i < kvm_vgic_global_state.nr_lr; i++) {
		if (!(cpu_if->vgic_lr[i] & ICH_LR_STATE))
			reg |= BIT(i);
	}

	return reg;
}

u64 vgic_v3_get_misr(struct kvm_vcpu *vcpu)
{
	struct vgic_v3_cpu_if *cpu_if = vcpu_nested_if(vcpu);
	int nr_lr = kvm_vgic_global_state.nr_lr;
	u64 reg = 0;

	if (vgic_v3_get_eisr(vcpu))
		reg |= ICH_MISR_EOI;

	if (cpu_if->vgic_hcr & ICH_HCR_UIE) {
		int used_lrs;

		used_lrs = nr_lr - hweight16(vgic_v3_get_elrsr(vcpu));
		if (used_lrs <= 1)
			reg |= ICH_MISR_U;
	}

	/* TODO: Support remaining bits in this register */
	return reg;
}

/*
 * For LRs which have HW bit set such as timer interrupts, we modify them to
 * have the host hardware interrupt number instead of the virtual one programmed
 * by the guest hypervisor.
 */
static void vgic_v3_create_shadow_lr(struct kvm_vcpu *vcpu)
{
	struct vgic_v3_cpu_if *cpu_if = vcpu_nested_if(vcpu);
	struct vgic_v3_cpu_if *s_cpu_if = vcpu_shadow_if(vcpu);
	struct vgic_irq *irq;
	int i, used_lrs = 0;

	for (i = 0; i < kvm_vgic_global_state.nr_lr; i++) {
		u64 lr = cpu_if->vgic_lr[i];
		int l1_irq;

		if (!(lr & ICH_LR_HW))
			goto next;

		/* We have the HW bit set */
		l1_irq = (lr & ICH_LR_PHYS_ID_MASK) >> ICH_LR_PHYS_ID_SHIFT;
		irq = vgic_get_irq(vcpu->kvm, vcpu, l1_irq);

		if (!irq || !irq->hw) {
			/* There was no real mapping, so nuke the HW bit */
			lr &= ~ICH_LR_HW;
			if (irq)
				vgic_put_irq(vcpu->kvm, irq);
			goto next;
		}

		/* Translate the virtual mapping to the real one */
		lr &= ~ICH_LR_EOI; /* Why? */
		lr &= ~ICH_LR_PHYS_ID_MASK;
		lr |= (u64)irq->hwintid << ICH_LR_PHYS_ID_SHIFT;
		vgic_put_irq(vcpu->kvm, irq);

next:
		s_cpu_if->vgic_lr[i] = lr;
		used_lrs = i + 1;
	}

	s_cpu_if->used_lrs = used_lrs;
}

/*
 * Change the shadow HWIRQ field back to the virtual value before copying over
 * the entire shadow struct to the nested state.
 */
static void vgic_v3_fixup_shadow_lr_state(struct kvm_vcpu *vcpu)
{
	struct vgic_v3_cpu_if *cpu_if = vcpu_nested_if(vcpu);
	struct vgic_v3_cpu_if *s_cpu_if = vcpu_shadow_if(vcpu);
	int lr;

	for (lr = 0; lr < kvm_vgic_global_state.nr_lr; lr++) {
		s_cpu_if->vgic_lr[lr] &= ~ICH_LR_PHYS_ID_MASK;
		s_cpu_if->vgic_lr[lr] |= cpu_if->vgic_lr[lr] & ICH_LR_PHYS_ID_MASK;
	}
}

void vgic_v3_sync_nested(struct kvm_vcpu *vcpu)
{
	struct vgic_v3_cpu_if *cpu_if = vcpu_nested_if(vcpu);
	struct vgic_v3_cpu_if *s_cpu_if = vcpu_shadow_if(vcpu);
	struct vgic_irq *irq;
	int i;

	for (i = 0; i < s_cpu_if->used_lrs; i++) {
		u64 lr = cpu_if->vgic_lr[i];
		int l1_irq;

		if (!(lr & ICH_LR_HW) || !(lr & ICH_LR_STATE))
			continue;

		/*
		 * If we had a HW lr programmed by the guest hypervisor, we
		 * need to emulate the HW effect between the guest hypervisor
		 * and the nested guest.
		 */
		l1_irq = (lr & ICH_LR_PHYS_ID_MASK) >> ICH_LR_PHYS_ID_SHIFT;
		irq = vgic_get_irq(vcpu->kvm, vcpu, l1_irq);
		if (!irq)
			continue; /* oh well, the guest hyp is broken */

		lr = __gic_v3_get_lr(i);
		if (!(lr & ICH_LR_STATE))
			irq->active = false;

		vgic_put_irq(vcpu->kvm, irq);
	}
}

void vgic_v3_load_nested(struct kvm_vcpu *vcpu)
{
	struct vgic_cpu *vgic_cpu = &vcpu->arch.vgic_cpu;

	vgic_cpu->shadow_vgic_v3 = vgic_cpu->nested_vgic_v3;
	vgic_v3_create_shadow_lr(vcpu);
	__vgic_v3_restore_state(vcpu_shadow_if(vcpu));
}

void vgic_v3_put_nested(struct kvm_vcpu *vcpu)
{
	struct vgic_cpu *vgic_cpu = &vcpu->arch.vgic_cpu;

	__vgic_v3_save_state(vcpu_shadow_if(vcpu));

	/*
	 * Translate the shadow state HW fields back to the virtual ones
	 * before copying the shadow struct back to the nested one.
	 */
	vgic_v3_fixup_shadow_lr_state(vcpu);
	vgic_cpu->nested_vgic_v3 = vgic_cpu->shadow_vgic_v3;
}

void vgic_v3_handle_nested_maint_irq(struct kvm_vcpu *vcpu)
{
	struct vgic_v3_cpu_if *cpu_if = vcpu_nested_if(vcpu);

	/*
	 * If we exit a nested VM with a pending maintenance interrupt from the
	 * GIC, then we need to forward this to the guest hypervisor so that it
	 * can re-sync the appropriate LRs and sample level triggered interrupts
	 * again.
	 */
	if (vgic_state_is_nested(vcpu) &&
	    (cpu_if->vgic_hcr & ICH_HCR_EN) &&
	    vgic_v3_get_misr(vcpu))
		kvm_inject_nested_irq(vcpu);
}
