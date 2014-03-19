#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <link.h>
#include <string.h>
#include <wchar.h>
#include "libcrunch_private.h"

#define MAPPING_IN_USE(m) ((m)->begin && (m)->end)
struct mapping
{
	void *begin;
	void *end;
	struct prefix_tree_node n;
};

static struct mapping *get_mapping_from_node(struct prefix_tree_node *n)
{
	/* HACK: FIXME: please get rid of this stupid node-based interface. */
	return (struct mapping *)((char*) n - offsetof(struct mapping, n));
}

/* How many mappings? 256 is a bit stingy. 
 * Each mapping is 48--64 bytes, so 4096 of them would take 256KB.
 * Maybe stick to 1024? */
#define NMAPPINGS 1024
struct mapping mappings[NMAPPINGS]; // NOTE: we *don't* use mappings[0]; the 0 byte means "empty"

#define PAGE_SIZE 4096
#define LOG_PAGE_SIZE 12

#define SANITY_CHECK_MAPPING(m) \
	do { \
		if (MAPPING_IN_USE((m))) { \
			for (unsigned long i = pagenum((m)->begin); i < pagenum((m)->end); ++i) { \
				assert(l0index[i] == ((m) - &mappings[0])); \
			} \
			assert(l0index[pagenum((m)->begin)-1] != ((m) - &mappings[0])); \
			assert(l0index[pagenum((m)->end)] != ((m) - &mappings[0])); \
		} \
	} while (0)
	
mapping_num_t *l0index __attribute__((visibility("protected")));

static void memset_mapping(mapping_num_t *begin, mapping_num_t num, size_t n)
{
	assert(1ull<<(8*sizeof(mapping_num_t)) >= NMAPPINGS - 1);
	assert(sizeof (wchar_t) == 2 * sizeof (mapping_num_t));

	/* We use wmemset with special cases at the beginning and end */
	if (n > 0 && (uintptr_t) begin % sizeof (wchar_t) != 0)
	{
		*begin++ = num;
		--n;
	}
	assert(n == 0 || (uintptr_t) begin % sizeof (wchar_t) == 0);
	
	// double up the value
	wchar_t wchar_val = ((wchar_t) num) << (8 * sizeof(mapping_num_t)) | num;
	
	// do the memset
	if (n != 0) wmemset((wchar_t *) begin, wchar_val, n / 2);
	
	// if we missed one off the end, do it now
	if (n % 2 == 1)
	{
		*(begin + (n-1)) = num;
	}
}

static void (__attribute__((constructor)) init)(void)
{
	/* Mmap our region. We map one byte for every page. */
	assert(sysconf(_SC_PAGE_SIZE) == PAGE_SIZE);
	l0index = MEMTABLE_NEW_WITH_TYPE(mapping_num_t, PAGE_SIZE, (void*) 0, (void*) STACK_BEGIN);
	assert(l0index != MAP_FAILED);
}

static uintptr_t pagenum(const void *p)
{
	return ((uintptr_t) p) >> LOG_PAGE_SIZE;
}

static const void *addr_of_pagenum(uintptr_t pagenum)
{
	return (const void *) (pagenum << LOG_PAGE_SIZE);
}

_Bool insert_equal(struct insert *p_ins1, struct insert *p_ins2)
{
	return p_ins1->alloc_site_flag == p_ins2->alloc_site_flag &&
		p_ins1->alloc_site == p_ins2->alloc_site;
		// don't compare prev/next, at least not for now
}
_Bool node_info_equal(struct node_info *p_info1, struct node_info *p_info2)
{
	return p_info1->what == p_info2->what && 
	(p_info1->what == DATA_PTR ? p_info1->un.data_ptr == p_info2->un.data_ptr
	            : (assert(p_info1->what == INS_AND_BITS), 
					(insert_equal(&p_info1->un.ins_and_bits.ins, &p_info2->un.ins_and_bits.ins)
						&& p_info1->un.ins_and_bits.npages == p_info2->un.ins_and_bits.npages
						&& p_info1->un.ins_and_bits.obj_offset == p_info2->un.ins_and_bits.obj_offset)
					)
	);
}

static struct mapping *find_free_mapping(void)
{
	for (struct mapping *p = &mappings[1]; p < &mappings[NMAPPINGS]; ++p)
	{
		SANITY_CHECK_MAPPING(p);
		
		if (!MAPPING_IN_USE(p))
		{
			return p;
		}
	}
	assert(0);
}

