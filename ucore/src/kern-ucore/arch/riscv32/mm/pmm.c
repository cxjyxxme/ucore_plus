#include <types.h>
#include <stdio.h>
#include <string.h>
#include <mmu.h>
#include <memlayout.h>
#include <pmm.h>
#include <buddy_pmm.h>
#include <sync.h>
#include <slab.h>
#include <error.h>
#include <proc.h>
#include <kio.h>
#include <mp.h>
#include <arch.h>
#include <swap.h>

/* *
 * Task State Segment:
 *
 * The TSS may reside anywhere in memory. A special segment register called
 * the Task Register (TR) holds a segment selector that points a valid TSS
 * segment descriptor which resides in the GDT. Therefore, to use a TSS
 * the following must be done in function gdt_init:
 *   - create a TSS descriptor entry in GDT
 *   - add enough information to the TSS in memory as needed
 *   - load the TR register with a segment selector for that segment
 *
 * There are several fileds in TSS for specifying the new stack pointer when a
 * privilege level change happens. But only the fields SS0 and ESP0 are useful
 * in our os kernel.
 *
 * The field SS0 contains the stack segment selector for CPL = 0, and the ESP0
 * contains the new ESP value for CPL = 0. When an interrupt happens in protected
 * mode, the x86 CPU will look in the TSS for SS0 and ESP0 and load their value
 * into SS and ESP respectively.
 * */

// physical address of boot-time page directory
uintptr_t boot_cr3;

/* *
 * The page directory entry corresponding to the virtual address range
 * [VPT, VPT + PTSIZE) points to the page directory itself. Thus, the page
 * directory is treated as a page table as well as a page directory.
 *
 * One result of treating the page directory as a page table is that all PTEs
 * can be accessed though a "virtual page table" at virtual address VPT. And the
 * PTE for number n is stored in vpt[n].
 *
 * A second consequence is that the contents of the current page directory will
 * always available at virtual address PGADDR(PDX(VPT), PDX(VPT), 0), to which
 * vpd is set bellow.
 * */
pte_t *const vpt = (pte_t *) VPT;
pde_t *const vpd = (pde_t *) PGADDR(PDX(VPT), (PDX(VPT)) + 1, 0);

static DEFINE_PERCPU_NOINIT(size_t, used_pages);
DEFINE_PERCPU_NOINIT(list_entry_t, page_struct_free_list);

// virtual address of physicall page array
struct Page *pages;
// amount of physical memory (in pages)
size_t npage = 0;
// The kernel image is mapped at VA=KERNBASE and PA=info.base
uint32_t va_pa_offset;
// memory starts at 0x80000000 in RISC-V
const size_t nbase = DRAM_BASE / PGSIZE;

// virtual address of boot-time page directory
pde_t *boot_pgdir = NULL;

// physical memory management
const struct pmm_manager *pmm_manager;

/**************************************************
 * Memory manager wrappers.
 **************************************************/

/**
 * Initialize mm tragedy.
 *     Just put it here to make it be static still.
 */
static void init_pmm_manager(void)
{
	pmm_manager = &buddy_pmm_manager;
	kprintf("memory managment: %s\n", pmm_manager->name);
	pmm_manager->init();
}

//init_memmap - call pmm->init_memmap to build Page struct for free memory  
static void init_memmap(struct Page *base, size_t n)
{
	pmm_manager->init_memmap(base, n);
}

static void check_alloc_page(void)
{
	pmm_manager->check();
	kprintf("check_alloc_page() succeeded!\n");
}

/**
 * set_pgdir - save the physical address of the current pgdir
 */
void set_pgdir(struct proc_struct *proc, pde_t * pgdir)
{
	assert(proc != NULL);
	proc->cr3 = PADDR(pgdir);
}

/**
 * map_pgdir - map the current pgdir @pgdir to its own address space
 */
void map_pgdir(pde_t * pgdir)
{
    ppn_t ppn = page2ppn(kva2page(pgdir));
    pgdir[PDX(VPT)] = pte_create(ppn, PTE_V);
    pgdir[PDX(VPT) + 1] = pte_create(ppn, PTE_V | PTE_R | PTE_W);
}

