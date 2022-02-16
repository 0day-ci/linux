// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2016 Josh Poimboeuf <jpoimboe@redhat.com>
 * Copyright (C) 2017 Joao Moreira   <jmoreira@suse.de>
 */

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "elf.h"
#include "list.h"
#include "klp-convert.h"

/*
 * Symbols parsed from symbols.klp are kept in two lists:
 * - symbols: keeps non-exported symbols
 * - exp_symbols: keeps exported symbols (__ksymtab_prefixed)
 */
static LIST_HEAD(symbols);
static LIST_HEAD(exp_symbols);

/* In-livepatch user-provided symbol positions are kept in list usr_symbols */
static LIST_HEAD(usr_symbols);

/* Converted symbols and their struct symbol -> struct sympos association */
static LIST_HEAD(converted_symbols);

struct converted_sym {
	struct list_head list;
	struct symbol *symbol;
	struct sympos sympos;
};

static void free_syms_lists(void)
{
	struct symbol_entry *entry, *aux;
	struct sympos *sp, *sp_aux;

	list_for_each_entry_safe(entry, aux, &symbols, list) {
		free(entry->object_name);
		free(entry->symbol_name);
		list_del(&entry->list);
		free(entry);
	}

	list_for_each_entry_safe(entry, aux, &exp_symbols, list) {
		free(entry->object_name);
		free(entry->symbol_name);
		list_del(&entry->list);
		free(entry);
	}

	list_for_each_entry_safe(sp, sp_aux, &usr_symbols, list) {
		free(sp->object_name);
		free(sp->symbol_name);
		list_del(&sp->list);
		free(sp);
	}
}

/* Parses file and fill symbols and exp_symbols list */
static bool load_syms_lists(const char *symbols_list)
{
	FILE *fsyms;
	size_t len = 0;
	ssize_t n;
	char *obj = NULL, *sym = NULL;
	bool ret = false;

	fsyms = fopen(symbols_list, "r");
	if (!fsyms) {
		WARN("Unable to open Symbol list: %s", symbols_list);
		return false;
	}

	/* read file format version */
	n = getline(&sym, &len, fsyms);
	if (n <= 0) {
		WARN("Unable to read Symbol list: %s", symbols_list);
		goto done;
	}

	if (strncmp(sym, "klp-convert-symbol-data.0.1", 27) != 0) {
		WARN("Symbol list is in unknown format.");
		goto done;
	}

	len = 0;
	free(sym);
	sym = NULL;

	/* read file */
	n = getline(&sym, &len, fsyms);
	while (n > 0) {
		if (sym[n-1] == '\n')
			sym[n-1] = '\0';

		/* Objects in symbols.klp are flagged with '*' */
		if (sym[0] == '*') {
			free(obj);
			obj = strdup(sym+1);
			if (!obj) {
				WARN("Unable to allocate object name\n");
				goto done;
			}
			free(sym);
		} else {
			struct symbol_entry *entry;

			if (!obj) {
				WARN("File format error\n");
				goto done;
			}

			entry = calloc(1, sizeof(struct symbol_entry));
			if (!entry) {
				WARN("Unable to allocate Symbol entry\n");
				goto done;
			}

			entry->object_name = strdup(obj);
			if (!entry->object_name) {
				WARN("Unable to allocate entry object name\n");
				free(entry);
				goto done;
			}

			entry->symbol_name = sym;
			if (strncmp(entry->symbol_name, "__ksymtab_", 10) == 0)
				list_add(&entry->list, &exp_symbols);
			else
				list_add(&entry->list, &symbols);
		}
		len = 0;
		sym = NULL;
		n = getline(&sym, &len, fsyms);
	}
	ret = true;

done:
	free(sym);
	free(obj);
	fclose(fsyms);
	return ret;
}

/* Searches for sympos of specific symbol in usr_symbols list */
static bool get_usr_sympos(struct symbol *s, struct sympos *sp)
{
	struct sympos *aux;

	list_for_each_entry(aux, &usr_symbols, list) {
		if (strcmp(aux->symbol_name, s->name) == 0) {
			sp->symbol_name = aux->symbol_name;
			sp->object_name = aux->object_name;
			sp->pos = aux->pos;
			return true;
		}
	}
	return false;
}