static _Bool
is_unindexed(void *begin, void *end)
{
	mapping_num_t *pos = &l0index[pagenum(begin)];
	while (pos < l0index + pagenum(end) && !*pos) { ++pos; }
	
	if (pos == l0index + pagenum(end)) return 1;
	
	debug_printf(3, "Found already-indexed position %p (mapping %d)\n", 
			addr_of_pagenum(pos - l0index), *pos);
	return 0;
}

static _Bool
is_unindexed_or_heap(void *begin, void *end)
{
	mapping_num_t *pos = &l0index[pagenum(begin)];
	while (pos < l0index + pagenum(end) && (!*pos || mappings[*pos].n.kind == HEAP)) { ++pos; }
	
	if (pos == l0index + pagenum(end)) return 1;
	
	debug_printf(3, "Found already-indexed non-heap position %p (mapping %d)\n", 
			addr_of_pagenum(pos - l0index), *pos);
	return 0;
}

static _Bool range_overlaps_mapping(struct mapping *m, void *base, size_t s)
{
	return (char*) base < (char*) m->end && (char*) base + s > (char*) m->begin;
}

#define SANITY_CHECK_NEW_MAPPING(base, s) \
	/* We have to tolerate overlaps in the case of anonymous mappings, because */ \
	/* they come and go without our direct oversight. */ \
	do { \
		for (unsigned i = 1; i < NMAPPINGS; ++i) { \
			assert(mappings[i].n.kind == HEAP || \
				!range_overlaps_mapping(&mappings[i], (base), (s))); \
		} \
	} while (0)
#define STRICT_SANITY_CHECK_NEW_MAPPING(base, s) \
	/* Don't tolerate overlaps! */ \
	do { \
		for (unsigned i = 1; i < NMAPPINGS; ++i) { \
			assert(!range_overlaps_mapping(&mappings[i], (base), (s))); \
		} \
	} while (0)

#define MAXPTR(a, b) \
	((((char*)(a)) > ((char*)(b))) ? (a) : (b))

#define MINPTR(a, b) \
	((((char*)(a)) < ((char*)(b))) ? (a) : (b))

static
struct mapping *create_or_extend_mapping(void *base, size_t s, unsigned kind, struct node_info *p_info)
{
	assert((uintptr_t) base % sysconf(_SC_PAGE_SIZE) == 0); // else something strange is happening
	assert(s % sysconf(_SC_PAGE_SIZE) == 0); // else something strange is happening

	
	debug_printf(3, "%s: creating mapping base %p, size %lu, kind %u, info %p\n", 
		__func__, base, (unsigned long) s, kind, p_info);
	
	/* In the case of heap regions, libc can munmap them without our seeing it. 
	 * So, we might be surprised to find that our index is out-of-date here. 
	 * We force a deletion if so. */
	mapping_num_t mapping_num = l0index[pagenum(base)];
	SANITY_CHECK_MAPPING(&mappings[mapping_num]);
	
	if (mapping_num != 0 && mappings[mapping_num].n.kind == 3)
	{
		// force an unmapping of this region
		debug_printf(3, "%s: forcing unmapping of %p-%p, the first part of which is mapped "
				"at number %d\n", __func__, base, (char*) base + s, mapping_num);
		prefix_tree_del(base, s);
	}
	
	// test for nearby mappings to extend
	mapping_num_t abuts_existing_start = l0index[pagenum((char*) base + s)];
	mapping_num_t abuts_existing_end = l0index[pagenum((char*) base - 1)];
	
