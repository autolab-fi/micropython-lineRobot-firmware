#ifndef COEFFICIENT_VALIDATION_H
#define COEFFICIENT_VALIDATION_H

#include <stdbool.h>

typedef enum {
    COEFFICIENT_FLOAT,
    COEFFICIENT_INT,
} coefficient_type_t;

typedef struct {
    const char *name;
    coefficient_type_t type;
    double minimum;
    double maximum;
} coefficient_spec_t;

const coefficient_spec_t *find_coefficient_spec(const char *name);
bool validate_coefficient_value(const coefficient_spec_t *spec, const char *type, double value);

#endif
