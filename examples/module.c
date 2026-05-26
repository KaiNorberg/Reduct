#include <reduct/reduct.h>

reduct_handle_t my_native(reduct_t* reduct, size_t argc, reduct_handle_t* argv)
{
    return REDUCT_HANDLE_FROM_NUMBER(52.0);
}

reduct_handle_t reduct_module_init(reduct_t* reduct)
{
    return REDUCT_HANDLE_CREATE_ALIST(reduct, 1,
        "my-native", REDUCT_HANDLE_CREATE_NATIVE(reduct, my_native)
    );
}