	/* Tolerate overlapping either of these two, if we're mapping heap (anonymous). 
	 * We simply adjust our base and size so that we fit exactly. 
	 */
	if (kind == HEAP)
	{
		SANITY_CHECK_NEW_MAPPING(base, s);
		// adjust w.r.t. abutments
		if (abuts_existing_start 
			&& range_overlaps_mapping(&mappings[abuts_existing_start], base, s)
			&& mappings[abuts_existing_start].n.kind == HEAP)
		{
			s = (char*) mappings[abuts_existing_start].begin - (char*) base;
		}
		if (abuts_existing_end
			&& range_overlaps_mapping(&mappings[abuts_existing_end], base, s)
			&& mappings[abuts_existing_start].n.kind == HEAP)
		{
			base = mappings[abuts_existing_end].end;
		}
		
		// also adjust w.r.t. overlaps
		mapping_num_t our_end_overlaps = l0index[pagenum((char*) base + s) - 1];
		mapping_num_t our_begin_overlaps = l0index[pagenum((char*) base)];

		if (our_end_overlaps
			&& range_overlaps_mapping(&mappings[our_end_overlaps], base, s)
			&& mappings[our_end_overlaps].n.kind == HEAP)
		{
			// move our end earlier, but not to earlier than base
			void *cur_end = (char *) base + s;
			void *new_end = MAXPTR(base, mappings[our_end_overlaps].begin);
			s = (char*) new_end - (char*) base;
		}
		if (our_begin_overlaps
			&& range_overlaps_mapping(&mappings[our_begin_overlaps], base, s)
			&& mappings[our_begin_overlaps].n.kind == HEAP)
		{
			// move our begin later, but not to later than base + s
			void *new_begin = MINPTR(mappings[our_begin_overlaps].begin, (char*) base + s); 
			ptrdiff_t length_reduction = (char*) new_begin - (char*) base;
			assert(length_reduction >= 0);
			base = new_begin;
			s -= length_reduction;
		}		
		
		STRICT_SANITY_CHECK_NEW_MAPPING(base, s);
	}
	else if (kind == STACK)
	{
		/* Tolerate sharing an upper boundary with an existing mapping. */
		mapping_num_t our_end_overlaps = l0index[pagenum((char*) base + s) - 1];
		
		if (our_end_overlaps)
		{
			_Bool contracting = base > mappings[our_end_overlaps].begin;
			struct mapping *m = &mappings[our_end_overlaps];
			
			if (contracting)
			{
				// simply update the lower bound, do the memset, sanity check and exit
				void *old_begin = m->begin;
				m->begin = base;
				assert(m->end == (char*) base + s);
				memset_mapping(l0index + pagenum(old_begin), 0, 
							((char*) old_begin - (char*) base) >> LOG_PAGE_SIZE);
				SANITY_CHECK_MAPPING(m);
				return m;
			}
			else // expanding or zero-growth
			{
				// simply update the lower bound, do the memset, sanity check and exit
				void *old_begin = m->begin;
				m->begin = base;
				assert(m->end == (char*) base + s);
				if (old_begin != base)
				{
					memset_mapping(l0index + pagenum(base), our_end_overlaps, 
								((char*) old_begin - (char*) base) >> LOG_PAGE_SIZE);
				}
				SANITY_CHECK_MAPPING(m);
				return m;
			}
		}
		
		// neither expanding nor contracting, so we look for strictly correct
		STRICT_SANITY_CHECK_NEW_MAPPING(base, s);
	}
	assert(is_unindexed_or_heap(base, (char*) base + s));
	
	debug_printf(3, "node info is %p\n", p_info);
	
	debug_printf(3, "%s: abuts_existing_start: %d, abuts_existing_end: %d\n",
			__func__, abuts_existing_start, abuts_existing_end);

	_Bool kind_matches = 1;
	_Bool node_matches = 1;
	
	_Bool can_coalesce_after = abuts_existing_start
				&& (kind_matches = (mappings[abuts_existing_start].n.kind == kind))
				&& (node_matches = (node_info_equal(&mappings[abuts_existing_start].n.info, p_info)));
	_Bool can_coalesce_before = abuts_existing_end
				&& (kind_matches = (mappings[abuts_existing_end].n.kind == kind))
				&& (node_matches = (node_info_equal(&mappings[abuts_existing_end].n.info, p_info)));
	debug_printf(3, "%s: can_coalesce_after: %s, can_coalesce_before: %s, "
			"kind_matches: %s, node_matches: %s \n",
			__func__, can_coalesce_after ? "true" : "false", can_coalesce_before ? "true" : "false", 
			kind_matches ? "true": "false", node_matches ? "true": "false" );
	
	/* If we *both* abut a start and an end, we're coalescing 
	 * three mappings. If so, just bump up our base and s, 
	 * free the spare mapping and coalesce before. */
	if (__builtin_expect(can_coalesce_before && can_coalesce_after, 0))
	{
		s += (char*) mappings[abuts_existing_start].end - (char*) mappings[abuts_existing_start].begin;
		mappings[abuts_existing_start].begin = 
			mappings[abuts_existing_start].end =
				NULL;
		debug_printf(3, "%s: bumped up size to join two mappings\n", __func__);
		can_coalesce_after = 0;
	}
	
