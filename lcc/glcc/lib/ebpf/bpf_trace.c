/* Copyright (c) 2011-2015 PLUMgrid, http://plumgrid.com
 * Copyright (c) 2016 Facebook
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 */
#include "linux/config.h"
#include "linux/bpf.h"

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/slab.h>
#include "linux/bpf_perf_event.h"
#include "linux/filter.h"
#include <linux/uaccess.h>
#include <linux/ctype.h>
#include <linux/kprobes.h>
#include <asm/kprobes.h>

#include "allsyms.h"

// u64 bpf_get_stackid(u64 r1, u64 r2, u64 r3, u64 r4, u64 r5);
// u64 bpf_get_stack(u64 r1, u64 r2, u64 r3, u64 r4, u64 r5);

/**
 * trace_call_bpf - invoke BPF program
 * @call: tracepoint event
 * @ctx: opaque context pointer
 *
 * kprobe handlers execute BPF programs via this helper.
 * Can be used from static tracepoints in the future.
 *
 * Return: BPF programs always return an integer which is interpreted by
 * kprobe handler as:
 * 0 - return from kprobe (event is filtered out)
 * 1 - store kprobe event into ring buffer
 * Other values are reserved and currently alias to 1
 */
// unsigned int trace_call_bpf(struct ftrace_event_call *call, void *ctx)
// {
// 	unsigned int ret;

// 	if (in_nmi()) /* not supported yet */
// 		return 1;

// 	preempt_disable();

// 	if (unlikely(__this_cpu_inc_return(bpf_prog_active) != 1)) {
// 		/*
// 		 * since some bpf program is already running on this cpu,
// 		 * don't call into another bpf program (same or different)
// 		 * and don't send kprobe event into ring-buffer,
// 		 * so return zero here
// 		 */
// 		ret = 0;
// 		goto out;
// 	}

// 	/*
// 	 * Instead of moving rcu_read_lock/rcu_dereference/rcu_read_unlock
// 	 * to all call sites, we did a bpf_prog_array_valid() there to check
// 	 * whether call->prog_array is empty or not, which is
// 	 * a heurisitc to speed up execution.
// 	 *
// 	 * If bpf_prog_array_valid() fetched prog_array was
// 	 * non-NULL, we go into trace_call_bpf() and do the actual
// 	 * proper rcu_dereference() under RCU lock.
// 	 * If it turns out that prog_array is NULL then, we bail out.
// 	 * For the opposite, if the bpf_prog_array_valid() fetched pointer
// 	 * was NULL, you'll skip the prog_array with the risk of missing
// 	 * out of events when it was updated in between this and the
// 	 * rcu_dereference() which is accepted risk.
// 	 */
// 	ret = BPF_PROG_RUN_ARRAY_CHECK(call->rh_data->prog_array, ctx, BPF_PROG_RUN);

//  out:
// 	__this_cpu_dec(bpf_prog_active);
// 	preempt_enable();

// 	return ret;
// }

/**
 * strncpy_from_unsafe: - Copy a NUL terminated string from unsafe address.
 * @dst:   Destination address, in kernel space.  This buffer must be at
 *         least @count bytes long.
 * @src:   Unsafe address.
 * @count: Maximum number of bytes to copy, including the trailing NUL.
 *
 * Copies a NUL-terminated string from unsafe address to kernel buffer.
 *
 * On success, returns the length of the string INCLUDING the trailing NUL.
 *
 * If access fails, returns -EFAULT (some data may have been copied
 * and the trailing NUL added).
 *
 * If @count is smaller than the length of the string, copies @count-1 bytes,
 * sets the last byte of @dst buffer to NUL and returns @count.
 */
long strncpy_from_unsafe(char *dst, const void *unsafe_addr, long count)
{
	mm_segment_t old_fs = get_fs();
	const void *src = unsafe_addr;
	long ret;

	if (unlikely(count <= 0))
		return 0;

	set_fs(KERNEL_DS);
	pagefault_disable();

	do {
		ret = __copy_from_user_inatomic(dst++,
						(const void __user __force *)src++, 1);
	} while (dst[-1] && ret == 0 && src - unsafe_addr < count);

	dst[-1] = '\0';
	pagefault_enable();
	set_fs(old_fs);

	return ret ? -EFAULT : src - unsafe_addr;
}

