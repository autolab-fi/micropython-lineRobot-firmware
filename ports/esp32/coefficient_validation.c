#include "coefficient_validation.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

static const coefficient_spec_t COEFFICIENT_SPECS[] = {
    {"pml1", COEFFICIENT_INT, 0, 39}, {"pml2", COEFFICIENT_INT, 0, 39},
    {"pmr1", COEFFICIENT_INT, 0, 39}, {"pmr2", COEFFICIENT_INT, 0, 39},
    {"pel1", COEFFICIENT_INT, 0, 39}, {"pel2", COEFFICIENT_INT, 0, 39},
    {"per1", COEFFICIENT_INT, 0, 39}, {"per2", COEFFICIENT_INT, 0, 39},
    {"pled1", COEFFICIENT_INT, 0, 39}, {"pled2", COEFFICIENT_INT, 0, 39},
    {"pclk", COEFFICIENT_INT, 0, 39}, {"psda", COEFFICIENT_INT, 0, 39},
    {"pbat", COEFFICIENT_INT, 0, 39}, {"pch", COEFFICIENT_INT, 0, 39},
    {"debug", COEFFICIENT_INT, 0, 1}, {"smi", COEFFICIENT_INT, 5, 1000},
    {"msc", COEFFICIENT_INT, 0, 100}, {"er", COEFFICIENT_INT, 100, 10000},
    {"wrad", COEFFICIENT_FLOAT, 1, 10}, {"wdist", COEFFICIENT_FLOAT, 5, 50},
    {"maxs", COEFFICIENT_FLOAT, 1, 30}, {"kpa", COEFFICIENT_FLOAT, 0, 300},
    {"kia", COEFFICIENT_FLOAT, 0, 300}, {"kda", COEFFICIENT_FLOAT, 0, 50},
    {"kpsl", COEFFICIENT_FLOAT, 0, 200}, {"kpsr", COEFFICIENT_FLOAT, 0, 200},
    {"kis", COEFFICIENT_FLOAT, 0, 100}, {"kdsl", COEFFICIENT_FLOAT, 0, 20},
    {"kdsr", COEFFICIENT_FLOAT, 0, 20}, {"ks", COEFFICIENT_FLOAT, 0, 300},
    {"ila", COEFFICIENT_FLOAT, 0, 20}, {"ils", COEFFICIENT_FLOAT, 0, 50},
};

const coefficient_spec_t *find_coefficient_spec(const char *name) {
    if (name == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < sizeof(COEFFICIENT_SPECS) / sizeof(COEFFICIENT_SPECS[0]); ++i) {
        if (strcmp(COEFFICIENT_SPECS[i].name, name) == 0) {
            return &COEFFICIENT_SPECS[i];
        }
    }
    return NULL;
}

bool validate_coefficient_value(const coefficient_spec_t *spec, const char *type, double value) {
    if (spec == NULL || type == NULL || !isfinite(value)) {
        return false;
    }
    if ((spec->type == COEFFICIENT_INT && strcmp(type, "int") != 0) ||
        (spec->type == COEFFICIENT_FLOAT && strcmp(type, "float") != 0)) {
        return false;
    }
    if (spec->type == COEFFICIENT_INT && floor(value) != value) {
        return false;
    }
    return value >= spec->minimum && value <= spec->maximum;
}