	if (can_coalesce_before)
	{
		debug_printf(3, "%s: post-extending existing mapping ending at %p\n", __func__,
				mappings[abuts_existing_end].end);
		memset_mapping(l0index + pagenum(mappings[abuts_existing_end].end), abuts_existing_end, 
			s >> LOG_PAGE_SIZE);
		mappings[abuts_existing_end].end = (char*) base + s;
		SANITY_CHECK_MAPPING(&mappings[abuts_existing_end]);
		return &mappings[abuts_existing_end];
	}
	if (can_coalesce_after)
	{
		debug_printf(3, "%s: pre-extending existing mapping at %p-%p\n", __func__,
				mappings[abuts_existing_start].begin, mappings[abuts_existing_start].end);
		mappings[abuts_existing_start].begin = (char*) base;
		memset_mapping(l0index + pagenum(base), abuts_existing_start, s >> LOG_PAGE_SIZE);
		SANITY_CHECK_MAPPING(&mappings[abuts_existing_start]);
		return &mappings[abuts_existing_start];
	}
	
	debug_printf(3, "%s: forced to assign new mapping\n", __func__);
	
	// else create new
	struct mapping *found = find_free_mapping();
	if (found)
	{
		found->begin = base;
		found->end = (char*) base + s;
		found->n.kind = kind;
		found->n.info = *p_info;
		memset_mapping(l0index + pagenum(base), (mapping_num_t) (found - &mappings[0]), s >> LOG_PAGE_SIZE);
		SANITY_CHECK_MAPPING(found);
		return found;
	}
	
	return NULL;
}

struct prefix_tree_node *prefix_tree_add(void *base, size_t s, unsigned kind, const void *data_ptr)
{
	if (!l0index) init();
	
	assert(!data_ptr || kind == STACK || 
			0 == strcmp(data_ptr, realpath_quick(data_ptr)));
	
	struct node_info info = { .what = DATA_PTR, .un = { data_ptr: data_ptr } };
	return prefix_tree_add_full(base, s, kind, &info);
}

void prefix_tree_add_sloppy(void *base, size_t s, unsigned kind, const void *data_ptr)
{
	if (!l0index) init();

	/* What's the biggest mapping you can think of? 
	 * 
	 * We don't want to index our memtables. I'm going to be conservative
	 * and avoid indexing anything above 4GB. */
	if (__builtin_expect(s >= BIGGEST_MAPPING, 0))
	{
		debug_printf(3, "Warning: not indexing huge mapping (size %lu) at %p\n", (unsigned long) s, base);
		return;
	}
	if (__builtin_expect((uintptr_t) base + s > STACK_BEGIN, 0))
	{
		debug_printf(3, "Warning: not indexing high-in-VAS mapping (size %lu) at %p\n", (unsigned long) s, base);
		return;
	}
	
	struct node_info info = { .what = DATA_PTR, .un = { data_ptr: data_ptr } };
	
	/* Just add the as-yet-unmapped bits of the range. */
	uintptr_t begin_pagenum = pagenum(base);
	uintptr_t current_pagenum = begin_pagenum;
	uintptr_t end_pagenum = pagenum((char*) base + s);
	while (current_pagenum < end_pagenum)
	{
		uintptr_t next_indexed_pagenum = current_pagenum;
		while (next_indexed_pagenum < end_pagenum && !l0index[next_indexed_pagenum])
		{ ++next_indexed_pagenum; }
		
		if (next_indexed_pagenum > current_pagenum)
		{
			prefix_tree_add_full((void*) addr_of_pagenum(current_pagenum), 
				(char*) addr_of_pagenum(next_indexed_pagenum)
					 - (char*) addr_of_pagenum(current_pagenum), 
				kind, &info);
		}
		
		current_pagenum = next_indexed_pagenum;
		// skip over any indexed bits so we're pointing at the next unindexed bit
		while (l0index[current_pagenum] && ++current_pagenum < end_pagenum);
	}
}

int 
prefix_tree_node_exact_match(struct prefix_tree_node *n, void *begin, void *end)
{
	struct mapping *m = get_mapping_from_node(n);
	return m->begin == begin && m->end == end;
}

struct prefix_tree_node *prefix_tree_add_full(void *base, size_t s, unsigned kind, struct node_info *p_arg)
{
	if (!l0index) init();