// BPF_CALL_2(bpf_override_return, struct pt_regs *, regs, unsigned long, rc)
// {
// 	return -EINVAL;
// }

// static const struct bpf_func_proto bpf_override_return_proto = {
// 	.func		= bpf_override_return,
// 	.gpl_only	= true,
// 	.ret_type	= RET_INTEGER,
// 	.arg1_type	= ARG_PTR_TO_CTX,
// 	.arg2_type	= ARG_ANYTHING,
// };

BPF_CALL_3(bpf_probe_read, void *, dst, u32, size, const void *, unsafe_ptr)
{
	int ret;

	ret = probe_kernel_read(dst, unsafe_ptr, size);
	if (unlikely(ret < 0))
		memset(dst, 0, size);

	return ret;
}

static const struct bpf_func_proto bpf_probe_read_proto = {
	.func		= bpf_probe_read,
	.gpl_only	= true,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_UNINIT_MEM,
	.arg2_type	= ARG_CONST_SIZE_OR_ZERO,
	.arg3_type	= ARG_ANYTHING,
};

// BPF_CALL_3(bpf_probe_write_user, void *, unsafe_ptr, const void *, src,
// 	   u32, size)
// {
// 	/*
// 	 * Ensure we're in user context which is safe for the helper to
// 	 * run. This helper has no business in a kthread.
// 	 *
// 	 * access_ok() should prevent writing to non-user memory, but in
// 	 * some situations (nommu, temporary switch, etc) access_ok() does
// 	 * not provide enough validation, hence the check on KERNEL_DS.
// 	 */

// 	if (unlikely(in_interrupt() ||
// 		     current->flags & (PF_KTHREAD | PF_EXITING)))
// 		return -EPERM;
// 	if (unlikely(segment_eq(get_fs(), KERNEL_DS)))
// 		return -EPERM;
// 	if (!access_ok(VERIFY_WRITE, unsafe_ptr, size))
// 		return -EPERM;

// 	return probe_kernel_write(unsafe_ptr, src, size);
// }

// static const struct bpf_func_proto bpf_probe_write_user_proto = {
// 	.func		= bpf_probe_write_user,
// 	.gpl_only	= true,
// 	.ret_type	= RET_INTEGER,
// 	.arg1_type	= ARG_ANYTHING,
// 	.arg2_type	= ARG_PTR_TO_MEM,
// 	.arg3_type	= ARG_CONST_SIZE,
// };

// static const struct bpf_func_proto *bpf_get_probe_write_proto(void)
// {
// 	pr_warn_ratelimited("%s[%d] is installing a program with bpf_probe_write_user helper that may corrupt user memory!",
// 			    current->comm, task_pid_nr(current));

// 	return &bpf_probe_write_user_proto;
// }

/*
 * limited trace_printk()
 * only %d %u %x %ld %lu %lx %lld %llu %llx %p %s conversion specifiers allowed
 */
BPF_CALL_5(bpf_trace_printk, char *, fmt, u32, fmt_size, u64, arg1,
	   u64, arg2, u64, arg3)
{
	bool str_seen = false;
	int mod[3] = {};
	int fmt_cnt = 0;
	u64 unsafe_addr;
	char buf[64];
	int i;

	/*
	 * bpf_check()->check_func_arg()->check_stack_boundary()
	 * guarantees that fmt points to bpf program stack,
	 * fmt_size bytes of it were initialized and fmt_size > 0
	 */
	if (fmt[--fmt_size] != 0)
		return -EINVAL;

	/* check format string for allowed specifiers */
	for (i = 0; i < fmt_size; i++) {
		if ((!isprint(fmt[i]) && !isspace(fmt[i])) || !isascii(fmt[i]))
			return -EINVAL;

		if (fmt[i] != '%')
			continue;

		if (fmt_cnt >= 3)
			return -EINVAL;

		/* fmt[i] != 0 && fmt[last] == 0, so we can access fmt[i + 1] */
		i++;
		if (fmt[i] == 'l') {
			mod[fmt_cnt]++;
			i++;
		} else if (fmt[i] == 'p' || fmt[i] == 's') {
			mod[fmt_cnt]++;
			i++;
			if (!isspace(fmt[i]) && !ispunct(fmt[i]) && fmt[i] != 0)
				return -EINVAL;
			fmt_cnt++;
			if (fmt[i - 1] == 's') {
				if (str_seen)
					/* allow only one '%s' per fmt string */
					return -EINVAL;
				str_seen = true;

				switch (fmt_cnt) {
				case 1:
					unsafe_addr = arg1;
					arg1 = (long) buf;
					break;
				case 2:
					unsafe_addr = arg2;
					arg2 = (long) buf;
					break;
				case 3:
					unsafe_addr = arg3;
					arg3 = (long) buf;
					break;
				}
				buf[0] = 0;
				strncpy_from_unsafe(buf,
						    (void *) (long) unsafe_addr,
						    sizeof(buf));
			}
			continue;
		}

		if (fmt[i] == 'l') {
			mod[fmt_cnt]++;
			i++;
		}

		if (fmt[i] != 'd' && fmt[i] != 'u' && fmt[i] != 'x')
			return -EINVAL;
		fmt_cnt++;
	}

	return __trace_printk(1/* fake ip will not be printed */, fmt,
			      mod[0] == 2 ? arg1 : mod[0] == 1 ? (long) arg1 : (u32) arg1,
			      mod[1] == 2 ? arg2 : mod[1] == 1 ? (long) arg2 : (u32) arg2,
			      mod[2] == 2 ? arg3 : mod[2] == 1 ? (long) arg3 : (u32) arg3);
}

