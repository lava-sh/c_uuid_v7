#include <hpy.h>
#include <hpy/runtime/argparse.h>

long c_uuid_v7_sum(long a, long b);

HPyDef_METH(sum, "sum", HPyFunc_VARARGS, .doc = "Return the sum of two integers.")
static HPy sum_impl(HPyContext *ctx, HPy self, const HPy *args, size_t nargs)
{
    long a;
    long b;

    (void)self;

    if (!HPyArg_Parse(ctx, NULL, args, nargs, "ll", &a, &b)) {
        return HPy_NULL;
    }

    return HPyLong_FromLong(ctx, c_uuid_v7_sum(a, b));
}

static HPyDef *module_defines[] = {
    &sum,
    NULL,
};

static HPyModuleDef moduledef = {
    .doc = "HPy shim around the Zig sum implementation.",
    .size = 0,
    .defines = module_defines,
};

HPy_MODINIT(_core, moduledef)