	assert((uintptr_t) base % PAGE_SIZE == 0);
	assert(s % PAGE_SIZE == 0);
	
	/* What's the biggest mapping you can think of? 
	 * 
	 * We don't want to index our memtables. I'm going to be conservative
	 * and avoid indexing anything above 4GB. */
	if (s >= BIGGEST_MAPPING)
	{
		debug_printf(3, "Warning: not indexing huge mapping (size %lu) at %p\n", (unsigned long) s, base);
		return NULL;
	}
	if (__builtin_expect((uintptr_t) base + s > STACK_BEGIN, 0))
	{
		debug_printf(3, "Warning: not indexing high-in-VAS mapping (size %lu) at %p\n", (unsigned long) s, base);
		return NULL;
	}
	
	uintptr_t first_page_num = (uintptr_t) base >> LOG_PAGE_SIZE;
	uintptr_t npages = s >> LOG_PAGE_SIZE;

	struct mapping *m = create_or_extend_mapping(base, s, kind, p_arg);
	SANITY_CHECK_MAPPING(m);
	return &m->n;
}

static struct mapping *split_mapping(struct mapping *m, void *split_addr)
{
	assert(m);
	assert((char*) split_addr > (char*) m->begin);
	assert((char*) split_addr < (char*) m->end);
	
	// make a new entry for the remaining-after part, then just chop before
	struct mapping *new_m = find_free_mapping();
	assert(new_m);
	new_m->begin = split_addr;
	new_m->end = m->end;
	assert((char*) new_m->end > (char*) new_m->begin);
	new_m->n = m->n;

	// rewrite uses of the old mapping number in the new-mapping portion of the memtable
	mapping_num_t new_mapping_num = new_m - &mappings[0];
	unsigned long npages
	 = ((char*) new_m->end - ((char*) new_m->begin)) >> LOG_PAGE_SIZE;
	memset_mapping(l0index + pagenum((char*) new_m->begin), new_mapping_num, npages);

	// delete (from m) the part now covered by new_m
	m->end = new_m->begin;
	
	SANITY_CHECK_MAPPING(m);
	SANITY_CHECK_MAPPING(new_m);
	
	return new_m;
}

void prefix_tree_del_node(struct prefix_tree_node *n)
{
	/* HACK: FIXME: please get rid of this stupid node-based interface. */
	struct mapping *m = get_mapping_from_node(n);
	
	// check sanity
	assert(l0index[pagenum(m->begin)] == m - &mappings[0]);
	
	prefix_tree_del(m->begin, (char*) m->end - (char*) m->begin);
}