static const struct bpf_func_proto bpf_trace_printk_proto = {
	.func		= bpf_trace_printk,
	.gpl_only	= true,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_MEM,
	.arg2_type	= ARG_CONST_SIZE,
};

const struct bpf_func_proto *bpf_get_trace_printk_proto(void)
{
	/*
	 * this program might be calling bpf_trace_printk,
	 * so allocate per-cpu printk buffers
	 */
	trace_printk_init_buffers_p();

	return &bpf_trace_printk_proto;
}

// static __always_inline int
// get_map_perf_counter(struct bpf_map *map, u64 flags,
// 		     u64 *value, u64 *enabled, u64 *running)
// {
// 	struct bpf_array *array = container_of(map, struct bpf_array, map);
// 	unsigned int cpu = smp_processor_id();
// 	u64 index = flags & BPF_F_INDEX_MASK;
// 	struct bpf_event_entry *ee;

// 	if (unlikely(flags & ~(BPF_F_INDEX_MASK)))
// 		return -EINVAL;
// 	if (index == BPF_F_CURRENT_CPU)
// 		index = cpu;
// 	if (unlikely(index >= array->map.max_entries))
// 		return -E2BIG;

// 	ee = READ_ONCE(array->ptrs[index]);
// 	if (!ee)
// 		return -ENOENT;

//     return 0;
// 	// return perf_event_read_local_p(ee->event, value, enabled, running);
// }

// BPF_CALL_2(bpf_perf_event_read, struct bpf_map *, map, u64, flags)
// {
// 	u64 value = 0;
// 	int err;

// 	err = get_map_perf_counter(map, flags, &value, NULL, NULL);
// 	/*
// 	 * this api is ugly since we miss [-22..-2] range of valid
// 	 * counter values, but that's uapi
// 	 */
// 	if (err)
// 		return err;
// 	return value;
// }

// static const struct bpf_func_proto bpf_perf_event_read_proto = {
// 	.func		= bpf_perf_event_read,
// 	.gpl_only	= true,
// 	.ret_type	= RET_INTEGER,
// 	.arg1_type	= ARG_CONST_MAP_PTR,
// 	.arg2_type	= ARG_ANYTHING,
// };

// BPF_CALL_4(bpf_perf_event_read_value, struct bpf_map *, map, u64, flags,
// 	   struct bpf_perf_event_value *, buf, u32, size)
// {
// 	int err = -EINVAL;

// 	if (unlikely(size != sizeof(struct bpf_perf_event_value)))
// 		goto clear;
// 	err = get_map_perf_counter(map, flags, &buf->counter, &buf->enabled,
// 				   &buf->running);
// 	if (unlikely(err))
// 		goto clear;
// 	return 0;
// clear:
// 	memset(buf, 0, size);
// 	return err;
// }

// static const struct bpf_func_proto bpf_perf_event_read_value_proto = {
// 	.func		= bpf_perf_event_read_value,
// 	.gpl_only	= true,
// 	.ret_type	= RET_INTEGER,
// 	.arg1_type	= ARG_CONST_MAP_PTR,
// 	.arg2_type	= ARG_ANYTHING,
// 	.arg3_type	= ARG_PTR_TO_UNINIT_MEM,
// 	.arg4_type	= ARG_CONST_SIZE,
// };