/* Removes symbols used for sympos annotation from livepatch elf object */
static void clear_sympos_symbols(struct section *annotation_sec,
		struct elf *klp_elf)
{
	struct symbol *sym, *aux;

	list_for_each_entry_safe(sym, aux, &klp_elf->symbols, list) {
		if (sym->sec == annotation_sec) {

			struct section *sec;
			struct rela *rela, *tmprela;

			list_for_each_entry(sec, &klp_elf->sections, list) {
				list_for_each_entry_safe(rela, tmprela, &sec->relas, list) {
					if (rela->sym == sym) {
						list_del(&rela->list);
						free(rela);
					}
				}
			}

			list_del(&sym->list);
			free(sym);
		}
	}
}

/* Removes annotation from livepatch elf object */
static void clear_sympos_annontations(struct elf *klp_elf)
{
	struct section *sec, *aux;

	list_for_each_entry_safe(sec, aux, &klp_elf->sections, list) {
		if (strncmp(sec->name, ".klp.module_relocs.", 19) == 0) {
			clear_sympos_symbols(sec, klp_elf);
			list_del(&sec->list);
			free(sec);
			continue;
		}
		if (strncmp(sec->name, ".rela.klp.module_relocs.", 24) == 0) {

			struct rela *rela, *tmprela;

			list_for_each_entry_safe(rela, tmprela, &sec->relas, list) {
				list_del(&rela->list);
				free(rela);
			}
			list_del(&sec->list);
			free(sec);
			continue;
		}
	}
}

/*
 * User provided sympos annotation checks:
 * - do two or more elements in usr_symbols have the same object and
 *   name, but different symbol position
 * - are there any usr_symbols without a rela?
 */
static bool sympos_sanity_check(struct elf *klp_elf)
{
	bool sane = true;
	struct sympos *sp, *aux;
	struct section *sec;
	struct rela *rela;

	list_for_each_entry(sp, &usr_symbols, list) {
		bool found_rela = false;

		aux = list_next_entry(sp, list);
		list_for_each_entry_from(aux, &usr_symbols, list) {
			if (sp->pos != aux->pos &&
			    strcmp(sp->object_name, aux->object_name) == 0 &&
			    strcmp(sp->symbol_name, aux->symbol_name) == 0) {
				WARN("Conflicting KLP_SYMPOS definition: %s.%s,%d vs. %s.%s,%d.",
				sp->object_name, sp->symbol_name, sp->pos,
				aux->object_name, aux->symbol_name, aux->pos);
				sane = false;
			}
		}

		list_for_each_entry(sec, &klp_elf->sections, list) {
			list_for_each_entry(rela, &sec->relas, list) {
				if (!strcmp(sp->symbol_name, rela->sym->name)) {
					found_rela = true;
					break;
				}
			}
		}
		if (!found_rela) {
			//sane = false;
			WARN("Couldn't find rela for annotated symbol: %s",
				sp->symbol_name);
		}


	}
	return sane;
}