void prefix_tree_del(void *base, size_t s)
{
	if (!l0index) init();
	
	assert(s % PAGE_SIZE == 0);
	assert((uintptr_t) base % PAGE_SIZE == 0);

	if (s >= BIGGEST_MAPPING)
	{
		debug_printf(3, "Warning: not unindexing huge mapping (size %lu) at %p\n", (unsigned long) s, base);
		return;
	}
	
	unsigned long cur_pagenum = pagenum(base); 
	unsigned long end_pagenum = pagenum((char*)base + s);
	mapping_num_t mapping_num;
	// if we get mapping num 0 at first, try again after forcing init_prefix_tree_from_maps()
	do
	{
		/* We might span multiple mappings, because munmap() is like that. */
		mapping_num = l0index[pagenum(base)];
	}
	while (mapping_num == 0 && (!initialized_maps ? (init_prefix_tree_from_maps(), 1) : 0));

	do
	{
		mapping_num = l0index[cur_pagenum];
		assert(mapping_num != 0);
		struct mapping *m = &mappings[mapping_num];
		SANITY_CHECK_MAPPING(m);
		size_t this_mapping_size = (char*) m->end - (char*) m->begin;
		
		/* Do we need to chop an entry? */
		_Bool remaining_before = m->begin < base;
		_Bool remaining_after
		 = (char*) m->end > (char*) base + s;

		void *next_addr = NULL;
		/* If we're chopping before and after, we need to grab a *new* 
		 * mapping number. */
		if (__builtin_expect(remaining_before && remaining_after, 0))
		{
			struct mapping *new_m = split_mapping(m, (char*) base + s);

			// we might still need to chop before, but not after
			remaining_after = 0;

			assert((uintptr_t) new_m->begin % PAGE_SIZE == 0);
			assert((uintptr_t) new_m->end % PAGE_SIZE == 0);
		}

		if (__builtin_expect(remaining_before, 0))
		{
			// means the to-be-unmapped range starts *after* the start of the current mapping
			char *this_unmapping_begin = (char*) base;
			assert((char*) m->end <= ((char*) base + s)); // we should have dealt with the other case above
			char *this_unmapping_end = //((char*) m->end > ((char*) base + s))
					//? ((char*) base + s)
					/*:*/ m->end;
			assert(this_unmapping_end > this_unmapping_begin);
			unsigned long npages = (this_unmapping_end - this_unmapping_begin)>>LOG_PAGE_SIZE;
			// zero out the to-be-unmapped part of the memtable
			memset_mapping(l0index + pagenum(this_unmapping_begin), 0, npages);
			// this mapping now ends at the unmapped base addr
			next_addr = m->end;
			m->end = base;
			SANITY_CHECK_MAPPING(m);
		}
		else if (__builtin_expect(remaining_after, 0))
		{
			// means the to-be-unmapped range ends *before* the end of the current mapping
			void *new_begin = (char*) base + s;
			assert((char*) new_begin > (char*) m->begin);
			unsigned long npages
			 = ((char*) new_begin - (char*) m->begin) >> LOG_PAGE_SIZE;
			memset_mapping(l0index + pagenum(m->begin), 0, npages);
			m->begin = new_begin;
			next_addr = new_begin; // should terminate us
			SANITY_CHECK_MAPPING(m);
		}
		else 
		{
			// else we're just deleting the whole entry
			memset_mapping(l0index + pagenum(m->begin), 0, 
					pagenum((char*) m->begin + this_mapping_size)
					 - pagenum(m->begin));
			next_addr = m->end;
			m->begin = m->end = NULL;
			SANITY_CHECK_MAPPING(m);
		}
		
		assert((uintptr_t) m->begin % PAGE_SIZE == 0);
		assert((uintptr_t) m->end % PAGE_SIZE == 0);
		
		/* How far have we got?  */
		assert(next_addr);
		cur_pagenum = pagenum(next_addr);
		
		// FIXME: if we see some zero mapping nums, skip forward
		if (__builtin_expect(cur_pagenum < end_pagenum && l0index[cur_pagenum] == 0, 0))
		{
			while (cur_pagenum < end_pagenum)
			{
				if (l0index[cur_pagenum]) break;
				++cur_pagenum;
			}
			debug_printf(3, "Warning: l0-unindexing a partially unmapped region %p-%p\n",
				next_addr, addr_of_pagenum(cur_pagenum));
		}
	} while (cur_pagenum < end_pagenum);
	
	assert(is_unindexed(base, (char*) base + s));
}

enum object_memory_kind prefix_tree_get_memory_kind(const void *obj)
{
	if (!l0index) init();
	
	mapping_num_t mapping_num = l0index[pagenum(obj)];
	if (mapping_num == 0) return UNKNOWN;
	else return mappings[mapping_num].n.kind;
}

void prefix_tree_print_all_to_stderr(void)
{
	if (!l0index) init();
	for (struct mapping *m = &mappings[1]; m < &mappings[NMAPPINGS]; ++m)
	{
		if (MAPPING_IN_USE(m)) fprintf(stderr, "%p-%p %01d %s %s %p\n", 
				m->begin, m->end, m->n.kind, name_for_memory_kind(m->n.kind), 
				m->n.info.what == DATA_PTR ? "(data ptr) " : "(insert + bits) ", 
				m->n.info.what == DATA_PTR ? m->n.info.un.data_ptr : (void*)(uintptr_t) m->n.info.un.ins_and_bits.ins.alloc_site);
	}
}
struct prefix_tree_node *
prefix_tree_deepest_match_from_root(void *base, struct prefix_tree_node ***out_prev_ptr)
{
	if (!l0index) init();
	if (out_prev_ptr) *out_prev_ptr = NULL;
	mapping_num_t mapping_num = l0index[pagenum(base)];
	if (mapping_num == 0) return NULL;
	else return &mappings[mapping_num].n;
}