/* pmm_init - initialize the physical memory management */
static void page_init(void)
{
    extern char kern_entry[];

    va_pa_offset = KERNBASE - (uint32_t)kern_entry;

    uint32_t mem_begin = (uint32_t)kern_entry;
    uint32_t mem_end = (128 << 20) + DRAM_BASE; // 128MB memory on qemu
    uint32_t mem_size = mem_end - mem_begin;

    kprintf("physcial memory map:\n");
    kprintf("  memory: 0x%08lx, [0x%08lx, 0x%08lx].\n", mem_size, mem_begin,
            mem_end - 1);

    uint64_t maxpa = mem_end;

    if (maxpa > KERNTOP) {
        maxpa = KERNTOP;
    }

    extern char end[];

    npage = maxpa / PGSIZE;
    // BBL has put the initial page table at the first available page after the
    // kernel
    // so stay away from it by adding extra offset to end
    pages = (struct Page *)ROUNDUP((void *)end, PGSIZE);

    for (size_t i = 0; i < npage - nbase; i++) {
        SetPageReserved(pages + i);
    }

    uintptr_t freemem = PADDR((uintptr_t)pages + sizeof(struct Page) * (npage - nbase));

    mem_begin = ROUNDUP(freemem, PGSIZE);
    mem_end = ROUNDDOWN(mem_end, PGSIZE);
    if (freemem < mem_end) {
        init_memmap(pa2page(mem_begin), (mem_end - mem_begin) / PGSIZE);
    }
}

static void enable_paging(void)
{
	// set page table
	lcr3(boot_cr3);
}

size_t nr_used_pages(void)
{
	return get_cpu_var(used_pages);
}

/**
 * nr_free_pages - call pmm->nr_free_pages to get the size (nr*PAGESIZE) of current free memory
 * @return number of free pages
 */
size_t nr_free_pages(void)
{
	size_t ret;
	bool intr_flag;
	local_intr_save(intr_flag);
	{
		ret = pmm_manager->nr_free_pages();
	}
	local_intr_restore(intr_flag);
	return ret;
}

/**
 * boot_map_segment - setup&enable the paging mechanism
 * @param la    linear address of this memory need to map (after x86 segment map)
 * @param size  memory size
 * @param pa    physical address of this memory
 * @param perm  permission of this memory
 */
void
boot_map_segment(pde_t * pgdir, uintptr_t la, size_t size, uintptr_t pa,
		 uint32_t perm)
{
	assert(PGOFF(la) == PGOFF(pa));
	size_t n = ROUNDUP(size + PGOFF(la), PGSIZE) / PGSIZE;
	la = ROUNDDOWN(la, PGSIZE);
	pa = ROUNDDOWN(pa, PGSIZE);
	for (; n > 0; n--, la += PGSIZE, pa += PGSIZE) {
		pte_t *ptep = get_pte(pgdir, la, 1);
		assert(ptep != NULL);
		*ptep = pte_create(pa >> PGSHIFT, PTE_V | perm);
	}
}

//pmm_init - setup a pmm to manage physical memory, build PDT&PT to setup paging mechanism 
//         - check the correctness of pmm & paging mechanism, print PDT&PT
void pmm_init(void)
{
	//We need to alloc/free the physical memory (granularity is 4KB or other size). 
	//So a framework of physical memory manager (struct pmm_manager)is defined in pmm.h
	//First we should init a physical memory manager(pmm) based on the framework.
	//Then pmm can alloc/free the physical memory. 
	//Now the first_fit/best_fit/worst_fit/buddy_system pmm are available.
	init_pmm_manager();

	// detect physical memory space, reserve already used memory,
	// then use pmm->init_memmap to create free page list
	page_init();

	//use pmm->check to verify the correctness of the alloc/free function in a pmm
	check_alloc_page();

	// create boot_pgdir, an initial page directory(Page Directory Table, PDT)
	boot_pgdir = boot_alloc_page();
	memset(boot_pgdir, 0, PGSIZE);
	boot_cr3 = PADDR(boot_pgdir);

	check_pgdir();

	static_assert(KERNBASE % PTSIZE == 0 && KERNTOP % PTSIZE == 0);

	// recursively insert boot_pgdir in itself
	// to form a virtual page table at virtual address VPT
	boot_pgdir[PDX(VPT)] = pte_create(PPN(boot_cr3), PTE_V);
	boot_pgdir[PDX(VPT) + 1] = pte_create(PPN(boot_cr3), PTE_V | PTE_R | PTE_W);

	// map all physical memory to linear memory with base linear addr KERNBASE
	//linear_addr KERNBASE~KERNBASE+KMEMSIZE = phy_addr 0~KMEMSIZE
	//But shouldn't use this map until enable_paging() & gdt_init() finished.
	boot_map_segment(boot_pgdir, KERNBASE, KMEMSIZE, PADDR(KERNBASE), PTE_V | PTE_R | PTE_W | PTE_X);

	enable_paging();

	//now the basic virtual memory map(see memalyout.h) is established.
	//check the correctness of the basic virtual memory map.
	check_boot_pgdir();

	print_pgdir(kprintf);

	slab_init();
}

void pmm_init_ap(void)
{
	list_entry_t *page_struct_free_list =
	    get_cpu_ptr(page_struct_free_list);
	list_init(page_struct_free_list);
	get_cpu_var(used_pages) = 0;
}

