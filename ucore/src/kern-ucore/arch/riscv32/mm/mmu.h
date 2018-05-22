#ifndef __KERN_MM_MMU_H__
#define __KERN_MM_MMU_H__

#ifndef __ASSEMBLER__
#include <types.h>
#endif /* !__ASSEMBLER__ */

// A linear address 'la' has a three-part structure as follows:
//
// +--------10------+-------10-------+---------12----------+
// | Page Directory |   Page Table   | Offset within Page  |
// |      Index     |     Index      |                     |
// +----------------+----------------+---------------------+
//  \--- PDX(la) --/ \--- PTX(la) --/ \---- PGOFF(la) ----/
//  \----------- PPN(la) -----------/
//
// The PDX, PTX, PGOFF, and PPN macros decompose linear addresses as shown.
// To construct a linear address la from PDX(la), PTX(la), and PGOFF(la),
// use PGADDR(PDX(la), PTX(la), PGOFF(la)).

// RISC-V uses 32-bit virtual address to access 34-bit physical address!
// Sv32 page table entry:
// +---------12----------+--------10-------+---2----+-------8-------+
// |       PPN[1]        |      PPN[0]     |Reserved|D|A|G|U|X|W|R|V|
// +---------12----------+-----------------+--------+---------------+

/*
 * RV32Sv32 page table entry:
 * | 31 10 | 9             7 | 6 | 5 | 4  1 | 0
 *    PFN    reserved for SW   D   R   TYPE   V
 *
 * RV64Sv39 / RV64Sv48 page table entry:
 * | 63           48 | 47 10 | 9             7 | 6 | 5 | 4  1 | 0
 *   reserved for HW    PFN    reserved for SW   D   R   TYPE   V
 */


#define PTXSHIFT        12	// offset of PTX in a linear address
#define PDXSHIFT        22	// offset of PDX in a linear address
#define PMXSHIFT		PDXSHIFT
#define PUXSHIFT		PDXSHIFT
#define PGXSHIFT		PDXSHIFT
#define PTE_PPN_SHIFT   10  // offset of PPN in a physical address

// page directory index
#define PDX(la) ((((uintptr_t)(la)) >> PDXSHIFT) & 0x3FF)
#define PGX(la) PDX(la)
#define PUX(la) PDX(la)
#define PMX(la) PDX(la)

// page table index
#define PTX(la) ((((uintptr_t)(la)) >> PTXSHIFT) & 0x3FF)

// page number field of address
#define PPN(la) (((uintptr_t)(la)) >> PTXSHIFT)

// offset in page
#define PGOFF(la) (((uintptr_t)(la)) & 0xFFF)

// construct linear address from indexes and offset
#define PGADDR(d, t, o) ((uintptr_t)((d) << PDXSHIFT | (t) << PTXSHIFT | (o)))

// address in page table or page directory entry
#define PTE_ADDR(pte)   (((uintptr_t)(pte) & ~0x3FF) << (PTXSHIFT - PTE_PPN_SHIFT))
#define PDE_ADDR(pde)   PTE_ADDR(pde)
#define PMD_ADDR(pmd)   PTE_ADDR(pmd)
#define PUD_ADDR(pud)   PTE_ADDR(pud)
#define PGD_ADDR(pgd)   PTE_ADDR(pgd)

/* page directory and page table constants */
#define NPDEENTRY       1024	// page directory entries per page directory
#define NPTEENTRY       1024	// page table entries per page table

#define PGSIZE          4096	// bytes mapped by a page
#define PGSHIFT         12	// log2(PGSIZE)
#define PTSIZE          (PGSIZE * NPTEENTRY)	// bytes mapped by a page directory entry
#define PMSIZE			PTSIZE
#define PUSIZE			PTSIZE
#define PTSHIFT         22	// log2(PTSIZE)

#define SECTSIZE        512	// bytes of a sector
#define PAGE_NSECT      (PGSIZE / SECTSIZE)	// sectors per page

