struct bpf_dispatcher;
struct bpf_prog;

struct bpf_prog *nf_hook_bpf_create(const struct nf_hook_entries *n);
struct bpf_prog *nf_hook_bpf_create_fb(void);

#if IS_ENABLED(CONFIG_NF_HOOK_BPF)
void nf_hook_bpf_change_prog(struct bpf_dispatcher *d, struct bpf_prog *from, struct bpf_prog *to);
#else
static inline void
nf_hook_bpf_change_prog(struct bpf_dispatcher *d, struct bpf_prog *f, struct bpf_prog *t)
{
}
#endif