size_t
prefix_tree_get_overlapping_mappings(struct prefix_tree_node **out_begin, 
		size_t out_size, void *begin, void *end)
{
	struct prefix_tree_node **out = out_begin;
	uintptr_t end_pagenum = pagenum(end);
	uintptr_t begin_pagenum = pagenum(begin);
	while (out - out_begin < out_size)
	{
		// look for the next mapping that overlaps: skip unmapped bits
		while (begin_pagenum < end_pagenum && !l0index[begin_pagenum])
		{ ++begin_pagenum; }
		
		if (begin_pagenum >= end_pagenum) break; // normal termination case
		
		mapping_num_t num = l0index[begin_pagenum];
		*out++ = &mappings[num].n;
		
		// advance begin_pagenum to one past the end of this mapping
		begin_pagenum = pagenum(mappings[num].end);
	}
	return out - out_begin;
}

struct prefix_tree_node *
prefix_tree_bounds(const void *ptr, const void **out_begin, const void **out_end)
{
	if (!l0index) init();
	mapping_num_t mapping_num = l0index[pagenum(ptr)];
	if (mapping_num == 0) return NULL;
	else 
	{
		if (out_begin) *out_begin = mappings[mapping_num].begin;
		if (out_end) *out_end = mappings[mapping_num].end;
		return &mappings[mapping_num].n;
	}
}


void *__try_index_l0(const void *ptr, size_t modified_size, const void *caller)
{
	/* We get called from heap_index when the malloc'd address is a multiple of the 
	 * page size. Check whether it fills (more-or-less) the alloc'd region, and if so,  
	 * install its trailer into the maps. We will fish it out in get_alloc_info. */
	
	__libcrunch_check_init();
	
	assert(page_size);

	char *chunk_end = (char*) ptr + malloc_usable_size((void*) ptr);
	
	if ((uintptr_t) ptr % page_size <= MAXIMUM_MALLOC_HEADER_OVERHEAD
			&& (uintptr_t) chunk_end % PAGE_SIZE == 0
			&& (uintptr_t) ptr - ROUND_DOWN_TO_PAGE_SIZE((uintptr_t) ptr) <= MAXIMUM_MALLOC_HEADER_OVERHEAD)
	{
		// ensure we have this in the maps
		enum object_memory_kind k1 = prefix_tree_get_memory_kind(ptr);
		enum object_memory_kind k2 = prefix_tree_get_memory_kind((char*) ptr + modified_size);
		if (k1 == UNKNOWN || k2 == UNKNOWN) 
		{
			prefix_tree_add_missing_maps();
			assert(prefix_tree_get_memory_kind(ptr) != UNKNOWN);
			assert(prefix_tree_get_memory_kind((char*) ptr + modified_size) != UNKNOWN);
		}
		
		/* Collect a contiguous sequence of so-far-without-insert mappings, 
		 * starting from ptr. */
		const void *lowest_bound = NULL;
		mapping_num_t num;
		unsigned nmappings = 0;
		_Bool saw_fit = 0;
		
		mapping_num_t cur_num;
		for (cur_num = l0index[pagenum(ptr)]; 
				cur_num != 0 && mappings[cur_num].n.info.what == DATA_PTR; 
				cur_num = l0index[pagenum(mappings[cur_num].end)])
		{
			struct mapping *m = &mappings[cur_num];
			SANITY_CHECK_MAPPING(m);
			
			// on our first run, remember the lowest ptr
			if (!lowest_bound)
			{
				// if we have an early part of the first mapping in the way, split it
				if ((char*) m->begin < (char*) ROUND_DOWN_TO_PAGE_SIZE((uintptr_t) ptr))
				{
					m = split_mapping(m, (void*) ROUND_DOWN_TO_PAGE_SIZE((uintptr_t) ptr));
					cur_num = m - &mappings[0];
				}
				lowest_bound = m->begin;
			}

			++nmappings;

			if ((char*) m->end >= chunk_end)
			{
				// we've successfully reached an end point
				saw_fit = 1;
				
				// if we leave a later part of the mapping remaining, split off
				if ((char*) m->end > chunk_end)
				{
					SANITY_CHECK_MAPPING(m);
					split_mapping(m, chunk_end);
					SANITY_CHECK_MAPPING(m);
				}
				
				break;
			}
		}
		
		if (saw_fit)
		{
			/* We think we've got a mmap()'d region. 
			 * Grab the bottom region in the sequence, 
			 * delete the others, 
			 * then create_or_extend the bottom one to the required length.
			 */
			mapping_num_t last_num = cur_num;
			assert(caller);
			assert(lowest_bound);
			uintptr_t npages = ((uintptr_t) chunk_end - (uintptr_t) lowest_bound) >> log_page_size;
			uintptr_t bottom_pagenum = pagenum(lowest_bound);
			mapping_num_t mapping_num = l0index[bottom_pagenum];
			assert(mapping_num != 0);
			struct mapping *m = &mappings[mapping_num];
			SANITY_CHECK_MAPPING(m);
			
			assert(mappings[last_num].end == chunk_end);
			
			assert(m->n.info.what == DATA_PTR);
			assert(m->n.kind == HEAP);
			m->n.info = (struct node_info) {
				.what = INS_AND_BITS, 
				.un = {
					ins_and_bits: { 
						.ins = (struct insert) {
							.alloc_site_flag = 0,
							.alloc_site = (uintptr_t) caller
						},
						.is_object_start = 1, 
						.npages = npages, 
						.obj_offset = (char*) ptr - (char*) lowest_bound
					}
				}
			};
			
			// delete the other mappings, then extend over them
			if ((char*) m->end < chunk_end) 
			{
				size_t s = chunk_end - (char*) m->end;
				prefix_tree_del(m->end, s);
				debug_printf(3, "node_info is %p\n,",&m->n.info ); 
				debug_printf(3, "We want to extend our bottom mapping number %d (%p-%p) "
					"to include %ld bytes from %p\n", 
					m - &mappings[0], m->begin, m->end, s, m->end); 
				assert(l0index[pagenum((char*) m->end - 1)] == m - &mappings[0]);
				SANITY_CHECK_MAPPING(m);
				struct mapping *new_m = create_or_extend_mapping(
						m->end, s, m->n.kind, &m->n.info);
				SANITY_CHECK_MAPPING(new_m);
				assert(new_m == m);
			}

			return &m->n.info.un.ins_and_bits.ins;
		}
		else
		{
			debug_printf(3, "Warning: could not l0-index pointer %p, size %lu "
				"in mapping range %p-%p (%lu bytes, %u mappings)\n,", ptr, modified_size, 
				lowest_bound, mappings[cur_num].end, 
				(char*) mappings[cur_num].end - (char*) lowest_bound, nmappings);
		}
	}
	else
	{
		debug_printf(3, "Warning: could not l0-index pointer %p, size %lu: doesn't end "
			"on page boundary\n", ptr, modified_size);
	}

	return NULL;
}

