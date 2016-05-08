#include <ruby.h>
#include <ruby/debug.h>
#include <ruby/version.h>
#include <ruby/vm.h>
#include <ruby/util.h>

// shitty way to get to the event flag in all versions of ruby.  We know
// it's the first member of the struct that's why this works.
#if RUBY_API_VERSION_MAJOR > 2 || (RUBY_API_VERSION_MAJOR == 2 && RUBY_API_VERSION_MINOR >= 1)
#   define TRACEPOINT_EVENT_FLAG(x) rb_tracearg_event_flag(x)
#else
#   define TRACEPOINT_EVENT_FLAG(x) (*(rb_event_flag_t *)x)
#endif

static ID id_trace_stack;
static ID id_rbtrace_state;
static VALUE thread_stack_state_class;
static VALUE frame_class;

struct thread_stack_frame;
struct thread_stack_frame {
    VALUE path;
    long lineno;
    VALUE method_id;
    VALUE binding;
    struct thread_stack_frame *prev;
};

struct thread_stack_state {
    struct thread_stack_frame *top;
    struct thread_stack_frame *freelist;
};

static void
thread_stack_frame_free(void *f)
{
    xfree(f);
}

static void
thread_stack_state_free(void *s)
{
    struct thread_stack_state *state = (struct thread_stack_state *)s;
    struct thread_stack_frame *frm, *prev;

    frm = state->top;
    while (frm) {
        prev = frm->prev;
        thread_stack_frame_free(frm);
        frm = prev;
    }

    frm = state->freelist;
    while (frm) {
        prev = frm->prev;
        thread_stack_frame_free(frm);
        frm = prev;
    }

    xfree(state);
}

static void
thread_stack_state_mark(void *s)
{
    struct thread_stack_state *state = (struct thread_stack_state *)s;
    struct thread_stack_frame *frm;

    for (frm = state->top; frm; frm = frm->prev) {
        rb_gc_mark(frm->path);
        rb_gc_mark(frm->method_id);
        rb_gc_mark(frm->binding);
    }
}

static VALUE
thread_stack_state_alloc(VALUE self)
{
    struct thread_stack_state *state = ALLOC(struct thread_stack_state);
    memset(state, 0, sizeof(struct thread_stack_state));
    return Data_Wrap_Struct(self, thread_stack_state_mark,
                            thread_stack_state_free, state);
}

static VALUE
thread_stack_state_init_copy(VALUE copy, VALUE orig)
{
    struct thread_stack_state *copy_state, *orig_state;
    struct thread_stack_frame *copy_frm, *orig_frm;

    if (copy == orig) {
        return copy;
    }

    Data_Get_Struct(copy, struct thread_stack_state, copy_state);
    Data_Get_Struct(orig, struct thread_stack_state, orig_state);

    for (orig_frm = orig_state->top; orig_frm; orig_frm = orig_frm->prev) {
        copy_frm = ALLOC(struct thread_stack_frame);
        memcpy(copy_frm, orig_frm, sizeof(struct thread_stack_frame));
        copy_frm->prev = copy_state->top;
        copy_state->top = copy_frm;
    }

    return copy;
}

static struct thread_stack_state *
thread_stack_state_get()
{
    struct thread_stack_state *state;
    VALUE top = rb_thread_local_aref(rb_thread_current(), id_trace_stack);
    if (top == Qnil) {
        top = rb_obj_alloc(thread_stack_state_class);
        rb_obj_call_init(top, 0, NULL);
        rb_thread_local_aset(rb_thread_current(), id_trace_stack, top);
    }
    Data_Get_Struct(top, struct thread_stack_state, state);
    return state;
}

static void
thread_stack_state_push(rb_trace_arg_t *targ)
{
    struct thread_stack_state *state = thread_stack_state_get();
    struct thread_stack_frame *frm;
    VALUE lineno;

    if (state->freelist) {
        frm = state->freelist;
        state->freelist = frm->prev;
    } else {
        frm = ALLOC(struct thread_stack_frame);
    }
    lineno = rb_tracearg_lineno(targ);
    frm->path = rb_tracearg_path(targ);
    frm->lineno = lineno != Qnil ? FIX2LONG(lineno) : 0;
    frm->method_id = rb_tracearg_method_id(targ);
    frm->binding = rb_tracearg_binding(targ);
    frm->prev = state->top;
    state->top = frm;
}

static void
thread_stack_state_pop(void)
{
    struct thread_stack_state *state = thread_stack_state_get();
    struct thread_stack_frame *frm;
    if (state->top) {
        frm = state->top;
        state->top = frm->prev;
        frm->prev = state->freelist;
        state->freelist = frm;
    }
}

static void
thread_stack_state_attach_to_exception(rb_trace_arg_t *targ)
{
    VALUE exc = rb_tracearg_raised_exception(targ);
    VALUE state = rb_thread_local_aref(rb_thread_current(), id_trace_stack);

    if (state == Qnil || exc == Qnil) {
        return;
    }

    rb_ivar_set(exc, id_rbtrace_state, rb_obj_dup(state));
}

static VALUE
thread_stack_state_get_frames(VALUE self)
{
    struct thread_stack_state *state;
    struct thread_stack_frame *frm;
    VALUE hl_frm;
    VALUE rv = rb_ary_new();
    Data_Get_Struct(self, struct thread_stack_state, state);

    if (state) {
        for (frm = state->top; frm; frm = frm->prev) {
            VALUE args[4] = {
                frm->path,
                LONG2FIX(frm->lineno),
                frm->method_id,
                frm->binding
            };
            hl_frm = rb_obj_alloc(frame_class);
            rb_obj_call_init(hl_frm, 4, args);
            rb_ary_push(rv, hl_frm);
        }
    }

    return rv;
}

static void
tracepoint_callback(VALUE tp, void *data)
{
    rb_trace_arg_t *targ = rb_tracearg_from_tracepoint(tp);

    switch (TRACEPOINT_EVENT_FLAG(targ)) {
    case RUBY_EVENT_CALL:
        thread_stack_state_push(targ);
        break;
    case RUBY_EVENT_RETURN:
        thread_stack_state_pop();
        break;
    case RUBY_EVENT_RAISE:
        thread_stack_state_attach_to_exception(targ);
        break;
    }
}

static VALUE
tracepoint_create(VALUE self)
{
    VALUE tp = rb_tracepoint_new(0, RUBY_EVENT_CALL | RUBY_EVENT_RETURN |
                                 RUBY_EVENT_RAISE,
                                 tracepoint_callback, 0);
    return tp;
}

void
Init_rbtrace(void)
{
    VALUE mod;

    id_rbtrace_state = rb_intern("@__rbtrace_state");

    mod = rb_const_get(rb_cObject, rb_intern("RbTrace"));
    frame_class = rb_const_get(mod, rb_intern("Frame"));

    thread_stack_state_class = rb_define_class_under(
        mod, "StackState", rb_cObject);
    rb_define_alloc_func(thread_stack_state_class, thread_stack_state_alloc);
    rb_define_method(thread_stack_state_class, "initialize_copy",
                     thread_stack_state_init_copy, 1);
    rb_define_method(thread_stack_state_class, "frames",
                     thread_stack_state_get_frames, 0);

    rb_define_module_function(
        mod, "make_tracepoint", tracepoint_create, 0);
}