/**
 * call pmm->alloc_pages to allocate a continuing n*PAGESIZE memory
 * @param n pages to be allocated
 */
struct Page *alloc_pages(size_t n)
{
	struct Page *page;
	bool intr_flag;
#ifdef UCONFIG_SWAP
try_again:
#endif
	local_intr_save(intr_flag);
	{
		page = pmm_manager->alloc_pages(n);
	}
	local_intr_restore(intr_flag);
#ifdef UCONFIG_SWAP
	if (page == NULL && try_free_pages(n)) {
		goto try_again;
	}
#endif

	get_cpu_var(used_pages) += n;
	return page;
}

/**
 * boot_alloc_page - allocate one page using pmm->alloc_pages(1) 
 * @return the kernel virtual address of this allocated page
 * note: this function is used to get the memory for PDT(Page Directory Table)&PT(Page Table)
 */
void *boot_alloc_page(void)
{
	struct Page *p = alloc_page();
	if (p == NULL) {
		panic("boot_alloc_page failed.\n");
	}
	return page2kva(p);
}

/**
 * free_pages - call pmm->free_pages to free a continuing n*PAGESIZE memory
 * @param base the first page to be freed
 * @param n number of pages to be freed
 */
void free_pages(struct Page *base, size_t n)
{
	bool intr_flag;
	local_intr_save(intr_flag);
	{
		pmm_manager->free_pages(base, n);
	}
	local_intr_restore(intr_flag);
	get_cpu_var(used_pages) -= n;
}

// invalidate a TLB entry, but only if the page tables being
// edited are the ones currently in use by the processor.
void tlb_update(pde_t * pgdir, uintptr_t la)
{
	tlb_invalidate(pgdir, la);
}

void tlb_invalidate(pde_t * pgdir, uintptr_t la)
{
	asm volatile("sfence.vm %0" : : "r"(la));
}

void check_boot_pgdir(void)
{
	pte_t *ptep;
	int i;
	for (i = ROUNDDOWN(KERNBASE, PGSIZE); i < npage * PGSIZE; i += PGSIZE) {
		assert((ptep = get_pte(boot_pgdir, (uintptr_t) KADDR(i), 0)) != NULL);
		assert(PTE_ADDR(*ptep) == i);
	}

	assert(PDE_ADDR(boot_pgdir[PDX(VPT)]) == PADDR(boot_pgdir));

	assert(boot_pgdir[0] == 0);

	struct Page *p;
	p = alloc_page();
	assert(page_insert(boot_pgdir, p, 0x100, PTE_R | PTE_W) == 0);
	assert(page_ref(p) == 1);
	assert(page_insert(boot_pgdir, p, 0x100 + PGSIZE, PTE_R | PTE_W) == 0);
	assert(page_ref(p) == 2);

	const char *str = "ucore: Hello world!!";
	strcpy((void *)0x100, str);
	assert(strcmp((void *)0x100, (void *)(0x100 + PGSIZE)) == 0);

	*(char *)(page2kva(p) + 0x100) = '\0';
	assert(strlen((const char *)0x100) == 0);

	free_page(p);
	free_page(pde2page(boot_pgdir[0]));
	boot_pgdir[0] = 0;

	kprintf("check_boot_pgdir() succeeded!\n");
}

/*
 * Check page table
 */
void check_pgdir(void)
{
    // assert(npage <= KMEMSIZE / PGSIZE);
    // The memory starts at 2GB in RISC-V
    // so npage is always larger than KMEMSIZE / PGSIZE
	assert(npage <= KERNTOP / PGSIZE);
	assert(boot_pgdir != NULL && (uint32_t) PGOFF(boot_pgdir) == 0);
	assert(get_page(boot_pgdir, TEST_PAGE, NULL) == NULL);

	struct Page *p1, *p2;
	p1 = alloc_page();
	assert(page_insert(boot_pgdir, p1, TEST_PAGE, 0) == 0);

	pte_t *ptep, perm;
	assert((ptep = get_pte(boot_pgdir, TEST_PAGE, 0)) != NULL);
	assert(pte2page(*ptep) == p1);
	assert(page_ref(p1) == 1);

	ptep = &((pte_t *) KADDR(PTE_ADDR(boot_pgdir[PDX(TEST_PAGE)])))[1];
	assert(get_pte(boot_pgdir, TEST_PAGE + PGSIZE, 0) == ptep);

	p2 = alloc_page();
	ptep_unmap(&perm);
	ptep_set_u_read(&perm);
	ptep_set_u_write(&perm);
	assert(page_insert(boot_pgdir, p2, TEST_PAGE + PGSIZE, perm) == 0);
	assert((ptep = get_pte(boot_pgdir, TEST_PAGE + PGSIZE, 0)) != NULL);
	assert(ptep_u_read(ptep));
	assert(ptep_u_write(ptep));
	assert(ptep_user(&(boot_pgdir[PDX(TEST_PAGE)])));
	assert(page_ref(p2) == 1);

	assert(page_insert(boot_pgdir, p1, TEST_PAGE + PGSIZE, 0) == 0);
	assert(page_ref(p1) == 2);
	assert(page_ref(p2) == 0);
	assert((ptep = get_pte(boot_pgdir, TEST_PAGE + PGSIZE, 0)) != NULL);
	assert(pte2page(*ptep) == p1);
	assert(!ptep_user(ptep));

	page_remove(boot_pgdir, TEST_PAGE);
	assert(page_ref(p1) == 1);
	assert(page_ref(p2) == 0);

	page_remove(boot_pgdir, TEST_PAGE + PGSIZE);
	assert(page_ref(p1) == 0);
	assert(page_ref(p2) == 0);

	assert(page_ref(pde2page(boot_pgdir[PDX(TEST_PAGE)])) == 1);
	free_page(pde2page(boot_pgdir[PDX(TEST_PAGE)]));
	boot_pgdir[PDX(TEST_PAGE)] = 0;
	exit_range(boot_pgdir, TEST_PAGE, TEST_PAGE + PGSIZE);

	kprintf("check_pgdir() succeeded.\n");
}

