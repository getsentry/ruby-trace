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
static VALUE raw_frame_struct;

static VALUE
get_thread_stack()
{
    return rb_thread_local_aref(rb_thread_current(), id_trace_stack);
}

static void
push_frame(rb_trace_arg_t *targ)
{
    VALUE stack = get_thread_stack();
    if (stack == Qnil) {
        stack = rb_ary_new();
        rb_thread_local_aset(rb_thread_current(), id_trace_stack, stack);
    }

    VALUE raw_frame = rb_funcall(raw_frame_struct, rb_intern("new"), 4,
                                 rb_tracearg_path(targ),
                                 rb_tracearg_lineno(targ),
                                 rb_tracearg_method_id(targ),
                                 rb_tracearg_binding(targ));
    rb_ary_push(stack, raw_frame);
}

static void
pop_frame(rb_trace_arg_t *targ)
{
    VALUE stack = get_thread_stack();
    if (stack != Qnil) {
        rb_ary_pop(stack);
    }
}

static void
attach_stack_to_exception(rb_trace_arg_t *targ)
{
    VALUE exc = rb_tracearg_raised_exception(targ);
    VALUE stack = get_thread_stack();

    if (stack == Qnil || exc == Qnil) {
        return;
    }

    rb_ivar_set(exc, rb_intern("@__rbtrace_stack"), rb_obj_dup(stack));
}

static void
tracepoint_callback(VALUE tp, void *data)
{
    rb_trace_arg_t *targ = rb_tracearg_from_tracepoint(tp);

    switch (TRACEPOINT_EVENT_FLAG(targ)) {
    case RUBY_EVENT_CALL:
        push_frame(targ);
        break;
    case RUBY_EVENT_RETURN:
        pop_frame(targ);
        break;
    case RUBY_EVENT_RAISE:
        attach_stack_to_exception(targ);
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

    mod = rb_const_get(rb_cObject, rb_intern("RbTrace"));
    rb_define_module_function(
        mod, "make_tracepoint", tracepoint_create, 0);
    raw_frame_struct = rb_const_get(mod, rb_intern("RawFrame"));
}