static __always_inline u64
__bpf_perf_event_output(struct pt_regs *regs, struct bpf_map *map,
			u64 flags, struct perf_raw_record *raw)
{
	struct bpf_array *array = container_of(map, struct bpf_array, map);
	u64 index = flags & BPF_F_INDEX_MASK;
	struct perf_sample_data sample_data;
	struct bpf_event_entry *ee;
	struct perf_event *event;

	if (index == BPF_F_CURRENT_CPU)
		index = raw_smp_processor_id();
	if (unlikely(index >= array->map.max_entries))
		return -E2BIG;

	ee = READ_ONCE(array->ptrs[index]);
	if (!ee)
		return -ENOENT;

#define PERF_COUNT_SW_BPF_OUTPUT	PERF_COUNT_SW_DUMMY

	event = ee->event;
	if (unlikely(event->attr.type != PERF_TYPE_SOFTWARE ||
		     event->attr.config != PERF_COUNT_SW_BPF_OUTPUT))
		return -EINVAL;

	if (unlikely(event->oncpu != smp_processor_id()))
		return -EOPNOTSUPP;

	perf_sample_data_init(&sample_data, 0, 0);
	sample_data.raw = raw;
	perf_event_output_p(event, &sample_data, regs);
	return 0;
}

BPF_CALL_5(bpf_perf_event_output, struct pt_regs *, regs, struct bpf_map *, map,
	   u64, flags, void *, data, u64, size)
{
	// struct perf_raw_record raw = {
	// 	.frag = {
	// 		.size = size,
	// 		.data = data,
	// 	},
	// };
	struct perf_raw_record raw = {
		.data = data,
		.size = size,
	};

	if (unlikely(flags & ~(BPF_F_INDEX_MASK)))
		return -EINVAL;

	return __bpf_perf_event_output(regs, map, flags, &raw);
}

static const struct bpf_func_proto bpf_perf_event_output_proto = {
	.func		= bpf_perf_event_output,
	.gpl_only	= true,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_CONST_MAP_PTR,
	.arg3_type	= ARG_ANYTHING,
	.arg4_type	= ARG_PTR_TO_MEM,
	.arg5_type	= ARG_CONST_SIZE_OR_ZERO,
};

// static DEFINE_PER_CPU(struct pt_regs, bpf_pt_regs);

// u64 bpf_event_output(struct bpf_map *map, u64 flags, void *meta, u64 meta_size,
// 		     void *ctx, u64 ctx_size, bpf_ctx_copy_t ctx_copy)
// {
	// struct pt_regs *regs = this_cpu_ptr(&bpf_pt_regs);
	// struct perf_raw_frag frag = {
	// 	.copy		= ctx_copy,
	// 	.size		= ctx_size,
	// 	.data		= ctx,
	// };
	// struct perf_raw_record raw = {
	// 	.frag = {
	// 		{
	// 			.next	= ctx_size ? &frag : NULL,
	// 		},
	// 		.size	= meta_size,
	// 		.data	= meta,
	// 	},
	// };

	// perf_fetch_caller_regs(regs);

	// return __bpf_perf_event_output(regs, map, flags, &raw);
// 	return 0;
// }

BPF_CALL_0(bpf_get_current_task)
{
	return (long) current;
}

static const struct bpf_func_proto bpf_get_current_task_proto = {
	.func		= bpf_get_current_task,
	.gpl_only	= true,
	.ret_type	= RET_INTEGER,
};

// BPF_CALL_3(bpf_probe_read_str, void *, dst, u32, size,
// 	   const void *, unsafe_ptr)
// {
// 	int ret;

// 	/*
// 	 * The strncpy_from_unsafe() call will likely not fill the entire
// 	 * buffer, but that's okay in this circumstance as we're probing
// 	 * arbitrary memory anyway similar to bpf_probe_read() and might
// 	 * as well probe the stack. Thus, memory is explicitly cleared
// 	 * only in error case, so that improper users ignoring return
// 	 * code altogether don't copy garbage; otherwise length of string
// 	 * is returned that can be used for bpf_perf_event_output() et al.
// 	 */
// 	ret = strncpy_from_unsafe(dst, unsafe_ptr, size);
// 	if (unlikely(ret < 0))
// 		memset(dst, 0, size);

