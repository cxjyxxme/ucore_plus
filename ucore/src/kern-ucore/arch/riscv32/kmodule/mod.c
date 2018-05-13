#include <elf.h>
#include <mod.h>
#include <kio.h>

int apply_relocate(struct secthdr *sechdrs,
		   const char *strtab,
		   unsigned int symindex,
		   unsigned int relsec, struct module *mod)
{
	unsigned int i;
	struct reloc_a_s *rel = (void *)sechdrs[relsec].sh_addr;
	struct symtab_s *sym;
	uint32_t *location;
	uint32_t value;

	kprintf("Applying relocate section %u to %u\n", relsec,
		sechdrs[relsec].sh_info);
	for (i = 0; i < sechdrs[relsec].sh_size / sizeof(*rel); i++) {
		/* This is where to make the change */
		location = (void *)sechdrs[sechdrs[relsec].sh_info].sh_addr
		    + rel[i].r_offset;
		/* This is the symbol it is referring to.  Note that all
		   undefined symbols have been resolved.  */
		sym = (struct symtab_s *)sechdrs[symindex].sh_addr
		    + GET_RELOC_SYM(rel[i].r_info);
        value = sym->st_value + rel[i].r_addend;
		switch (GET_RELOC_TYPE(rel[i].r_info)) {
		case R_RISCV_NONE:
			break;
		case R_RISCV_32:
			/* We add the value into the location given */
			*location = value;
			break;
		default:
			kprintf
			    ("apply_relocate: module %s: Unknown relocation: %u\n",
			     mod->name, GET_RELOC_TYPE(rel[i].r_info));
			return -1;
		}
	}
	return 0;
}

/* Apply the given add relocation to the (simplified) ELF.  Return
	-error or 0 */
int apply_relocate_add(struct secthdr *sechdrs,
		       const char *strtab,
		       unsigned int symindex,
		       unsigned int relsec, struct module *mod)
{
	kprintf("apply_relocate_add: module %s: ADD RELOCATION unsupported\n",
		mod->name);
	return -1;
}