static unsigned unindex_l0_one_mapping(struct prefix_tree_node *n, const void *lower, const void *upper)
{
	n->info.what = 0;
	return (char*) upper - (char*) lower;
}

unsigned __unindex_l0(const void *mem)
{
	const void *lower;
	const void *upper;
	struct prefix_tree_node *n = prefix_tree_bounds(mem, &lower, &upper);
	unsigned lower_to_upper_npages = ((uintptr_t) upper - (uintptr_t) lower) >> log_page_size;
	assert(n);

	/* We want to unindex the same number of pages we indexed. */
	unsigned npages_to_unindex = n->info.un.ins_and_bits.npages;
	unsigned total_size_to_unindex = npages_to_unindex << log_page_size;

	unsigned total_size_unindexed = lower_to_upper_npages << log_page_size;
	do
	{
		total_size_unindexed += unindex_l0_one_mapping(n, lower, upper);
		if (total_size_unindexed < total_size_to_unindex)
		{
			// advance to the next mapping
			n = prefix_tree_bounds(upper, &lower, &upper);
		}
	} while (total_size_unindexed < total_size_to_unindex);
	
	return total_size_unindexed;
}

struct insert *__lookup_l0(const void *mem, void **out_object_start)
{
	struct prefix_tree_node *n = prefix_tree_deepest_match_from_root((void*) mem, NULL);
	if (n && n->info.what)
	{
		// 1. we have to search backwards for the start of the mmapped region
		const void *cur = mem;
		const void *lower, *upper;
		// walk backwards through contiguous mappings, til we find one with the high bit set
		do
		{
			n = prefix_tree_bounds(cur, &lower, &upper);
			cur = n ? (const char*) lower - 1  : cur;
		} while (n && (assert(n->info.what), !n->info.un.ins_and_bits.is_object_start));
		
		// if n is null, it means we ran out of mappings before we saw the high bit
		assert(n);
		
		*out_object_start = (char*) lower + n->info.un.ins_and_bits.obj_offset;
		return &n->info.un.ins_and_bits.ins;
	}
	else return NULL;
}