// 	return ret;
// }

// static const struct bpf_func_proto bpf_probe_read_str_proto = {
// 	.func		= bpf_probe_read_str,
// 	.gpl_only	= true,
// 	.ret_type	= RET_INTEGER,
// 	.arg1_type	= ARG_PTR_TO_UNINIT_MEM,
// 	.arg2_type	= ARG_CONST_SIZE_OR_ZERO,
// 	.arg3_type	= ARG_ANYTHING,
// };

static const struct bpf_func_proto *
tracing_func_proto(enum bpf_func_id func_id, const struct bpf_prog *prog)
{
	switch (func_id) {
	case BPF_FUNC_map_lookup_elem:
		return &bpf_map_lookup_elem_proto;
	case BPF_FUNC_map_update_elem:
		return &bpf_map_update_elem_proto;
	case BPF_FUNC_map_delete_elem:
		return &bpf_map_delete_elem_proto;
	case BPF_FUNC_probe_read:
		return &bpf_probe_read_proto;
	case BPF_FUNC_ktime_get_ns:
		return &bpf_ktime_get_ns_proto;
	// case BPF_FUNC_tail_call:
	// 	return &bpf_tail_call_proto;
	case BPF_FUNC_get_current_pid_tgid:
		return &bpf_get_current_pid_tgid_proto;
	case BPF_FUNC_get_current_task:
		return &bpf_get_current_task_proto;
	// case BPF_FUNC_get_current_uid_gid:
	// 	return &bpf_get_current_uid_gid_proto;
	case BPF_FUNC_get_current_comm:
		return &bpf_get_current_comm_proto;
	case BPF_FUNC_trace_printk:
		return bpf_get_trace_printk_proto();
	case BPF_FUNC_get_smp_processor_id:
		return &bpf_get_smp_processor_id_proto;
	// case BPF_FUNC_get_numa_node_id:
	// 	return &bpf_get_numa_node_id_proto;
	// case BPF_FUNC_perf_event_read:
	// 	return &bpf_perf_event_read_proto;
	// case BPF_FUNC_probe_write_user:
	// 	return bpf_get_probe_write_proto();
	// case BPF_FUNC_get_prandom_u32:
	// 	return &bpf_get_prandom_u32_proto;
	// case BPF_FUNC_probe_read_str:
	// 	return &bpf_probe_read_str_proto;
	default:
		return NULL;
	}
}

static const struct bpf_func_proto *
kprobe_prog_func_proto(enum bpf_func_id func_id, const struct bpf_prog *prog)
{
	switch (func_id) {
	case BPF_FUNC_perf_event_output:
		return &bpf_perf_event_output_proto;
	case BPF_FUNC_get_stackid:
		return &bpf_get_stackid_proto;
	// case BPF_FUNC_get_stack:
	// 	return &bpf_get_stack_proto;
	// case BPF_FUNC_perf_event_read_value:
	// 	return &bpf_perf_event_read_value_proto;
	// case BPF_FUNC_override_return:
	// 	pr_warn_ratelimited("%s[%d] is installing a program with bpf_override_return helper that may cause unexpected behavior!",
	// 			    current->comm, task_pid_nr(current));
	// 	return &bpf_override_return_proto;
	default:
		return tracing_func_proto(func_id, prog);
	}
}

// /* bpf+kprobe programs can access fields of 'struct pt_regs' */
static bool kprobe_prog_is_valid_access(int off, int size, enum bpf_access_type type,
					const struct bpf_prog *prog,
					struct bpf_insn_access_aux *info)
{
	if (off < 0 || off >= sizeof(struct pt_regs))
		return false;
	if (type != BPF_READ)
		return false;
	if (off % size != 0)
		return false;
	/*
	 * Assertion for 32 bit to make sure last 8 byte access
	 * (BPF_DW) to the last 4 byte member is disallowed.
	 */
	if (off + size > sizeof(struct pt_regs))
		return false;

	return true;
}

const struct bpf_verifier_ops kprobe_verifier_ops = {
	.get_func_proto  = kprobe_prog_func_proto,
	.is_valid_access = kprobe_prog_is_valid_access,
};

const struct bpf_prog_ops kprobe_prog_ops = {
};