//perm2str - use string 'u,r,w,-' to present the permission
static const char *perm2str(int perm)
{
	static char str[4];
	str[0] = (perm & PTE_U) ? 'u' : '-';
	str[1] = 'r';
	str[2] = (perm & PTE_W) ? 'w' : '-';
	str[3] = '\0';
	return str;
}

//get_pgtable_items - In [left, right] range of PDT or PT, find a continuing linear addr space
//                  - (left_store*X_SIZE~right_store*X_SIZE) for PDT or PT
//                  - X_SIZE=PTSIZE=4M, if PDT; X_SIZE=PGSIZE=4K, if PT
// paramemters:
//  left:        no use ???
//  right:       the high side of table's range
//  start:       the low side of table's range
//  table:       the beginning addr of table
//  left_store:  the pointer of the high side of table's next range
//  right_store: the pointer of the low side of table's next range
// return value: 0 - not a invalid item range, perm - a valid item range with perm permission 
static int
get_pgtable_items(size_t left, size_t right, size_t start, uintptr_t * table,
		  size_t * left_store, size_t * right_store)
{
	if (start >= right) {
		return 0;
	}
	while (start < right && !(table[start] & PTE_V)) {
		start++;
	}
	if (start < right) {
		if (left_store != NULL) {
			*left_store = start;
		}
		int perm = (table[start++] & PTE_USER);
		while (start < right && (table[start] & PTE_USER) == perm) {
			start++;
		}
		if (right_store != NULL) {
			*right_store = start;
		}
		return perm;
	}
	return 0;
}

//print_pgdir - print the PDT&PT
void print_pgdir(int (*printf) (const char *fmt, ...))
{
    kprintf("-------------------- BEGIN --------------------\n");
    size_t left, right = 0, perm;
    while ((perm = get_pgtable_items(0, NPDEENTRY, right, vpd, &left, &right)) != 0)
    {
        kprintf("PDE(%03x) %08x-%08x %08x %s\n", right - left, left * PTSIZE,
                right * PTSIZE, (right - left) * PTSIZE, perm2str(perm));

        if ((perm & (PTE_V | PTE_R | PTE_W | PTE_X)) != PTE_V) continue;

        size_t l, r = left * NPTEENTRY;
        uintptr_t i;
        size_t old_l, old_r, old_perm = 0;
        for (i = left; i < right; i++) {
            while (1) {
                perm = get_pgtable_items(i * NPTEENTRY, (i + 1) * NPTEENTRY, r,
                    (uintptr_t *)(KADDR((uintptr_t)PDE_ADDR(vpd[i]))) - i * NPTEENTRY, &l, &r);

                if (perm == 0) break;

                if (old_perm != perm) {
                    if (old_perm != 0) {
                        kprintf("  |-- PTE(%05x) %08x-%08x %08x %s\n", old_r - old_l,
                                old_l * PGSIZE, old_r * PGSIZE, (old_r - old_l) * PGSIZE, perm2str(old_perm));
                    }
                    old_l = l;
                    old_r = r;
                    old_perm = perm;
                } else {
                    old_r = r;
                }
            }
        }
        if (old_perm != 0) {
            kprintf("  |-- PTE(%05x) %08x-%08x %08x %s\n", old_r - old_l, old_l * PGSIZE,
                    old_r * PGSIZE, (old_r - old_l) * PGSIZE, perm2str(old_perm));
        }
    }
    kprintf("--------------------- END ---------------------\n");
}
