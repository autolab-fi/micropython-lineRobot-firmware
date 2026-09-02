#include "coefficient_validation.h"

#include <assert.h>
#include <math.h>
#include <stddef.h>

int main(void) {
    const coefficient_spec_t *wrad = find_coefficient_spec("wrad");
    assert(validate_coefficient_value(wrad, "float", 3.05));
    assert(!validate_coefficient_value(wrad, "float", 30.5));
    assert(!validate_coefficient_value(wrad, "int", 3.0));

    const coefficient_spec_t *encoder = find_coefficient_spec("er");
    assert(validate_coefficient_value(encoder, "int", 2376));
    assert(!validate_coefficient_value(encoder, "int", 2376.5));
    assert(!validate_coefficient_value(encoder, "float", 2376));

    assert(find_coefficient_spec("wifi_pass") == NULL);
    assert(!validate_coefficient_value(NULL, "float", 1));
    assert(!validate_coefficient_value(wrad, "float", NAN));
    return 0;
}