/* Parses the livepatch elf object and fills usr_symbols */
static bool load_usr_symbols(struct elf *klp_elf)
{
	char objname[MODULE_NAME_LEN];
	struct sympos *sp;
	struct section *sec, *relasec;
	struct rela *rela;
	Elf_Data converted_data;
	struct klp_module_reloc *reloc;
	int i, nr_entries;

	list_for_each_entry(sec, &klp_elf->sections, list) {
		if (sscanf(sec->name, ".klp.module_relocs.%55s", objname) != 1)
			continue;

		/*
		 * SYMPOS annotations are saved into arrays in
		 * .klp.module_relocs.* sections of type PROGBITS, so we
		 * need to manually translate the .sympos endianness in
		 * case we may be cross-compiling.
		 */
		sec->elf_data->d_type = ELF_T_WORD;
		converted_data.d_buf = sec->elf_data->d_buf;
		converted_data.d_size = sec->elf_data->d_size;
		converted_data.d_version = sec->elf_data->d_version;
		gelf_xlatetom(klp_elf->elf, &converted_data, sec->elf_data,
			      elf_getident(klp_elf->elf, NULL)[EI_DATA]);

		reloc = converted_data.d_buf;
		relasec = sec->rela;

		i = 0;
		nr_entries = sec->size / sizeof(*reloc);
		list_for_each_entry(rela, &relasec->relas, list) {
			if (i >= nr_entries) {
				WARN("section %s length beyond nr_entries\n",
						relasec->name);
				return false;
			}
			sp = calloc(1, sizeof(struct sympos));
			if (!sp) {
				WARN("Unable to allocate sympos memory\n");
				return false;
			}
			sp->object_name = strdup(objname);
			if (!sp->object_name) {
				WARN("Unable to allocate object name\n");
				free(sp);
				return false;
			}
			sp->symbol_name = strdup(rela->sym->name);
			if (!sp->symbol_name) {
				WARN("Unable to allocate symbol name\n");
				free(sp);
				return false;
			}
			sp->pos = reloc[i].sympos;
			list_add(&sp->list, &usr_symbols);
			i++;
		}
		if (i != nr_entries) {
			WARN("nr_entries mismatch (%d != %d) for %s\n",
					i, nr_entries, relasec->name);
			return false;
		}
	}
	clear_sympos_annontations(klp_elf);
	return sympos_sanity_check(klp_elf);
}

/* prints list of valid sympos for symbol with provided name */
static void print_valid_module_relocs(char *name)
{
	struct symbol_entry *e;
	char *cur_obj = "";
	int counter = 0;
	bool first = true;

	/* Symbols from the same object are locally gathered in the list */
	list_for_each_entry(e, &symbols, list) {
		if (strcmp(e->object_name, cur_obj) != 0) {
			cur_obj = e->object_name;
			counter = 0;
		}
		if (strcmp(e->symbol_name, name) == 0) {
			if (counter == 0) {
				if (first) {
					fprintf(stderr, "Valid KLP_SYMPOS for symbol %s:\n", name);
					fprintf(stderr, "-------------------------------------------------\n");
				} else {
					fprintf(stderr, "}\n");
				}

				fprintf(stderr, "KLP_MODULE_RELOC(%s){\n",
						cur_obj);
				first = false;
			}
			fprintf(stderr, "\tKLP_SYMPOS(%s,%d)\n", name, counter);
			counter++;
		}
	}
	if (!first) {
		fprintf(stderr, "}\n");
		fprintf(stderr, "-------------------------------------------------\n");
	}
}

/*
 * Searches for symbol in symbols list and returns its sympos if it is unique,
 * otherwise prints a list with all considered valid sympos
 */
static struct symbol_entry *find_sym_entry_by_name(char *name)
{
	struct symbol_entry *found = NULL;
	struct symbol_entry *e;

	list_for_each_entry(e, &symbols, list) {
		if (strcmp(e->symbol_name, name) == 0) {

			/*
			 * If there exist multiple symbols with the same
			 * name then user-provided sympos is required
			 */
			if (found) {
				WARN("Define KLP_SYMPOS for the symbol: %s",
						e->symbol_name);

				print_valid_module_relocs(name);
				return NULL;
			}
			found = e;
		}
	}
	if (found)
		return found;

	return NULL;
}

/* Checks if sympos is valid, otherwise prints valid sympos list */
static bool valid_sympos(struct sympos *sp)
{
	struct symbol_entry *e;

	if (sp->pos == 0) {

		/*
		 * sympos of 0 is reserved for uniquely named obj:sym,
		 * verify that this is the case
		 */
		int counter = 0;

		list_for_each_entry(e, &symbols, list) {
			if ((strcmp(e->symbol_name, sp->symbol_name) == 0) &&
			    (strcmp(e->object_name, sp->object_name) == 0)) {
				counter++;
			}
		}
		if (counter == 1)
			return true;

		WARN("Provided KLP_SYMPOS of 0, but found %d symbols matching: %s.%s,%d",
				counter, sp->object_name, sp->symbol_name,
				sp->pos);

	} else {

		/*
		 * sympos > 0 indicates a specific commonly-named obj:sym,
		 * indexing starts with 1
		 */
		int index = 1;

		list_for_each_entry(e, &symbols, list) {
			if ((strcmp(e->symbol_name, sp->symbol_name) == 0) &&
			    (strcmp(e->object_name, sp->object_name) == 0)) {
				if (index == sp->pos)
					return true;
				index++;
			}
		}

		WARN("Provided KLP_SYMPOS does not match a symbol: %s.%s,%d",
				sp->object_name, sp->symbol_name, sp->pos);
	}

	print_valid_module_relocs(sp->symbol_name);

	return false;
}