static DEFINE_PER_CPU(struct pt_regs, bpf_raw_tp_regs);
BPF_CALL_5(bpf_perf_event_output_tp, void *, tp_buff, struct bpf_map *, map,
	   u64, flags, void *, data, u64, size)
{
	// struct pt_regs *regs = *(struct pt_regs **)tp_buff;
	struct pt_regs *regs = this_cpu_ptr(&bpf_raw_tp_regs);
	perf_fetch_caller_regs(regs);

	/*
	 * r1 points to perf tracepoint buffer where first 8 bytes are hidden
	 * from bpf program and contain a pointer to 'struct pt_regs'. Fetch it
	 * from there and call the same bpf_perf_event_output() helper inline.
	 */
	return ____bpf_perf_event_output(regs, map, flags, data, size);
}

static const struct bpf_func_proto bpf_perf_event_output_proto_tp = {
	.func		= bpf_perf_event_output_tp,
	.gpl_only	= true,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_CONST_MAP_PTR,
	.arg3_type	= ARG_ANYTHING,
	.arg4_type	= ARG_PTR_TO_MEM,
	.arg5_type	= ARG_CONST_SIZE_OR_ZERO,
};

// BPF_CALL_3(bpf_get_stackid_tp, void *, tp_buff, struct bpf_map *, map,
// 	   u64, flags)
// {
// 	struct pt_regs *regs = *(struct pt_regs **)tp_buff;

// 	/*
// 	 * Same comment as in bpf_perf_event_output_tp(), only that this time
// 	 * the other helper's function body cannot be inlined due to being
// 	 * external, thus we need to call raw helper function.
// 	 */
// 	return bpf_get_stackid((unsigned long) regs, (unsigned long) map,
// 			       flags, 0, 0);
// }

// static const struct bpf_func_proto bpf_get_stackid_proto_tp = {
// 	.func		= bpf_get_stackid_tp,
// 	.gpl_only	= true,
// 	.ret_type	= RET_INTEGER,
// 	.arg1_type	= ARG_PTR_TO_CTX,
// 	.arg2_type	= ARG_CONST_MAP_PTR,
// 	.arg3_type	= ARG_ANYTHING,
// };

// BPF_CALL_4(bpf_get_stack_tp, void *, tp_buff, void *, buf, u32, size,
// 	   u64, flags)
// {
// 	struct pt_regs *regs = *(struct pt_regs **)tp_buff;

// 	return bpf_get_stack((unsigned long) regs, (unsigned long) buf,
// 			     (unsigned long) size, flags, 0);
// }

// static const struct bpf_func_proto bpf_get_stack_proto_tp = {
// 	.func		= bpf_get_stack_tp,
// 	.gpl_only	= true,
// 	.ret_type	= RET_INTEGER,
// 	.arg1_type	= ARG_PTR_TO_CTX,
// 	.arg2_type	= ARG_PTR_TO_UNINIT_MEM,
// 	.arg3_type	= ARG_CONST_SIZE_OR_ZERO,
// 	.arg4_type	= ARG_ANYTHING,
// };

static const struct bpf_func_proto *
tp_prog_func_proto(enum bpf_func_id func_id, const struct bpf_prog *prog)
{
	switch (func_id) {
	case BPF_FUNC_perf_event_output:
		return &bpf_perf_event_output_proto_tp;
	// case BPF_FUNC_get_stackid:
	// 	return &bpf_get_stackid_proto_tp;
	// case BPF_FUNC_get_stack:
	// 	return &bpf_get_stack_proto_tp;
	default:
		return tracing_func_proto(func_id, prog);
	}
}

static bool tp_prog_is_valid_access(int off, int size, enum bpf_access_type type,
				    const struct bpf_prog *prog,
				    struct bpf_insn_access_aux *info)
{
#define PERF_MAX_TRACE_SIZE	2048
// fix for raw tp
	if (off < 0|| off >= PERF_MAX_TRACE_SIZE)
		return false;
	if (type != BPF_READ)
		return false;
	if (off % size != 0)
		return false;

	BUILD_BUG_ON(PERF_MAX_TRACE_SIZE % sizeof(__u64));
	return true;
}

const struct bpf_verifier_ops tracepoint_verifier_ops = {
	.get_func_proto  = tp_prog_func_proto,
	.is_valid_access = tp_prog_is_valid_access,
};