// page table entry (PTE) fields
#define PTE_V     0x001 // Valid
#define PTE_R     0x002 // Read
#define PTE_W     0x004 // Write
#define PTE_X     0x008 // Execute
#define PTE_U     0x010 // User
#define PTE_G     0x020 // Global
#define PTE_A     0x040 // Accessed
#define PTE_D     0x080 // Dirty
#define PTE_SOFT  0x300 // Reserved for Software

#define PTE_SWAP        (PTE_A | PTE_D)
#define PTE_USER        (PTE_R | PTE_W | PTE_X | PTE_U | PTE_V)

#ifndef __ASSEMBLER__

typedef uintptr_t pte_t;
typedef uintptr_t pde_t;
typedef uintptr_t pud_t;
typedef uintptr_t pmd_t;
typedef uintptr_t pgd_t;
typedef pte_t swap_entry_t;
typedef pte_t pte_perm_t;

static inline void ptep_map(pte_t * ptep, uintptr_t pa)
{
	*ptep = (pa >> (PGSHIFT - PTE_PPN_SHIFT) | PTE_V);
}

static inline void ptep_unmap(pte_t * ptep)
{
	*ptep = 0;
}

static inline int ptep_invalid(pte_t * ptep)
{
	return (*ptep == 0);
}

static inline int ptep_present(pte_t * ptep)
{
	return (*ptep & PTE_V);
}

static inline int ptep_user(pte_t * ptep)
{
	return (*ptep & PTE_U);
}

static inline int ptep_s_read(pte_t * ptep)
{
	return (*ptep & PTE_R);
}

static inline int ptep_s_write(pte_t * ptep)
{
	return (*ptep & PTE_W);
}

static inline int ptep_u_read(pte_t * ptep)
{
	return ((*ptep & PTE_U) && (*ptep & PTE_R));
}

static inline int ptep_u_write(pte_t * ptep)
{
	return ((*ptep & PTE_U) && (*ptep & PTE_W));
}

static inline void ptep_set_user(pte_t * ptep)
{
	*ptep |= PTE_U;
}

static inline void ptep_set_exe(pte_t * ptep)
{
	*ptep |= PTE_X;
}

static inline void ptep_set_s_read(pte_t * ptep)
{
	*ptep |= PTE_R;
}

static inline void ptep_set_s_write(pte_t * ptep)
{
	*ptep |= PTE_R | PTE_W;
}

static inline void ptep_set_u_read(pte_t * ptep)
{
	*ptep |= PTE_R | PTE_U;
}

static inline void ptep_set_u_write(pte_t * ptep)
{
	*ptep |= PTE_R | PTE_W | PTE_U;
}

static inline void ptep_unset_s_read(pte_t * ptep)
{
	*ptep &= (~(PTE_R | PTE_W));
}

static inline void ptep_unset_s_write(pte_t * ptep)
{
	*ptep &= (~PTE_W);
}

static inline void ptep_unset_u_read(pte_t * ptep)
{
	*ptep &= (~(PTE_R | PTE_W | PTE_U));
}

static inline void ptep_unset_u_write(pte_t * ptep)
{
	*ptep &= (~(PTE_W | PTE_U));
}

static inline pte_perm_t ptep_get_perm(pte_t * ptep, pte_perm_t perm)
{
	return (*ptep) & perm;
}

static inline void ptep_set_perm(pte_t * ptep, pte_perm_t perm)
{
	*ptep |= perm;
}

static inline void ptep_copy(pte_t * to, pte_t * from)
{
	*to = *from;
}

static inline void ptep_unset_perm(pte_t * ptep, pte_perm_t perm)
{
	*ptep &= (~perm);
}

static inline int ptep_accessed(pte_t * ptep)
{
	return *ptep & PTE_A;
}

static inline int ptep_dirty(pte_t * ptep)
{
	return *ptep & PTE_D;
}

static inline void ptep_set_accessed(pte_t * ptep)
{
	*ptep |= PTE_A;
}

static inline void ptep_set_dirty(pte_t * ptep)
{
	*ptep |= PTE_D;
}

static inline void ptep_unset_accessed(pte_t * ptep)
{
	*ptep &= (~PTE_A);
}

static inline void ptep_unset_dirty(pte_t * ptep)
{
	*ptep &= (~PTE_D);
}

#endif

#endif /* !__KERN_MM_MMU_H__ */