/*
 * Add this symbol to the converted_symbols list to cache its sympos and
 * for later renaming.
 */
static bool remember_sympos(struct symbol *s, struct sympos *sp)
{
	struct converted_sym *cs;

	cs = calloc(1, sizeof(*cs));
	if (!cs) {
		WARN("Unable to allocate converted_symbol entry");
		return false;
	}

	cs->symbol = s;
	cs->sympos = *sp;
	list_add(&cs->list, &converted_symbols);

	return true;
}

/* Returns the right sympos respective to a symbol to be relocated */
static bool find_sympos(struct symbol *s, struct sympos *sp)
{
	struct symbol_entry *entry;
	struct converted_sym *cs;

	/* did we already convert this symbol? */
	list_for_each_entry(cs, &converted_symbols, list) {
		if (cs->symbol == s) {
			*sp = cs->sympos;
			return true;
		}
	}

	/* did the user specified via annotation? */
	if (get_usr_sympos(s, sp)) {
		if (valid_sympos(sp)) {
			remember_sympos(s, sp);
			return true;
		}
		return false;
	}

	/* search symbol in symbols list */
	entry = find_sym_entry_by_name(s->name);
	if (entry) {
		sp->symbol_name = entry->symbol_name;
		sp->object_name = entry->object_name;
		sp->pos = 0;
		remember_sympos(s, sp);
		return true;
	}
	return false;
}

/*
 * Finds or creates a klp rela section based on another given section (@oldsec)
 * and sympos (@*sp), then returns it
 */
static struct section *get_or_create_klp_rela_section(struct section *oldsec,
		struct sympos *sp, struct elf *klp_elf)
{
	char *name;
	struct section *sec;
	unsigned int length;

	length = strlen(KLP_RELA_PREFIX) + strlen(sp->object_name)
		 + strlen(oldsec->base->name) + 2;

	name = calloc(1, length);
	if (!name) {
		WARN("Memory allocation failed (%s%s.%s)\n", KLP_RELA_PREFIX,
				sp->object_name, oldsec->base->name);
		return NULL;
	}

	if (snprintf(name, length, KLP_RELA_PREFIX "%s.%s", sp->object_name,
				oldsec->base->name) >= length) {
		WARN("Length error (%s)", name);
		free(name);
		return NULL;
	}

	sec = find_section_by_name(klp_elf, name);
	if (!sec)
		sec = create_rela_section(klp_elf, name, oldsec->base);

	if (sec)
		sec->sh.sh_flags |= SHF_RELA_LIVEPATCH;

	free(name);
	return sec;
}

/* Converts rela symbol names */
static bool convert_symbol(struct symbol *s, struct sympos *sp)
{
	char *name;
	char pos[4];	/* assume that pos will never be > 999 */
	unsigned int length;

	if (snprintf(pos, sizeof(pos), "%d", sp->pos) > sizeof(pos)) {
		WARN("Insufficient buffer for expanding sympos (%s.%s,%d)\n",
				sp->object_name, sp->symbol_name, sp->pos);
		return false;
	}

	length = strlen(KLP_SYM_PREFIX) + strlen(sp->object_name)
		 + strlen(sp->symbol_name) + sizeof(pos) + 3;

	name = calloc(1, length);
	if (!name) {
		WARN("Memory allocation failed (%s%s.%s,%s)\n", KLP_SYM_PREFIX,
				sp->object_name, sp->symbol_name, pos);
		return false;
	}

	if (snprintf(name, length, KLP_SYM_PREFIX "%s.%s,%s", sp->object_name,
				sp->symbol_name, pos) >= length) {

		WARN("Length error (%s%s.%s,%s)", KLP_SYM_PREFIX,
				sp->object_name, sp->symbol_name, pos);
		free(name);
		return false;
	}

	/*
	 * Despite the memory waste, we don't mind freeing the original symbol
	 * name memory chunk. Keeping it there is harmless and, since removing
	 * bytes from the string section is non-trivial, it is unworthy.
	 */
	s->name = name;
	s->sec = NULL;
	s->sym.st_name = -1;
	s->sym.st_shndx = SHN_LIVEPATCH;

	return true;
}