const struct bpf_prog_ops tracepoint_prog_ops = {
};

// BPF_CALL_3(bpf_perf_prog_read_value, struct bpf_perf_event_data_kern *, ctx,
// 	   struct bpf_perf_event_value *, buf, u32, size)
// {
// 	int err = -EINVAL;

// 	if (unlikely(size != sizeof(struct bpf_perf_event_value)))
// 		goto clear;
// 	err = perf_event_read_local_p(ctx->event, &buf->counter, &buf->enabled,
// 				    &buf->running);
// 	if (unlikely(err))
// 		goto clear;
// 	return 0;
// clear:
// 	memset(buf, 0, size);
// 	return err;
// }

// static const struct bpf_func_proto bpf_perf_prog_read_value_proto = {
//          .func           = bpf_perf_prog_read_value,
//          .gpl_only       = true,
//          .ret_type       = RET_INTEGER,
//          .arg1_type      = ARG_PTR_TO_CTX,
//          .arg2_type      = ARG_PTR_TO_UNINIT_MEM,
//          .arg3_type      = ARG_CONST_SIZE,
// };

// static const struct bpf_func_proto *
// pe_prog_func_proto(enum bpf_func_id func_id, const struct bpf_prog *prog)
// {
// 	switch (func_id) {
// 	case BPF_FUNC_perf_event_output:
// 		return &bpf_perf_event_output_proto_tp;
// 	case BPF_FUNC_get_stackid:
// 		return &bpf_get_stackid_proto_tp;
// 	case BPF_FUNC_get_stack:
// 		return &bpf_get_stack_proto_tp;
// 	case BPF_FUNC_perf_prog_read_value:
// 		return &bpf_perf_prog_read_value_proto;
// 	default:
// 		return tracing_func_proto(func_id, prog);
// 	}
// }

// static bool pe_prog_is_valid_access(int off, int size, enum bpf_access_type type,
// 				    const struct bpf_prog *prog,
// 				    struct bpf_insn_access_aux *info)
// {
// 	const int size_sp = FIELD_SIZEOF(struct bpf_perf_event_data,
// 					 sample_period);

// 	if (off < 0 || off >= sizeof(struct bpf_perf_event_data))
// 		return false;
// 	if (type != BPF_READ)
// 		return false;
// 	if (off % size != 0) {
// 		if (sizeof(unsigned long) != 4)
// 			return false;
// 		if (size != 8)
// 			return false;
// 		if (off % size != 4)
// 			return false;
// 	}

// 	switch (off) {
// 	case bpf_ctx_range(struct bpf_perf_event_data, sample_period):
// 		bpf_ctx_record_field_size(info, size_sp);
// 		if (!bpf_ctx_narrow_access_ok(off, size, size_sp))
// 			return false;
// 		break;
// 	default:
// 		if (size != sizeof(long))
// 			return false;
// 	}

// 	return true;
// }

// static u32 pe_prog_convert_ctx_access(enum bpf_access_type type,
// 				      const struct bpf_insn *si,
// 				      struct bpf_insn *insn_buf,
// 				      struct bpf_prog *prog, u32 *target_size)
// {
// 	struct bpf_insn *insn = insn_buf;

// 	switch (si->off) {
// 	case offsetof(struct bpf_perf_event_data, sample_period):
// 		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(struct bpf_perf_event_data_kern,
// 						       data), si->dst_reg, si->src_reg,
// 				      offsetof(struct bpf_perf_event_data_kern, data));
// 		*insn++ = BPF_LDX_MEM(BPF_DW, si->dst_reg, si->dst_reg,
// 				      bpf_target_off(struct perf_sample_data, period, 8,
// 						     target_size));
// 		break;
// 	default:
// 		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(struct bpf_perf_event_data_kern,
// 						       regs), si->dst_reg, si->src_reg,
// 				      offsetof(struct bpf_perf_event_data_kern, regs));
// 		*insn++ = BPF_LDX_MEM(BPF_SIZEOF(long), si->dst_reg, si->dst_reg,
// 				      si->off);
// 		break;
// 	}

// 	return insn - insn_buf;
// }

// const struct bpf_verifier_ops perf_event_verifier_ops = {
// 	.get_func_proto		= pe_prog_func_proto,
// 	.is_valid_access	= pe_prog_is_valid_access,
// 	.convert_ctx_access	= pe_prog_convert_ctx_access,
// };

