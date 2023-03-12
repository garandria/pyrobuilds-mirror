// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 Patrick Franz <deltaone@debian.org>
 */

#define _GNU_SOURCE
#include <assert.h>
#include <locale.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "configfix.h"

static struct symbol_dvalue * sym_create_sdv(struct symbol *sym, char *input);

/* -------------------------------------- */

static int same_symbol_fix(struct symbol_fix *x, struct symbol_fix *y)
{
        return !(strcmp(sym_get_name(x->sym), sym_get_name(y->sym)))
                && x->tri == y->tri;
}

static int same_sfix_node(struct sfix_node *x, struct sfix_node *y)
{
        return same_symbol_fix(x->elem, y->elem);
}

static int same_sfix_list(struct sfix_list *l1, struct sfix_list *l2)
{
        struct sfix_node *n1, *n2;
        int found;

        if (l1->size != l2->size)
                return -1;

        sfix_list_for_each(n1, l1) {
                found = -1;
                sfix_list_for_each(n2, l2) {
                        if (same_sfix_node(n1, n2)) {
                                found = 1;
                                break;
                        }
                }
                if (!found)
                        return found;
        }
        return 1;
}

static int same_sfl_node(struct sfl_node *x, struct sfl_node *y)
{
        return same_sfix_list(x->elem, y->elem);
}

static struct sfl_list *sfl_list_remove_duplicate(struct sfl_list *l)
{
        struct sfl_node *n, *nn;
        struct sfl_list *res = sfl_list_init();
        sfl_list_add(res, l->head->elem);

        sfl_list_for_each(n, l) {
                sfl_list_for_each(nn, res) {
                        if (!same_sfl_node(n, nn)) {
                                sfl_list_add(res, n->elem);
                                break;
                        }
						printd("Fail!");
                }
        }
        return res;
}

int main(int argc, char *argv[])
{
		CFDEBUG = true;

		if (argc > 1 && !strcmp(argv[1], "-s")) {
				printd("\nHello configfix!\n\n");

				run_satconf_cli(argv[2]);
				return 0;
		}

		init_config(argv[1]);

		struct sfl_list *diagnoses, *ddiagnoses;
		struct sdv_list *symbols;
		char *option = argv[2];
		char *newval = argv[3];

		/* create the array */
		symbols = sdv_list_init();

		struct symbol  *sym;
		if (((sym = sym_find(option)) == NULL)) {
				printd("Symbol %s not found!\n", option);
				return -1;
		}

		if (!strcmp(sym_get_string_value(sym), newval)) {
				printd("Symbol %s is already set to %s", option, newval);
				return 42;
		}

		struct symbol_dvalue *sdv = sym_create_sdv(sym, newval);
		sdv_list_add(symbols, sdv);

		diagnoses = run_satconf(symbols);

		if (diagnoses == NULL) {
				printd("Ready\n");
				sym_set_string_value(sym, newval);
				sym_calc_value(sym);
				if (conf_write(NULL) < 0) {
						return -1;
				}
				return 0;
		}

		if (diagnoses->size == 0) {
				printd("No diagnosis\n");
				return -1;
		}

		ddiagnoses = sfl_list_remove_duplicate(diagnoses);
		struct sfl_node *node;
		sfl_list_for_each(node, ddiagnoses) {
				if (CFDEBUG){
						printd("Trying: \n");
						print_diagnosis_symbol(node->elem);
				}
				if (apply_fix(node->elem) != 0){
						if (conf_write(NULL) < 0) {
								return -1;
						}
						return 0;
				}
		}

		return -1;
}

/*
 * create a symbol_dvalue struct containing the symbol and the desired value
 */
static struct symbol_dvalue * sym_create_sdv(struct symbol *sym, char *input)
{
		struct symbol_dvalue *sdv = malloc(sizeof(struct symbol_dvalue));
		sdv->sym = sym;
		sdv->type = sym_is_boolean(sym) ? SDV_BOOLEAN : SDV_NONBOOLEAN;

		if (sym_is_boolean(sym)) {
				if (strcmp(input, "y") == 0)
						sdv->tri = yes;
				else if (strcmp(input, "m") == 0)
						sdv->tri = mod;
				else if (strcmp(input, "n") == 0)
						sdv->tri = no;
				else
						perror("Not a valid tristate value.");

				/* sanitize input for booleans */
				if (sym->type == S_BOOLEAN && sdv->tri == mod)
						sdv->tri = yes;
		} else if (sym_is_nonboolean(sym)) {
				sdv->nb_val = str_new();
				str_append(&sdv->nb_val, input);
		}

		return sdv;
}
