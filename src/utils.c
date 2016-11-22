#include "php_tarantool.h"

#include "utils.h"

#include <stdio.h>

const char *tutils_op_to_string(zval *obj) {
	zend_uchar type = Z_TYPE_P(obj);
	switch(type) {
	case(IS_UNDEF):
		return "UNDEF";
	case(IS_NULL):
		return "NULL";
	case(IS_FALSE):
		return "FALSE";
	case(IS_TRUE):
		return "TRUE";
	case(IS_LONG):
		return "LONG";
	case(IS_DOUBLE):
		return "DOUBLE";
	case(IS_STRING):
		return "STRING";
	case(IS_ARRAY):
		return "ARRAY";
	case(IS_OBJECT):
		return "OBJECT";
	case(IS_RESOURCE):
		return "RESOURCE";
	case(IS_REFERENCE):
		return "REFERENCE";
	case(IS_CONSTANT):
		return "CONSTANT";
	case(IS_CONSTANT_AST):
		return "CONSTANT_AST";
	case(IS_CALLABLE):
		return "CALLABLE";
	default:
		return "UNKNOWN";
	}
}

void tutils_hexdump_base (FILE *ostream, char *desc, const char *addr, size_t len) {
	size_t i;
	unsigned char buff[17];
	const unsigned char *pc = (const unsigned char *)addr;

	if (desc != NULL) {
		fprintf(ostream, "%s:\n", desc);
	}

	for (i = 0; i < len; i++) {
		if (i % 16 == 0) {
			if (i != 0) fprintf(ostream, "  %s\n", buff);
			fprintf(ostream, "  %04zx ", i);
		}

		fprintf(ostream, " %02x", *pc);

		if ((*pc < 0x20) || (*pc > 0x7e))
			buff[i % 16] = '.';
		else
			buff[i % 16] = *pc;
		buff[(i % 16) + 1] = '\0';
		++pc;
	}

	while (i++ % 16 != 0) fprintf(ostream, "   ");

	fprintf(ostream, "  %s\n\n", buff);
}

void tutils_hexdump (char *desc, const char *addr, size_t len) {
	return tutils_hexdump_base(stdout, desc, addr, len);
}

void tutils_hexdump_zs (char *desc, zend_string *val) {
	return tutils_hexdump(desc, ZSTR_VAL(val), ZSTR_LEN(val));
}

void tutils_hexdump_ss (char *desc, smart_string *val) {
	return tutils_hexdump(desc, val->c, val->len);
}