/* Checks if a rela was converted */
static bool is_converted_rela(struct rela *rela)
{
	return !!rela->klp_rela_sec;
}

/*
 * Convert rela that cannot be resolved by the classic module loader
 * to the special klp rela one.
 */
static bool convert_rela(struct section *oldsec, struct rela *rela,
		struct sympos *sp, struct elf *klp_elf)
{
	struct section *sec;

	sec = get_or_create_klp_rela_section(oldsec, sp, klp_elf);
	if (!sec) {
		WARN("Can't create or access klp.rela section (%s.%s)\n",
				sp->object_name, sp->symbol_name);
		return false;
	}

	rela->klp_rela_sec = sec;

	return true;
}

static void move_rela(struct rela *r)
{
	/* Move the converted rela to klp rela section */
	list_del(&r->list);
	list_add_tail(&r->list, &r->klp_rela_sec->relas);
}

/* Checks if given symbol name matches a symbol in exp_symbols */
static bool is_exported(char *sname)
{
	struct symbol_entry *e;

	/*
	 * exp_symbols itens are prefixed with __ksymtab_ - comparisons must
	 * skip prefix and check if both are properly null-terminated
	 */
	list_for_each_entry(e, &exp_symbols, list) {
		if (strcmp(e->symbol_name + 10, sname) == 0)
			return true;
	}
	return false;
}

/* Checks if symbol should be skipped */
static bool skip_symbol(struct symbol *sym)
{
	/* already resolved? */
	if (sym->sec)
		return true;

	/* skip symbol with index 0 */
	if (!sym->idx)
		return true;

	/* we should not touch .TOC. on ppc64le */
	if (strcmp(sym->name, ".TOC.") == 0)
		return true;

	if (is_exported(sym->name))
		return true;

	return false;
}

/* Checks if a section is a klp rela section */
static bool is_klp_rela_section(char *sname)
{
	int len = strlen(KLP_RELA_PREFIX);

	if (strncmp(sname, KLP_RELA_PREFIX, len) == 0)
		return true;
	return false;
}

/*
 * Frees the list, new names and rela sections as created by
 * remember_sympos(), convert_rela(), and convert_symbol()
 */
static void free_converted_resources(struct elf *klp_elf)
{
	struct converted_sym *cs, *cs_aux;
	struct section *sec;

	list_for_each_entry_safe(cs, cs_aux, &converted_symbols, list) {
		free(cs->symbol->name);
		free(cs);
	}

	list_for_each_entry(sec, &klp_elf->sections, list) {
		if (is_klp_rela_section(sec->name)) {
			free(sec->elf_data);
			free(sec->name);
		}
	}
}

/*
 * Checks if section may be skipped (conditions)
 */
static bool skip_section(struct section *sec)
{
	if (!is_rela_section(sec))
		return true;

	if (is_klp_rela_section(sec->name))
		return true;

	return false;
}

/*
 * Checks if rela conversion is supported in given section
 */
static bool supported_section(struct section *sec, char *object_name)
{
#if 0
	/*
	 * klp-relocations forbidden in sections that otherwise would
	 * match in allowed_prefixes[]
	 */
	static const char * const not_allowed[] = {
		".rela.data.rel.ro",
		".rela.data.rel.ro.local",
		".rela.data..ro_after_init",
		NULL
	};
#endif

	/* klp-relocations allowed in sections only for vmlinux */
	static const char * const allowed_vmlinux[] = {
		".rela__jump_table",
		NULL
	};

	/* klp-relocations allowed in sections with prefixes */
	static const char * const allowed_prefixes[] = {
		".rela.data",
		".rela.rodata",	// supported ???
		".rela.sdata",
		".rela.text",
		".rela.toc",
		NULL
	};

	const char * const *name;

#if 0
	for (name = not_allowed; *name; name++)
		if (strcmp(sec->name, *name) == 0)
			return false;
#endif

	if (strcmp(object_name, "vmlinux") == 0) {
		for (name = allowed_vmlinux; *name; name++)
			if (strcmp(sec->name, *name) == 0)
				return true;
	}

	for (name = allowed_prefixes; *name; name++)
		if (strncmp(sec->name, *name, strlen(*name)) == 0)
			return true;

	return false;
}