// const struct bpf_prog_ops perf_event_prog_ops = {
// };

// static DEFINE_MUTEX(bpf_event_mutex);

// #define BPF_TRACE_MAX_PROGS 64

// int perf_event_attach_bpf_prog(struct perf_event *event,
// 			       struct bpf_prog *prog)
// {
// 	struct bpf_prog_array __rcu *old_array;
// 	struct bpf_prog_array *new_array;
// 	int ret = -EEXIST;

// 	/* Kprobe override only works for ftrace based kprobes. */
// 	if (prog->kprobe_override && !trace_kprobe_ftrace(event->tp_event))
// 		return -EINVAL;

// 	mutex_lock(&bpf_event_mutex);

// 	if (event->prog)
// 		goto unlock;

// 	old_array = event->tp_event->rh_data->prog_array;
// 	if (old_array &&
// 	    bpf_prog_array_length(old_array) >= BPF_TRACE_MAX_PROGS) {
// 		ret = -E2BIG;
// 		goto unlock;
// 	}

// 	ret = bpf_prog_array_copy(old_array, NULL, prog, &new_array);
// 	if (ret < 0)
// 		goto unlock;

// 	/* set the new array to event->tp_event and set event->prog */
// 	event->prog = prog;
// 	rcu_assign_pointer(event->tp_event->rh_data->prog_array, new_array);
// 	bpf_prog_array_free(old_array);

// unlock:
// 	mutex_unlock(&bpf_event_mutex);
// 	return ret;
// }

// void perf_event_detach_bpf_prog(struct perf_event *event)
// {
// 	struct bpf_prog_array __rcu *old_array;
// 	struct bpf_prog_array *new_array;
// 	int ret;

// 	mutex_lock(&bpf_event_mutex);

// 	if (!event->prog)
// 		goto unlock;

// 	old_array = event->tp_event->rh_data->prog_array;
// 	ret = bpf_prog_array_copy(old_array, event->prog, NULL, &new_array);
// 	if (ret == -ENOENT)
// 		goto unlock;
// 	if (ret < 0) {
// 		bpf_prog_array_delete_safe(old_array, event->prog);
// 	} else {
// 		rcu_assign_pointer(event->tp_event->rh_data->prog_array, new_array);
// 		bpf_prog_array_free(old_array);
// 	}

// 	bpf_prog_put(event->prog);
// 	event->prog = NULL;

// unlock:
// 	mutex_unlock(&bpf_event_mutex);
// }

// int perf_event_query_prog_array(struct perf_event *event, void __user *info)
// {
// 	struct perf_event_query_bpf __user *uquery = info;
// 	struct perf_event_query_bpf query = {};
// 	u32 *ids, prog_cnt, ids_len;
// 	int ret;

// 	if (!capable(CAP_SYS_ADMIN))
// 		return -EPERM;
// 	if (event->attr.type != PERF_TYPE_TRACEPOINT)
// 		return -EINVAL;
// 	if (copy_from_user(&query, uquery, sizeof(query)))
// 		return -EFAULT;

// 	ids_len = query.ids_len;
// 	if (ids_len > BPF_TRACE_MAX_PROGS)
// 		return -E2BIG;
// 	ids = kcalloc(ids_len, sizeof(u32), GFP_USER | __GFP_NOWARN);
// 	if (!ids)
// 		return -ENOMEM;
// 	/*
// 	 * The above kcalloc returns ZERO_SIZE_PTR when ids_len = 0, which
// 	 * is required when user only wants to check for uquery->prog_cnt.
// 	 * There is no need to check for it since the case is handled
// 	 * gracefully in bpf_prog_array_copy_info.
// 	 */

// 	mutex_lock(&bpf_event_mutex);
// 	ret = bpf_prog_array_copy_info(event->tp_event->rh_data->prog_array,
// 				       ids,
// 				       ids_len,
// 				       &prog_cnt);
// 	mutex_unlock(&bpf_event_mutex);

// 	if (copy_to_user(&uquery->prog_cnt, &prog_cnt, sizeof(prog_cnt)) ||
// 	    copy_to_user(uquery->ids, ids, ids_len * sizeof(u32)))
// 		ret = -EFAULT;

// 	kfree(ids);
// 	return ret;
// }