int main(int argc, const char **argv)
{
	const char *klp_in_module, *klp_out_module, *symbols_list;
	struct rela *rela, *tmprela;
	struct section *sec;
	struct sympos sp;
	struct elf *klp_elf;
	struct converted_sym *cs;
	int errors = 0;

	if (argc != 4) {
		WARN("Usage: %s <symbols.klp> <input.ko> <output.ko>", argv[0]);
		return -1;
	}

	symbols_list = argv[1];
	klp_in_module = argv[2];
	klp_out_module = argv[3];

	klp_elf = elf_open(klp_in_module);
	if (!klp_elf) {
		WARN("Unable to read elf file %s\n", klp_in_module);
		return -1;
	}

	if (!load_syms_lists(symbols_list))
		return -1;

	if (!load_usr_symbols(klp_elf)) {
		WARN("Unable to load user-provided sympos");
		return -1;
	}

	list_for_each_entry(sec, &klp_elf->sections, list) {
		if (skip_section(sec))
			continue;

		list_for_each_entry(rela, &sec->relas, list) {
			if (skip_symbol(rela->sym))
				continue;

			/* rela needs to be converted */

			if (!find_sympos(rela->sym, &sp)) {
				WARN("Unable to find missing symbol: %s",
						rela->sym->name);
				errors++;
				continue;
			}
			if (!supported_section(sec, sp.object_name)) {
				WARN("Conversion not supported for symbol: %s section: %s object: %s",
						rela->sym->name, sec->name,
						sp.object_name);
				errors++;
				continue;
			}
			if (!convert_rela(sec, rela, &sp, klp_elf)) {
				WARN("Unable to convert relocation: %s",
						rela->sym->name);
				return -1;
			}
		}

		if (errors)
			return -1;

		/* Now move all converted relas in list-safe manner */
		list_for_each_entry_safe(rela, tmprela, &sec->relas, list) {
			if (is_converted_rela(rela))
				move_rela(rela);
		}
	}

	/* Rename the converted symbols */
	list_for_each_entry(cs, &converted_symbols, list) {
		if (!convert_symbol(cs->symbol, &cs->sympos)) {
			WARN("Unable to convert symbol name (%s)\n",
					cs->symbol->name);
			return -1;
		}
	}

	free_syms_lists();
	if (elf_write_file(klp_elf, klp_out_module))
		return -1;

	free_converted_resources(klp_elf);
	elf_close(klp_elf);

	return 0;
}

/* Functions kept commented since they might be useful for future debugging */

/* Dumps sympos list (useful for debugging purposes)
 * static void dump_sympos(void)
 * {
 *	struct sympos *sp;
 *
 *	fprintf(stderr, "BEGIN OF SYMPOS DUMP\n");
 *	list_for_each_entry(sp, &usr_symbols, list) {
 *		fprintf(stderr, "%s %s %d\n", sp->symbol_name, sp->object_name,
 *				sp->pos);
 *	}
 *	fprintf(stderr, "END OF SYMPOS DUMP\n");
 * }
 *
 *
 * / Dump symbols list for debugging purposes /
 * static void dump_symbols(void)
 * {
 *	struct symbol_entry *entry;
 *
 *	fprintf(stderr, "BEGIN OF SYMBOLS DUMP\n");
 *	list_for_each_entry(entry, &symbols, list)
 *		printf("%s %s\n", entry->object_name, entry->symbol_name);
 *	fprintf(stderr, "END OF SYMBOLS DUMP\n");
 * }
 */
