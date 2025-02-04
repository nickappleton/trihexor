/* boilerplate for this was taken from one of the imgui examples */
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <stdio.h>
#if defined(IMGUI_IMPL_OPENGL_ES2)
#include <GLES2/gl2.h>
#endif
#include <GLFW/glfw3.h>
#include <math.h>

#define MAKE_BIT_MASK(nb_bit_) ((1 << (nb_bit_)) - 1)
#define MAKE_UTREE(name_, keytype_, rootbits_, nodebits_, datatype_) \
struct name_ ## _node { \
	datatype_              data; \
	keytype_               key; \
	struct name_ ## _node *ap_ch[1 << (nodebits_)]; \
}; \
struct name_ { \
	struct name_ ## _node *ap_roots[1 << (rootbits_)]; \
}; \
void name_ ## _init(struct name_ *p_tree) { \
	size_t i; \
	for (i = 0; i < (((size_t)1) << (rootbits_)); i++) { \
		p_tree->ap_roots[i] = NULL; \
	} \
} \
datatype_ *name_ ## _node_get_data(struct name_ ## _node *p_node) { \
	return &(p_node->data); \
} \
datatype_ *name_ ## _node_optional_get_data(struct name_ ## _node *p_node) { \
	return (p_node == NULL) ? NULL : &(p_node->data); \
} \
struct name_ ## _node *name_ ## _find_insert_cb(struct name_ *p_tree, keytype_ key, struct name_ ## _node *(*p_createnode)(keytype_ key, void *p_userdata), void *p_userdata_or_node) { \
	struct name_ ## _node **pp_node = &(p_tree->ap_roots[key & MAKE_BIT_MASK(rootbits_)]); \
	struct name_ ## _node *p_node = *pp_node; \
	keytype_ keyiter = key >> (rootbits_); \
	while (p_node != NULL) { \
		if (p_node->key == key) \
			return p_node; \
		pp_node   = &(p_node->ap_ch[keyiter & MAKE_BIT_MASK(nodebits_)]); \
		p_node    = *pp_node; \
		keyiter >>= (nodebits_); \
	} \
	p_node = (p_createnode != NULL) ? p_createnode(key, p_userdata_or_node) : (struct name_ ## _node *)p_userdata_or_node; \
	if (p_node != NULL) { \
		size_t i; \
		*pp_node = p_node; \
		p_node->key = key; \
		for (i = 0; i < (((size_t)1) << (nodebits_)); i++) { \
			p_node->ap_ch[i] = NULL; \
		} \
	} \
	return p_node; \
} \
struct name_ ## _node *name_ ## _find_insert(struct name_ *p_tree, keytype_ key, struct name_ ## _node *p_node) { \
	return name_ ## _find_insert_cb(p_tree, key, NULL, p_node); \
} \
struct name_ ## _node *name_ ## _remove(struct name_ *p_tree, keytype_ key) { \
	struct name_ ## _node **pp_node = &(p_tree->ap_roots[key & MAKE_BIT_MASK(rootbits_)]); \
	struct name_ ## _node *p_node = *pp_node; \
	keytype_ keyiter = key >> (rootbits_); \
	while (p_node != NULL) { \
		if (p_node->key == key) \
			break; \
		pp_node   = &(p_node->ap_ch[keyiter & MAKE_BIT_MASK(nodebits_)]); \
		p_node    = *pp_node; \
		keyiter >>= (nodebits_); \
	} \
	if (p_node != NULL) { \
		while (1 /* while not a leaf node ... */) { \
			struct name_ ## _node *p_ch; \
			size_t kidx, i; \
			for (kidx = 0; kidx < (((size_t)1) << (nodebits_)); kidx++) { \
				size_t chk = (keyiter + kidx) & MAKE_BIT_MASK(nodebits_); \
				if (p_node->ap_ch[chk] != NULL) { \
					kidx = chk; \
					break; \
				} \
			} \
			if (kidx >= (((size_t)1) << (nodebits_))) \
				break; \
			p_ch = p_node->ap_ch[kidx]; \
			for (i = 0; i < (((size_t)1) << (nodebits_)); i++) { \
				struct name_ ## _node *p_tmp; \
				p_tmp             = p_node->ap_ch[i]; \
				p_node->ap_ch[i] = p_ch->ap_ch[i]; \
				p_ch->ap_ch[i]   = p_tmp; \
			} \
			*pp_node            = p_ch; \
			pp_node             = &(p_ch->ap_ch[kidx]); \
			p_ch->ap_ch[kidx]   = p_node; \
			keyiter           >>= (nodebits_); \
		} \
		*pp_node = NULL; \
	} \
	return p_node; \
} \
struct name_ ## _enumerator_stack_item { \
	struct name_ ## _node **pp_list; \
	int                     next_list_item; \
}; \
struct name_ ## _enumerator { \
	struct name_ ## _enumerator_stack_item a_stack[64]; \
	int                                    stack_pos; \
}; \
void name_ ## _enumerator_next(struct name_ ## _enumerator *p_enumerator) { \
	while (1) { \
		struct name_ ## _enumerator_stack_item *p_top = &(p_enumerator->a_stack[p_enumerator->stack_pos]); \
		if (p_top->next_list_item-- == 0) { \
			p_enumerator->stack_pos--; \
			return; \
		} \
		if (p_top->pp_list[p_top->next_list_item] != NULL) { \
			p_enumerator->stack_pos++; \
			p_enumerator->a_stack[p_enumerator->stack_pos].pp_list        = p_top->pp_list[p_top->next_list_item]->ap_ch; \
			p_enumerator->a_stack[p_enumerator->stack_pos].next_list_item = ((int)1) << (nodebits_); \
			continue; \
		} \
	} \
} \
struct name_ ## _node *name_ ## _enumerator_peek(struct name_ ## _enumerator *p_enumerator) { \
	if (p_enumerator->stack_pos >= 0) { \
		struct name_ ## _enumerator_stack_item *p_top = &(p_enumerator->a_stack[p_enumerator->stack_pos]); \
		return p_top->pp_list[p_top->next_list_item]; \
	} \
	return NULL; \
} \
struct name_ ## _node *name_ ## _enumerator_get(struct name_ ## _enumerator *p_enumerator) { \
	struct name_ ## _node *p_ret = NULL; \
	if (p_enumerator->stack_pos >= 0) { \
		struct name_ ## _enumerator_stack_item *p_top = &(p_enumerator->a_stack[p_enumerator->stack_pos]); \
		p_ret = p_top->pp_list[p_top->next_list_item]; \
		name_ ## _enumerator_next(p_enumerator); \
	} \
	return p_ret; \
} \
void name_ ## _enumerator_init(struct name_ ## _enumerator *p_enumerator, struct name_ *p_tree) { \
	p_enumerator->a_stack[0].pp_list        = p_tree->ap_roots; \
	p_enumerator->a_stack[0].next_list_item = ((int)1) << (rootbits_); \
	p_enumerator->stack_pos                 = 0; \
	name_ ## _enumerator_next(p_enumerator); \
}






static void glfw_error_callback(int error, const char* description)
{
	fprintf(stderr, "Glfw Error %d: %s\n", error, description);
}

#define NUM_LAYERS           (3)
#define PAGE_XY_BITS         (4)
#define PAGE_XY_NB           (1 << PAGE_XY_BITS)
#define PAGE_XY_MASK         (PAGE_XY_NB - 1)
#define PAGE_CELLS_PER_LAYER (PAGE_XY_NB*PAGE_XY_NB)
#define PAGE_CELL_COUNT      (PAGE_CELLS_PER_LAYER*NUM_LAYERS)

struct gridaddr {
	/* 31 bits. */
	uint32_t y;

	/* 31 bits. */
	uint32_t x;

	/* 2 bits - only values 0-2 used */
	uint32_t z;

};

#define URANGE_CHECK(a, max_inclusive) (assert((a) <= (max_inclusive)), (a))

/* Convert a grid-address to a page address and return the cell index within the page. */
static uint32_t gridaddr_split(struct gridaddr *p_page_addr, const struct gridaddr *p_addr) {
	p_page_addr->x = (URANGE_CHECK(p_addr->x, 0x7FFFFFFF) & ~(uint32_t)PAGE_XY_MASK);
	p_page_addr->y = (URANGE_CHECK(p_addr->y, 0x7FFFFFFF) & ~(uint32_t)PAGE_XY_MASK);
	p_page_addr->z = 0;
	return
		(p_addr->x & PAGE_XY_MASK) |
		((p_addr->y & PAGE_XY_MASK) << PAGE_XY_BITS) |
		(URANGE_CHECK(p_addr->z, 2) << (2*PAGE_XY_BITS));
}

static int gridaddr_is_page_addr(const struct gridaddr *p_addr) {
	return
		 (URANGE_CHECK(p_addr->x, 0x7FFFFFFF) & (uint32_t)PAGE_XY_MASK) == 0 &&
		 (URANGE_CHECK(p_addr->y, 0x7FFFFFFF) & (uint32_t)PAGE_XY_MASK) == 0 &&
		 URANGE_CHECK(p_addr->z, 2) == 0;
}

static int gridaddr_add_check(struct gridaddr *p_dest, const struct gridaddr *p_src, int32_t xoffset, int32_t yoffset) {
	uint32_t x = URANGE_CHECK(p_src->x, 0x7FFFFFFF) + (uint32_t)xoffset;
	uint32_t y = URANGE_CHECK(p_src->y, 0x7FFFFFFF) + (uint32_t)yoffset;
	if ((x & 0x80000000) || (y & 0x80000000))
		return 1;
	p_dest->x = x;
	p_dest->y = y;
	p_dest->z = p_src->z;
	return 0;
}

#define EDGE_DIR_N   (0)
#define EDGE_DIR_NE  (1)
#define EDGE_DIR_SE  (2)
#define EDGE_DIR_NW  (3)
#define EDGE_DIR_SW  (4)
#define EDGE_DIR_S   (5)
#define EDGE_DIR_NUM (6)
static int get_opposing_edge_id(int dir) {
	assert(dir < EDGE_DIR_NUM);
	return 5 - dir;
}

static const char *AP_LAYER_NAMES[NUM_LAYERS] = {"Red", "Green", "Blue"};

#define EDGE_LAYER_CONNECTION_UNCONNECTED             (0) /* 000 */
#define EDGE_LAYER_CONNECTION_SENDS                   (1) /* 001 */
#define EDGE_LAYER_CONNECTION_RECEIVES_DELAY_INVERTED (2) /* 010 */
#define EDGE_LAYER_CONNECTION_RECEIVES_INVERTED       (3) /* 011 */
#define EDGE_LAYER_CONNECTION_NET_CONNECTED           (4) /* 100 */

#define GRIDCELL_PROGRAM_NET_ID_BITS            (0xFFFFFF0000000000ull)
#define GRIDCELL_PROGRAM_NET_ID_BITS_SET(x_)    (((uint64_t)(x_)) << 40)
#define GRIDCELL_PROGRAM_NET_ID_BITS_GET(x_)    ((x_) >> 40)

/* The following bits get set by program_compile() if a failure occurs. They
 * can be used to work out if the cell is responsible for an error
 * condition. */
#define GRIDCELL_PROGRAM_BROKEN_BITS            (0x000000FC00000000ull)
#define GRIDCELL_PROGRAM_BROKEN_BIT_MASK(edge_) (((uint64_t)0x400000000) << (edge_))
#define GRIDCELL_PROGRAM_BUSY_BIT               (0x0000000200000000ull) /* Indicates that the cell itself is part of a dependency loop */
#define GRIDCELL_PROGRAM_MULTI_NAME_BIT         (0x0000000100000000ull) /* Indicates that the net which this cell is part of has been named in multiple locations and this is one of the cells */
#define GRIDCELL_PROGRAM_NO_MERGED_BIT          (0x0000000080000000ull)
#define GRIDCELL_PROGRAM_DUPLICATE_NAME_BIT     (0x0000000040000000ull)
#define GRIDCELL_NET_LABEL_BIT                  (0x0000000020000000ull)
#define GRIDCELL_BIT_MERGED_LAYERS              (0x0000000010000000ull)
#define GRIDCELL_CELL_EDGE_BITS_MASK            (0x000000000FFFFC00ull) /* 18-bits */
#define GRIDCELL_CELL_EDGE_BITS_GET(x_, edge_id_)         ((((uint64_t)(x_)) >> (10 + 3*(edge_id_))) & 0x7)
#define GRIDCELL_CELL_EDGE_BITS_SET(edge_id_, edge_type_) (((uint64_t)(edge_type_)) << (10 + 3*(edge_id_)))

/*
 * 63       55       47       39       31       23       15       7
 * PPPPPPPP PPPPPPPP PPPPPPPP BBBBBB12 34NMEEEE EEEEEEEE EEEEEEAA AAAAAAAA
 * 
 * A = 10 bits of address into gridpage
 * E = 18 bits of edge properties
 * N = 1 GRIDCELL_NET_LABEL_BIT bit
 * M = 1 GRIDCELL_BIT_MERGED_LAYERS bit
 * 4 = 1 GRIDCELL_PROGRAM_DUPLICATE_NAME_BIT bit set after a build error when a net has a name used by another net
 * 3 = 1 GRIDCELL_PROGRAM_NO_MERGED_BIT bit set after build error when a net has a defined name but no merged layer cell
 * 2 = 1 GRIDCELL_PROGRAM_MULTI_NAME_BIT bit set after build error on multiple cells in a single net defining a label
 * 1 = 1 GRIDCELL_PROGRAM_BUSY_BIT bit (or cell-involved-in-cycle-error bit after build error)
 * B = 6 bits of edge-involved-in-cycle error bits
 * P = 24 bits of net identifier
 */

#define GRIDCELL_PROGRAM_BITS        (GRIDCELL_PROGRAM_NET_ID_BITS|GRIDCELL_PROGRAM_BROKEN_BITS|GRIDCELL_PROGRAM_BUSY_BIT|GRIDCELL_PROGRAM_MULTI_NAME_BIT|GRIDCELL_PROGRAM_NO_MERGED_BIT|GRIDCELL_PROGRAM_DUPLICATE_NAME_BIT)


/* tests if any cell edges are not disconnected, the layers are merged or the
 * GRIDCELL_NET_LABEL_BIT bit is set. if any of these conditions are true, the
 * cell is guaranteed to have a net assigned to it. If it is not set, the cell
 * definitely will not have a net assigned to it. */
#define CELL_WILL_GET_A_NET(data_)   (((data_) & (GRIDCELL_BIT_MERGED_LAYERS | GRIDCELL_NET_LABEL_BIT | GRIDCELL_CELL_EDGE_BITS_MASK)) != 0)


/* How the grid is layed out:
 *
 *           x=0       x=1       x=2       x=3
 *        ___       ___       ___       ___       ___
 *       /   \     /   \     /   \     /   \     /   \     /
 * y=6  / 0,6 \___/ 1,6 \___/ 2,6 \___/ 3,6 \___/ 4,6 \___/ ...
 *      \     /   \     /   \     /   \     /   \     /
 * y=5   \___/ 0,5 \___/ 1,5 \___/ 2,5 \___/ 3,5 \___/ ...
 *       /   \     /   \     /   \     /   \     /
 * y=4  / 0,4 \___/ 1,4 \___/ 2,4 \___/ 3,4 \___/ ...
 *      \     /   \     /   \     /   \     /
 * y=3   \___/ 0,3 \___/ 1,3 \___/ 2,3 \___/ ...
 *       /   \     /   \     /   \     /
 * y=2  / 0,2 \___/ 1,2 \___/ 1,2 \___/ ...
 *      \     /   \     /   \     /
 * y=1   \___/ 0,1 \___/ 1,1 \___/ ...
 *       /   \     /   \     /             NW   N   NE
 * y=0  / 0,0 \___/ 1,0 \___/ ...            \  |  /
 *      \     /   \     /                  W __\|/__ E
 * y=-1  \___/ 0,-1\___/ ...                   /|\
 *       /   \     /                         /  |  \
 * y=-2 / 0,-2\___/ ...                    SW   S   SE
 *      \     /
 *       \___/
 *      
 * For edges             For vertices
 * ---------             ------------
 *
 * Common rules:
 *  N  = (x,   y+2)      E  = (x+1, y)
 *  S  = (x,   y-2)      W  = (x-1, y)
 * 
 * If y is even:
 *  NE = (x,   y+1)      NE = (x,   y+3)
 *  SE = (x,   y-1)      SE = (x,   y-3)
 *  SW = (x-1, y-1)      SW = (x-1, y-3)
 *  NW = (x-1, y+1)      NW = (x-1, y+3)
 * 
 * If y is odd:
 *  NE = (x+1, y+1)      NE = (x+1, y+3)
 *  SE = (x+1, y-1)      SE = (x+1, y-3)
 *  SW = (x,   y-1)      SW = (x,   y-3)
 *  NW = (x,   y+1)      NW = (x,   y+3)
 */

static int gridaddr_edge_neighbour(struct gridaddr *p_dest, const struct gridaddr *p_src, int edge_direction) {
	static const int32_t y_offsets[6] =
		{   2   /* N */
		,   1   /* NE */
		,  -1   /* SE */
		,   1   /* NW */
		,  -1   /* SW */
		,  -2   /* S */
		};
	static const int32_t x_offsets[6][2] =
		/*  y_even  y_odd */
		{   {0,     0} /* N */
		,   {0,     1} /* NE */
		,   {0,     1} /* SE */
		,   {-1,    0} /* NW */
		,   {-1,    0} /* SW */
		,   {0,     0} /* S */
		};
	assert(edge_direction < EDGE_DIR_NUM);
	return gridaddr_add_check(p_dest, p_src, x_offsets[edge_direction][p_src->y & 1], y_offsets[edge_direction]);
}

struct gridcell {
	uint64_t data;

	/* LSB
	 *
	 * Used to identify the address of a cell within a gridpage. Can be used
	 * to get a pointer to the gridpage from the gridcell (which always exists
	 * inside of a gridpage.)
	 * 0:9  - is the index of the cell within the page (PAGE_XY_BITS (x) + PAGE_XY_BITS (y) + 2 (z) bits)
	 *
	 * Edge property bits.
	 * 10:12 - N_EDGE
	 * 13:15 - NE_EDGE
	 * 16:18 - SE_EDGE
	 * 19:21 - NW_EDGE
	 * 22:24 - SW_EDGE
	 * 25:27 - S_EDGE
	 *
	 * Set if the cell is merged with all cells above and below it. The flag
	 * is either set on none or all layers.
	 * 28    - MERGED_LAYERS
	 *
	 * This bit indicates that the cell defines a name or description for the
	 * net it is part of. This bit guarantees the creation of a net.
	 * 29    - GRIDCELL_NET_LABEL_BIT
	 *
	 * This bit is used as a temporary while creating a program. Once the
	 * program has been written this bit will be cleared. If the program
	 * fails to be created due to cycles, this bit will be set if the cell is
	 * part of a broken cycle. This is not ideal and it would be better to
	 * have 6 bits which identify the edge instead of the cell itself and we
	 * might change that later - for now, we have 32 bits for the net id.
	 * 33    - BUSY/BROKEN_NET
	 * 34:39 - BROKEN_EDGE (N, NE, SE, NW, SW, S)
	 * 40:63 - STORAGE_ID */

};

static void rt_assert_impl(int condition, const char *p_cond_str, const char *p_file, const int line) {
	if (!condition) {
		fprintf(stderr, "assertion failure(%s:%d): %s\n", p_file, line, p_cond_str);
		abort();
	}
}
#define RT_ASSERT_INNER(condition_, file_, line_) rt_assert_impl((condition_), #condition_, file_, line_)
#define RT_ASSERT(condition_)                     rt_assert_impl((condition_), #condition_, __FILE__, __LINE__)

struct gridpage {
	struct gridcell   data[PAGE_CELL_COUNT];
	struct gridstate *p_owner;
	struct gridaddr   position; /* position of data[0] in the full grid  */
	
};

MAKE_UTREE(gridpage_lookup, uint64_t, 8, 1, struct gridpage)

#define MAX_NET_NAME_LENGTH (64)
#define MAX_NET_DESCRIPTION_LENGTH (8192)

struct cellnetinfo {
	struct gridaddr     position;          /* key and full position of cell (with z==0). this cell must have the GRIDCELL_BIT_MERGED_LAYERS and GRIDCELL_NET_LABEL_BIT bits set. */
	char                aa_net_name[NUM_LAYERS][MAX_NET_NAME_LENGTH];        /* unique name for the net containing this cell. */
	char                aa_net_description[NUM_LAYERS][MAX_NET_DESCRIPTION_LENGTH]; /* description for the net which contains this cell. */

};

MAKE_UTREE(cellnetinfo_lookup, uint64_t, 8, 1, struct cellnetinfo)

#ifndef NDEBUG
static void verify_gridpage(struct gridpage *p_page, const char *p_file, const int line) {
	size_t i;
	RT_ASSERT_INNER(p_page->p_owner != NULL, p_file, line);
	RT_ASSERT_INNER((p_page->position.x & PAGE_XY_MASK) == 0, p_file, line);
	RT_ASSERT_INNER((p_page->position.y & PAGE_XY_MASK) == 0, p_file, line);
	RT_ASSERT_INNER((p_page->position.z == 0), p_file, line);
	for (i = 0; i < PAGE_CELL_COUNT; i++)
		RT_ASSERT_INNER((p_page->data[i].data & 0x3FF) == i, p_file, line);
}
#define DEBUG_CHECK_GRIDPAGE(p_page_) verify_gridpage(p_page_, __FILE__, __LINE__)
#else
#define DEBUG_CHECK_GRIDPAGE(p_page_) ((void)0)
#endif

struct gridstate {
	struct gridpage_lookup    pages;
	struct cellnetinfo_lookup cellinfo;

};

void gridstate_init(struct gridstate *p_gridstate) {
	gridpage_lookup_init(&(p_gridstate->pages));
	cellnetinfo_lookup_init(&(p_gridstate->cellinfo));
}

/* Converts a grid address into a unique, scrambled and invertible
 * identifier. */
static uint64_t gridaddr_to_id(const struct gridaddr *p_addr) {
	uint64_t grp =
		(URANGE_CHECK((uint64_t)p_addr->z, 0x3) << 62) |
		(URANGE_CHECK((uint64_t)p_addr->y, 0x7FFFFFFF) << 31) |
		URANGE_CHECK(p_addr->x, 0x7FFFFFFF);
	uint64_t umix = grp*8249772677985670961ull;
	return (umix >> 48) ^ umix;
}

static void gridaddr_from_id(struct gridaddr *p_addr, uint64_t id) {
	uint64_t umix = (id >> 48) ^ id;
	uint64_t grp = umix*7426732773883044305ull;
	p_addr->z = (uint32_t)(grp >> 62);
	p_addr->y = (uint32_t)((grp >> 31) & 0x7FFFFFFF);
	p_addr->x = (uint32_t)(grp & 0x7FFFFFFF);
	assert(p_addr->z < 3);
}

static void write_u32(unsigned char *p_buf, uint32_t data) {
	p_buf[0] = data & 0xFF;
	p_buf[1] = (data >> 8) & 0xFF;
	p_buf[2] = (data >> 16) & 0xFF;
	p_buf[3] = (data >> 24) & 0xFF;
}

static void write_u64(unsigned char *p_buf, uint64_t data) {
	write_u32(p_buf, data & 0xFFFFFFFF);
	write_u32(p_buf, data >> 32);
}

#define RW_EDGE_NO_ACTION               (0) /* 000 */
#define RW_EDGE_RECEIVES_DELAY_INVERTED (2) /* 001 */
#define RW_EDGE_RECEIVES_INVERTED       (3) /* 010 */
#define RW_EDGE_NET_CONNECTED           (4) /* 011 - only valid on n, ne, se (0, 1, 2) edges. */

/* Returns size number of cell declarations written into p_buf.
 * 
 * p_buf must contain worst-case storage.
 * 
 * Up to PAGE_CELLS_PER_LAYER cell declarations will be written. Each cell
 * declaration is 16 bytes. */
static size_t gridpage_serialise(struct gridpage *p_page, unsigned char *p_buf) {
	int    i;
	size_t count = 0;
	for (i = 0; i < PAGE_CELLS_PER_LAYER; i++) {
		uint64_t ldata[3];

		ldata[0] = p_page->data[i+0*PAGE_CELLS_PER_LAYER].data;
		ldata[1] = p_page->data[i+1*PAGE_CELLS_PER_LAYER].data;
		ldata[2] = p_page->data[i+2*PAGE_CELLS_PER_LAYER].data;

		if  (   !CELL_WILL_GET_A_NET(ldata[0])
		    &&  !CELL_WILL_GET_A_NET(ldata[1])
		    &&  !CELL_WILL_GET_A_NET(ldata[2])
		    ) {
			continue;
		}

		if (p_buf != NULL) {
			uint32_t addr_x = p_page->position.x | (i & PAGE_XY_MASK);
			uint32_t addr_y = p_page->position.y | ((i >> PAGE_XY_BITS) & PAGE_XY_MASK);
			uint64_t edgemode_bits;
			int      j, k;

			/* 62 bits of xy position and 2 bits of hash */
			assert(((addr_x | addr_y) & 0x80000000) == 0);
			edgemode_bits  = (((uint64_t)addr_y) << 31) | addr_x;
			edgemode_bits |= (edgemode_bits*8249772677985670961ull) & 0xC000000000000000;
			write_u64(p_buf, edgemode_bits);
			p_buf += 8;

			/* writes 54 bits of edge data */
			edgemode_bits = 0;
			for (k = 0; k < 3; k++) {
				for (j = 0; j < 6; j++) {
					if (GRIDCELL_CELL_EDGE_BITS_GET(ldata[k], j) == EDGE_LAYER_CONNECTION_NET_CONNECTED)
						edgemode_bits = (edgemode_bits << 3) | RW_EDGE_NET_CONNECTED;
					else if (GRIDCELL_CELL_EDGE_BITS_GET(ldata[k], j) == EDGE_LAYER_CONNECTION_RECEIVES_DELAY_INVERTED)
						edgemode_bits = (edgemode_bits << 3) | RW_EDGE_RECEIVES_DELAY_INVERTED;
					else if (GRIDCELL_CELL_EDGE_BITS_GET(ldata[k], j) == EDGE_LAYER_CONNECTION_RECEIVES_INVERTED)
						edgemode_bits = (edgemode_bits << 3) | RW_EDGE_RECEIVES_INVERTED;
					else
						edgemode_bits = (edgemode_bits << 3) | RW_EDGE_NO_ACTION;
				}
			}

			/* 1-bit for merged layer indicator */
			edgemode_bits <<= 1;
			if (ldata[0] & GRIDCELL_BIT_MERGED_LAYERS) {
				assert((ldata[1] & GRIDCELL_BIT_MERGED_LAYERS) != 0);
				assert((ldata[2] & GRIDCELL_BIT_MERGED_LAYERS) != 0);
				edgemode_bits |= 1;
			} else {
				assert((ldata[1] & GRIDCELL_BIT_MERGED_LAYERS) == 0);
				assert((ldata[2] & GRIDCELL_BIT_MERGED_LAYERS) == 0);
			}

			/* 3-bits for net info indicators */
			for (k = 0; k < 3; k++) {
				edgemode_bits = (edgemode_bits << 1) | ((ldata[k] & GRIDCELL_NET_LABEL_BIT) ? 1 : 0);
			}

			/* HHHHHH00 00000000 00000000 11111111 11111111 11222222 22222222 2222Mabc
			 * H = checksum
			 * 0 = layer 0 edge bits
			 * 1 = layer 1 edge bits
			 * 2 = layer 2 edge bits
			 * M = merged layer bit
			 * abc = layer 0, 1 and 2 net label bits  */

			/* 54+4=58 bits of cell information and 6 bits of hash. */
			edgemode_bits |= (edgemode_bits*8249772677985670961ull) & 0xFC00000000000000;
			write_u64(p_buf, edgemode_bits);
			p_buf += 8;
		}

		count++;
	}
	return count;
}

static size_t gridstate_serialise(struct gridstate *p_grid, unsigned char *p_buffer) {
	struct gridpage_lookup_enumerator  enumerator;
	struct gridpage                   *p_page;
	uint64_t                           num_total_cells = 0;

	num_total_cells = 0;
	gridpage_lookup_enumerator_init(&enumerator, &(p_grid->pages));
	while ((p_page = gridpage_lookup_node_optional_get_data(gridpage_lookup_enumerator_get(&enumerator))) != NULL) {
		num_total_cells += gridpage_serialise(p_page, NULL);
	}

	if (p_buffer != NULL) {
		write_u64(p_buffer, num_total_cells);
		p_buffer += 8;

		gridpage_lookup_enumerator_init(&enumerator, &(p_grid->pages));
		while ((p_page = gridpage_lookup_node_optional_get_data(gridpage_lookup_enumerator_get(&enumerator))) != NULL) {
			p_buffer += 16*gridpage_serialise(p_page, p_buffer);
		}
	}

	return 8 + num_total_cells*16;
}

static struct cellnetinfo_lookup_node *make_cellnetinfo_lookup_node(uint64_t key, void *p_context) {
	struct cellnetinfo_lookup_node *p_node;
	(void)p_context;
	if ((p_node = (struct cellnetinfo_lookup_node *)malloc(sizeof(*p_node))) != NULL) {
		struct cellnetinfo *p_cni = cellnetinfo_lookup_node_get_data(p_node);
		int i;
		for (i = 0; i < NUM_LAYERS; i++) {
			p_cni->aa_net_description[i][0] = '\0';
			p_cni->aa_net_name[i][0]        = '\0';
		}
		gridaddr_from_id(&(p_cni->position), key);
	}
	return p_node;
}

static struct cellnetinfo *gridstate_get_cellnetinfo(struct gridstate *p_grid, const struct gridaddr *p_page_addr, int permit_create) {
	assert(p_page_addr->z == 0 && "cellnetinfo always must apply to layer 0");
	return
		cellnetinfo_lookup_node_optional_get_data(cellnetinfo_lookup_find_insert_cb
			(&(p_grid->cellinfo)
			,gridaddr_to_id(p_page_addr)
			,(permit_create) ? make_cellnetinfo_lookup_node : NULL
			,NULL
			));
}

static struct gridpage_lookup_node *make_gridpage_lookup_node(uint64_t key, void *p_context) {
	struct gridpage_lookup_node *p_node;
	if ((p_node = (struct gridpage_lookup_node *)malloc(sizeof(*p_node))) != NULL) {
		struct gridpage *p_gp = gridpage_lookup_node_get_data(p_node);
		int i;
		p_gp->p_owner = (struct gridstate *)p_context;
		gridaddr_from_id(&(p_gp->position), key);
		for (i = 0; i < PAGE_CELL_COUNT; i++) {
			p_gp->data[i].data = i;
		}
	}
	return p_node;
}

static struct gridpage *gridstate_get_gridpage(struct gridstate *p_grid, const struct gridaddr *p_page_addr, int permit_create) {
	struct gridpage *p_ret;
	assert(gridaddr_is_page_addr(p_page_addr));
	p_ret = gridpage_lookup_node_optional_get_data(gridpage_lookup_find_insert_cb(&(p_grid->pages), gridaddr_to_id(p_page_addr), (permit_create) ? make_gridpage_lookup_node : NULL, (permit_create) ? p_grid : NULL));
	if (p_ret != NULL) {
		DEBUG_CHECK_GRIDPAGE(p_ret);
	}
	return  p_ret;
}

static struct gridcell *gridstate_get_gridcell(struct gridstate *p_grid, const struct gridaddr *p_addr, int permit_create) {
	struct gridaddr page_addr;
	uint32_t cell_index = gridaddr_split(&page_addr, p_addr);
	struct gridpage *p_page = gridstate_get_gridpage(p_grid, &page_addr, permit_create);
	if (p_page == NULL)
		return NULL;
	return &(p_page->data[cell_index]);
}

static unsigned gridcell_get_page_index(const struct gridcell *p_gc) {
	return p_gc->data & 0x3FF;
}

static struct gridpage *gridcell_get_page_and_index(const struct gridcell *p_cell, uint32_t *p_page_index) {
	*p_page_index = gridcell_get_page_index(p_cell);
	return (struct gridpage *)(p_cell - *p_page_index);
}

/* Given a cell, find the page which it is part of */
static struct gridpage *gridcell_get_gridpage_and_full_addr(struct gridcell *p_cell, struct gridaddr *p_addr) {
	uint32_t         page_index;
	struct gridpage *p_page = gridcell_get_page_and_index(p_cell, &page_index);
	DEBUG_CHECK_GRIDPAGE(p_page);
	p_addr->x = p_page->position.x | (page_index & PAGE_XY_MASK);
	p_addr->y = p_page->position.y | ((page_index >> PAGE_XY_BITS) & PAGE_XY_MASK);
	p_addr->z = page_index >> (2*PAGE_XY_BITS);
	return p_page;
}

static struct gridpage *gridpage_get_gridpage(struct gridpage *p_page, const struct gridaddr *p_page_addr, int permit_create) {
	/* This is a fast path for when the page address is the supplied page. */
	if (p_page->position.x == p_page_addr->x && p_page->position.y == p_page_addr->y)
		return p_page;
	assert(p_page->p_owner != NULL);
	return gridstate_get_gridpage(p_page->p_owner, p_page_addr, permit_create);
}

static struct gridcell *gridpage_get_gridcell(struct gridpage *p_page, const struct gridaddr *p_addr, int permit_create) {
	struct gridaddr page_addr;
	uint32_t        page_index = gridaddr_split(&page_addr, p_addr);
	if ((p_page = gridpage_get_gridpage(p_page, &page_addr, permit_create)) == NULL)
		return NULL;
	return &(p_page->data[page_index]);
}

static struct gridcell *gridcell_get_edge_neighbour(struct gridcell *p_cell, int edge_id, int permit_create) {
	struct gridaddr  neighbour_addr;
	struct gridpage *p_cell_page = gridcell_get_gridpage_and_full_addr(p_cell, &neighbour_addr);

	assert(edge_id < EDGE_DIR_NUM); /* there are no east and west edges in the hex grid, just as there are no north and south vertices */
	
	if (gridaddr_edge_neighbour(&neighbour_addr, &neighbour_addr, edge_id))
		return NULL;

	return gridpage_get_gridcell(p_cell_page, &neighbour_addr, permit_create);
}

static int gridcell_set_neighbour_edge_connection_type(struct gridcell *p_gc, int edge_id, int connection_type) {
	struct gridcell *p_neighbour = gridcell_get_edge_neighbour(p_gc, edge_id, 1);
	if (p_neighbour != NULL) {
		uint64_t cval;
		uint64_t mask = 0x7;
		uint64_t nval = connection_type;
		int      csft = 10 + edge_id*3;
		int      nsft = 10 + get_opposing_edge_id(edge_id)*3;

		if (connection_type == EDGE_LAYER_CONNECTION_UNCONNECTED || connection_type == EDGE_LAYER_CONNECTION_NET_CONNECTED) {
			cval = connection_type;
		} else {
			assert(connection_type == EDGE_LAYER_CONNECTION_RECEIVES_DELAY_INVERTED || connection_type == EDGE_LAYER_CONNECTION_RECEIVES_INVERTED);
			cval = EDGE_LAYER_CONNECTION_SENDS;
		}

		p_gc->data        = (p_gc->data & ~(mask << csft)) | (cval << csft);
		p_neighbour->data = (p_neighbour->data & ~(mask << nsft)) | (nval << nsft);
		return 0;
	}
	return 1;
}

static int gridcell_get_edge_connection_type(const struct gridcell *p_gc, int edge_id) {
	unsigned edge_props;
	assert(edge_id < EDGE_DIR_NUM);
	edge_props       = (p_gc->data >> (10 + 3*edge_id)) & 0x7;
	return URANGE_CHECK(edge_props, 4);
}

static int gridcell_get_neighbour_edge_connection_type(struct gridcell *p_gc, int edge_id) {
	struct gridcell *p_neighbour = gridcell_get_edge_neighbour(p_gc, edge_id, 0);
	if (p_neighbour == NULL)
		return EDGE_LAYER_CONNECTION_UNCONNECTED;
	return gridcell_get_edge_connection_type(p_neighbour, get_opposing_edge_id(edge_id));
}

static int gridcell_are_layers_fused_get(const struct gridcell *p_gc) {
	return (p_gc->data & GRIDCELL_BIT_MERGED_LAYERS) != 0;
}
static void gridcell_are_layers_fused_set(struct gridcell *p_gc, int b_value) {
	struct gridpage *p_page;
	uint32_t         page_index;
	uint32_t         i;

	if (b_value != 0 && (p_gc->data & GRIDCELL_BIT_MERGED_LAYERS) != 0)
		return;
	if (b_value == 0 && (p_gc->data & GRIDCELL_BIT_MERGED_LAYERS) == 0)
		return;

	p_page = gridcell_get_page_and_index(p_gc, &page_index);

	page_index &= 0xFF;

	for (i = 0; i < NUM_LAYERS; i++) {
		if (b_value) {
			p_page->data[page_index | (i << 8)].data |= GRIDCELL_BIT_MERGED_LAYERS;
		} else {
			p_page->data[page_index | (i << 8)].data &= ~GRIDCELL_BIT_MERGED_LAYERS;
		}
	}
}
static void gridcell_are_layers_fused_toggle(struct gridcell *p_gc) {
	gridcell_are_layers_fused_set(p_gc, !gridcell_are_layers_fused_get(p_gc));
}



struct program_net {
	uint32_t net_id;
	uint32_t first_cell_stack_index;
	uint32_t cell_count;
	uint32_t current_solve_cell;
	uint32_t current_solve_edge;
	uint32_t b_currently_in_solve_stack;
	uint32_t b_exists_in_a_cycle;
	uint32_t serial_gates;
	uint32_t gate_fanout;

	uint32_t        nb_net_info_refs;
	struct gridaddr net_info_cell_addr;
	char           *p_net_name;
	char           *p_net_description;

};

struct program {
	/* The program is made up of net_count blocks which define the value of that net.
	 *
	 * Each group consists of a count of how many sources exist. Followed by a pair
	 * of words for each source. The first word is the mode and the second is a source
	 * net. The modes are as follows:
	 *   * 0: invert the value of the given net
	 *   * 1: invert the value of the previous value of the given net
	 * 
	 * The supplied nets ids must be smaller than the currently active net for
	 * modes which require immediate value. This forces sequencing of operation.
	 * 
	 * It is permissible for count values to be zero in which case the data is not
	 * touched.
	 * 
	 * After the program has executed, the data buffer is copied into an old state
	 * buffer and the new buffer is zeroed. The caller may initialise specific data
	 * bits prior to execution to provide external input. */
	uint32_t *p_code;
	size_t    code_count;
	size_t    code_alloc_count;

	/* named net list */
	struct program_net **pp_labelled_nets;
	size_t               named_net_count;
	size_t               labelled_net_count;
	size_t               labelled_net_alloc_count;

	/* net list */
	struct program_net **pp_netstack;
	struct program_net  *p_nets;
	uint64_t            *p_data_init;
	uint64_t            *p_data;
	uint64_t            *p_last_data;
	size_t               net_alloc_count;
	size_t               net_count;

	/* stack */
	struct gridcell    **pp_stack;
	size_t               stack_count;
	size_t               stack_alloc_count;

	/* Statistics */
	size_t               stacked_cell_count; /* available on failed build */
	uint64_t             substrate_area; /* available on failed build */
	uint32_t             worst_logic_chain; /* available only if program valid */

};


#define U64MASKS(x_) \
	{x_((uint64_t)0x0000000000000001) \
	,x_((uint64_t)0x0000000000000002) \
	,x_((uint64_t)0x0000000000000004) \
	,x_((uint64_t)0x0000000000000008) \
	,x_((uint64_t)0x0000000000000010) \
	,x_((uint64_t)0x0000000000000020) \
	,x_((uint64_t)0x0000000000000040) \
	,x_((uint64_t)0x0000000000000080) \
	,x_((uint64_t)0x0000000000000100) \
	,x_((uint64_t)0x0000000000000200) \
	,x_((uint64_t)0x0000000000000400) \
	,x_((uint64_t)0x0000000000000800) \
	,x_((uint64_t)0x0000000000001000) \
	,x_((uint64_t)0x0000000000002000) \
	,x_((uint64_t)0x0000000000004000) \
	,x_((uint64_t)0x0000000000008000) \
	,x_((uint64_t)0x0000000000010000) \
	,x_((uint64_t)0x0000000000020000) \
	,x_((uint64_t)0x0000000000040000) \
	,x_((uint64_t)0x0000000000080000) \
	,x_((uint64_t)0x0000000000100000) \
	,x_((uint64_t)0x0000000000200000) \
	,x_((uint64_t)0x0000000000400000) \
	,x_((uint64_t)0x0000000000800000) \
	,x_((uint64_t)0x0000000001000000) \
	,x_((uint64_t)0x0000000002000000) \
	,x_((uint64_t)0x0000000004000000) \
	,x_((uint64_t)0x0000000008000000) \
	,x_((uint64_t)0x0000000010000000) \
	,x_((uint64_t)0x0000000020000000) \
	,x_((uint64_t)0x0000000040000000) \
	,x_((uint64_t)0x0000000080000000) \
	,x_((uint64_t)0x0000000100000000) \
	,x_((uint64_t)0x0000000200000000) \
	,x_((uint64_t)0x0000000400000000) \
	,x_((uint64_t)0x0000000800000000) \
	,x_((uint64_t)0x0000001000000000) \
	,x_((uint64_t)0x0000002000000000) \
	,x_((uint64_t)0x0000004000000000) \
	,x_((uint64_t)0x0000008000000000) \
	,x_((uint64_t)0x0000010000000000) \
	,x_((uint64_t)0x0000020000000000) \
	,x_((uint64_t)0x0000040000000000) \
	,x_((uint64_t)0x0000080000000000) \
	,x_((uint64_t)0x0000100000000000) \
	,x_((uint64_t)0x0000200000000000) \
	,x_((uint64_t)0x0000400000000000) \
	,x_((uint64_t)0x0000800000000000) \
	,x_((uint64_t)0x0001000000000000) \
	,x_((uint64_t)0x0002000000000000) \
	,x_((uint64_t)0x0004000000000000) \
	,x_((uint64_t)0x0008000000000000) \
	,x_((uint64_t)0x0010000000000000) \
	,x_((uint64_t)0x0020000000000000) \
	,x_((uint64_t)0x0040000000000000) \
	,x_((uint64_t)0x0080000000000000) \
	,x_((uint64_t)0x0100000000000000) \
	,x_((uint64_t)0x0200000000000000) \
	,x_((uint64_t)0x0400000000000000) \
	,x_((uint64_t)0x0800000000000000) \
	,x_((uint64_t)0x1000000000000000) \
	,x_((uint64_t)0x2000000000000000) \
	,x_((uint64_t)0x4000000000000000) \
	,x_((uint64_t)0x8000000000000000) \
	}

#define NOTHING(x_) x_
static uint64_t BIT_MASKS[] = U64MASKS(NOTHING);
#undef NOTHING
#if 0
#define INVERT(x_) ~x_
static uint64_t INV_BIT_MASKS[] = U64MASKS(INVERT);
#undef INVERT
#endif

/* Before running this, the caller can initialise any field input bits to
 * 1 in the p_data pointer. p_data is zeroed after every execution so
 * externally supplied bits must be written prior to calling program_run
 * every time. Also, neither p_data nor p_last_data can be cached, their
 * values will change after every call to program_run.
 * 
 * After running this, output data for each net is in p_last_data. */
void program_run(struct program *p_program) {
	/* run the program. */
	uint32_t *p_code     = p_program->p_code;
	uint64_t *p_data     = p_program->p_data;
	uint64_t *p_old_data = p_program->p_last_data;
	size_t    net_count  = p_program->net_count;
	size_t    dest_net;

	/* state init. */
	memcpy(p_data, p_program->p_data_init, ((p_program->net_count + 63)/64)*sizeof(uint64_t));

	for (dest_net = 0; dest_net < net_count; dest_net++) {
		size_t    nb_sources   = *p_code++;
		uint64_t *p_dest       = &(p_data[dest_net >> 6]);
		uint64_t  dest_val     = *p_dest;
		uint64_t  dest_val_set = dest_val | (1ull << (dest_net & 0x3F));

		dest_val_set = (nb_sources) ? dest_val_set : dest_val; /* ensure no further reads if there are zero sources */

		while (dest_val != dest_val_set) {
			uint32_t  word       = *p_code++;
			uint32_t  src_net    = word & 0xFFFFFF;
			uint64_t *p_data_src = (word & 0x1000000) ? p_old_data : p_data;
			uint64_t  src_data   = (assert((word & 0x1000000) != 0 || (src_net < dest_net)), p_data_src[src_net >> 6]);
			uint64_t  src_val    = src_data; /* (word & 0x2000000) ? ~src_data : src_data; <<<<< MAYBE - DO WE WANT DIODES? */
			uint64_t  src_ctl    = src_val & (1ull << (src_net & 0x3F)); // & BIT_MASKS[src_net & 0x3F];
			dest_val     = (src_ctl) ? dest_val : dest_val_set; /* set the bit if the conditions have been met */
			dest_val_set = (--nb_sources) ? dest_val_set : dest_val; /* ensure termination */
			assert((word >> 24) < 2);
		}

		p_code += nb_sources;
		*p_dest = dest_val;
	}

	/* pointer jiggle and state reset. */
	p_program->p_data      = p_old_data;
	p_program->p_last_data = p_data;
}

static
struct program_net *
program_net_push(struct program *p_program) {
	if (p_program->net_count >= p_program->net_alloc_count) {
		struct program_net  *p_new_nets;
		struct program_net **pp_new_netstack;
		uint64_t            *p_new_data;
		size_t newsz      = ((p_program->net_count*4)/3) & ~(size_t)0xff;
		size_t data_words;
		if (newsz < 1024)
			newsz = 1024;
		data_words = (newsz + 63)/64;
		if ((p_new_nets = (struct program_net *)realloc(p_program->p_nets, sizeof(struct program_net)*newsz)) == NULL)
			abort();
		if ((pp_new_netstack = (struct program_net **)realloc(p_program->pp_netstack, sizeof(struct program_net *)*newsz)) == NULL)
			abort();
		if ((p_new_data = (uint64_t *)realloc(p_program->p_data, sizeof(uint64_t)*3*data_words)) == NULL)
			abort();

		p_program->net_alloc_count = newsz;
		p_program->pp_netstack     = pp_new_netstack;
		p_program->p_nets          = p_new_nets;
		p_program->p_data          = p_new_data;
		p_program->p_last_data     = &(p_new_data[data_words]);
		p_program->p_data_init     = &(p_new_data[2*data_words]);
	}
	return &(p_program->p_nets[p_program->net_count++]);
}

static void program_init(struct program *p_program) {
	p_program->p_code           = NULL;
	p_program->p_data           = NULL;
	p_program->p_last_data      = NULL;
	p_program->pp_stack         = NULL;
	p_program->p_nets           = NULL;
	p_program->pp_netstack      = NULL;
	p_program->pp_labelled_nets = NULL;

	p_program->code_alloc_count         = 0;
	p_program->stack_alloc_count        = 0;
	p_program->net_alloc_count          = 0;
	p_program->labelled_net_alloc_count = 0;

	p_program->code_count         = 0;
	p_program->stack_count        = 0;
	p_program->net_count          = 0;
	p_program->labelled_net_count = 0;
	p_program->named_net_count    = 0;
	p_program->stacked_cell_count = 0;
	p_program->substrate_area     = 0;
	p_program->worst_logic_chain  = 0;

	(void)program_net_push(p_program);

	p_program->p_data[0]      = 0;
	p_program->p_last_data[0] = 0;

	p_program->net_count   = 0;
}

/* Returns true if the program is executable and program_run() can be called.
 * This function returns false after a failed call to program_compile() and
 * indicates that some of the cells will have the BROKEN bit set. */
static int program_is_valid(struct program *p_program) {
	return (p_program->net_count == 0) || (p_program->code_count > 0);
}

static void program_labelled_net_push(struct program *p_program, struct program_net *p_ptr) {
	if (p_program->labelled_net_count >= p_program->labelled_net_alloc_count) {
		size_t               newsz = ((p_program->labelled_net_alloc_count*4)/3) & ~(size_t)0xff;
		struct program_net **pp_new_list;
		if (newsz < 1024)
			newsz = 1024;
		if ((pp_new_list = (struct program_net **)realloc(p_program->pp_labelled_nets, newsz*sizeof(struct program_net *))) == NULL)
			abort();
		p_program->pp_labelled_nets         = pp_new_list;
		p_program->labelled_net_alloc_count = newsz;
	}
	p_program->pp_labelled_nets[p_program->labelled_net_count++] = p_ptr;
}

static void program_stack_push(struct program *p_program, struct gridcell *p_ptr) {
	if (p_program->stack_count >= p_program->stack_alloc_count) {
		size_t            newsz = ((p_program->stack_alloc_count*4)/3) & ~(size_t)0xff;
		struct gridcell **pp_new_list;
		if (newsz < 1024)
			newsz = 1024;
		if ((pp_new_list = (struct gridcell **)realloc(p_program->pp_stack, newsz*sizeof(struct gridcell *))) == NULL)
			abort();
		p_program->pp_stack          = pp_new_list;
		p_program->stack_alloc_count = newsz;
	}
	p_program->pp_stack[p_program->stack_count++] = p_ptr;
}

static uint32_t *program_code_reserve(struct program *p_program) {
	if (p_program->code_count >= p_program->code_alloc_count) {
		size_t    newsz = ((p_program->code_count*4)/3) & ~(size_t)0xff;
		uint32_t *p_new_code;
		if (newsz < 1024)
			newsz = 1024;
		if ((p_new_code = (uint32_t *)realloc(p_program->p_code, newsz*sizeof(uint32_t))) == NULL)
			abort();
		p_program->p_code           = p_new_code;
		p_program->code_alloc_count = newsz;
	}
	return &(p_program->p_code[p_program->code_count++]);
}

static int net_label_compare(const void *p_a, const void *p_b) {
	struct program_net *p_n1 = *(struct program_net **)p_a;
	struct program_net *p_n2 = *(struct program_net **)p_b;
	if (p_n1->p_net_name == NULL && p_n2->p_net_name == NULL) {
		if (p_n1->p_net_description == NULL && p_n2->p_net_description == NULL)
			return 0;
		if (p_n1->p_net_description == NULL)
			return 1;
		if (p_n2->p_net_description == NULL)
			return -1;
		return strcmp(p_n1->p_net_description, p_n2->p_net_description);
	}
	if (p_n1->p_net_name == NULL)
		return 1;
	if (p_n2->p_net_name == NULL)
		return -1;
	return strcmp(p_n1->p_net_name, p_n2->p_net_name);
}

static struct program_net *program_find_named_net(struct program *p_program, const char *p_name) {
	struct program_net   dummy;
	struct program_net  *p_dummy;
	struct program_net **pp_found;

	assert(p_name != NULL);

	dummy.p_net_description = NULL;
	dummy.p_net_name        = (char *)p_name;

	p_dummy  = &dummy;
	pp_found = (struct program_net **)bsearch
		((const void *)&p_dummy
		,(const void *)p_program->pp_labelled_nets
		,p_program->named_net_count
		,sizeof(struct program_net *)
		,net_label_compare
		);

	if (pp_found == NULL)
		return NULL;

	return *pp_found;
}

static void move_single_cell(struct gridstate *p_gridstate, struct gridcell **pp_list_base, uint32_t idx, uint32_t *p_insidx, uint64_t compute_flag_and_net_id_bits, struct program_net *p_net) {
	uint32_t new_index     = (*p_insidx)++;

	assert((pp_list_base[idx]->data & GRIDCELL_PROGRAM_BUSY_BIT) == 0);

	if (pp_list_base[idx]->data & GRIDCELL_NET_LABEL_BIT) {
		if (p_net->nb_net_info_refs++ == 0) {
			struct cellnetinfo *p_info;
			struct gridaddr     tmpaddr;
			uint32_t            layer;
			(void)gridcell_get_gridpage_and_full_addr(pp_list_base[idx], &tmpaddr);
			p_net->net_info_cell_addr = tmpaddr;
			layer = tmpaddr.z;
			tmpaddr.z = 0;
			p_info = gridstate_get_cellnetinfo(p_gridstate, &tmpaddr, 0);
			assert(p_info != NULL);
			p_net->p_net_name        = p_info->aa_net_name[layer];
			p_net->p_net_description = p_info->aa_net_description[layer];
			if (p_net->p_net_name[0] == '\0')
				p_net->p_net_name = NULL;
			if (p_net->p_net_description[0] == '\0')
				p_net->p_net_description = NULL;
		} else {
			p_net->p_net_name        = NULL;
			p_net->p_net_description = NULL;
		}
	}

	if (new_index != idx) {
		struct gridcell *p_tmp  = pp_list_base[new_index];
		pp_list_base[new_index] = pp_list_base[idx];
		pp_list_base[idx]       = p_tmp;

		assert((p_tmp->data & GRIDCELL_PROGRAM_BUSY_BIT) == 0);
		p_tmp->data = (p_tmp->data & ~GRIDCELL_PROGRAM_NET_ID_BITS) | GRIDCELL_PROGRAM_NET_ID_BITS_SET(idx);
	}

	pp_list_base[new_index]->data = (pp_list_base[new_index]->data & ~(GRIDCELL_PROGRAM_BUSY_BIT|GRIDCELL_PROGRAM_NET_ID_BITS)) | compute_flag_and_net_id_bits;
}

/* This function takes the cell with index "idx" and moves it into the current
 * net group at position *p_insidx and then increments this position. If the
 * cell at idx is part of a GRIDCELL_BIT_MERGED_LAYERS set, all cells above
 * and below the given cell will also be moved into the group (*p_insidx will
 * be incremented by the number of layers in this case).
 * 
 * Cells that happen to be in positions where the new cells need to exist will
 * be swapped. The cell that has been moved (and will always be moved to a
 * higher index) will have its net id updated to the new index.
 * 
 * Before this function is called, cells at idx will have their net ID set to
 * idx. After this function is called, the net ID will be set to whatever is
 * inside compute_flag_and_net_id_bits. So this function is what changes the
 * net ID from representing a cell index - to being the actual net ID. */
static void move_cell_and_layers(struct gridstate *p_gridstate, struct gridcell **pp_list_base, uint32_t idx, uint32_t *p_insidx, uint64_t compute_flag_and_net_id_bits, struct program_net *p_net) {
	assert(GRIDCELL_PROGRAM_NET_ID_BITS_GET(pp_list_base[idx]->data) == idx);
	
	if (pp_list_base[idx]->data & GRIDCELL_BIT_MERGED_LAYERS) {
		uint32_t         page_index_base, i;
		struct gridpage *p_page = gridcell_get_page_and_index(pp_list_base[idx], &page_index_base);

		/* preserve the page x, y address while zeroing the layer */
		page_index_base &= 0xFF;

		for (i = 0; i < NUM_LAYERS; i++) {
			uint32_t page_index    = page_index_base | (i << 8);
			uint64_t index_in_list = GRIDCELL_PROGRAM_NET_ID_BITS_GET(p_page->data[page_index].data);

			assert(pp_list_base[index_in_list] == &(p_page->data[page_index]));

			/* if the merged layer bit is set on one layer, it must be set on all! */
			assert(p_page->data[page_index].data & GRIDCELL_BIT_MERGED_LAYERS);

			move_single_cell(p_gridstate, pp_list_base, index_in_list, p_insidx, compute_flag_and_net_id_bits, p_net);
		}
	} else {
		move_single_cell(p_gridstate, pp_list_base, idx, p_insidx, compute_flag_and_net_id_bits, p_net);
	}
}

#define SQRT3   (1.732050807568877f)
#define SQRT3_4 (SQRT3*0.5f)

#define PROGRAM_DEBUG (0)

/* After calling program_compile, the upper 32 bits of each cell's data
 * correspond to the net id of that cell. This can be used to get the
 * the value of the net after running the program by looking at the
 * stored data. It can also be used to force values on in the program
 * data prior to execution. */
static int program_compile(struct program *p_program, struct gridstate *p_gridstate) {
	size_t            num_cells;
	int               b_program_busted = 0;

	p_program->stack_count        = 0;
	p_program->net_count          = 0;
	p_program->code_count         = 0;
	p_program->stacked_cell_count = 0;
	p_program->substrate_area     = 0;
	p_program->worst_logic_chain  = 0;
	p_program->labelled_net_count = 0;
	p_program->named_net_count    = 0;

#if PROGRAM_DEBUG
	printf("added %llu grid pages\n", (unsigned long long)num_grid_pages);
#endif

	/* 2) push all used grid-cells onto the list. set their net id to the
	 * position in the list and clear the compute bit. */
	{
		size_t                      stacked_cell_count = 0;
		uint32_t                    min_x = 0;
		uint32_t                    max_x = 0;
		uint32_t                    min_y = 0;
		uint32_t                    max_y = 0;
		struct gridpage_lookup_enumerator  enumerator;
		struct gridpage            *p_gp;

		num_cells = 0;
		gridpage_lookup_enumerator_init(&enumerator, &(p_gridstate->pages));
		while ((p_gp = gridpage_lookup_node_optional_get_data(gridpage_lookup_enumerator_get(&enumerator))) != NULL) {
			int j;
			for (j = 0; j < PAGE_CELLS_PER_LAYER; j++) {
				int      l;
				size_t   num_cells_start = num_cells;

				/* Clear all upper bits related to program development and
				 * then set the net ID to be the index of the cell in the
				 * list. We need it to be this for the next step. */
				for (l = 0; l < NUM_LAYERS; l++) {
					int addr = j+l*PAGE_CELLS_PER_LAYER;
					p_gp->data[addr].data &= ~GRIDCELL_PROGRAM_BITS; /* always clear program bits */
					if (CELL_WILL_GET_A_NET(p_gp->data[addr].data)) {
						p_gp->data[addr].data |= GRIDCELL_PROGRAM_NET_ID_BITS_SET(num_cells);
						num_cells++;
						program_stack_push(p_program, &(p_gp->data[addr]));
					}
				}

				if (num_cells_start != num_cells) {
					uint32_t gpx   = p_gp->position.x | (j & PAGE_XY_MASK);
					uint32_t gpy   = p_gp->position.y |  ((j >> PAGE_XY_BITS) & PAGE_XY_MASK);
					uint32_t xfrmx = 2*gpx + (gpy&1);
					if (stacked_cell_count == 0) {
						min_x = xfrmx;
						max_x = xfrmx;
						min_y = gpy;
						max_y = gpy;
					} else {
						min_x = (min_x < xfrmx) ? min_x : xfrmx;
						max_x = (max_x > xfrmx) ? max_x : xfrmx;
						min_y = (min_y < gpy) ? min_y : gpy;
						max_y = (max_y > gpy) ? max_y : gpy;
					}

					//float yp = gpy*SQRT3_4;
					//float xp = (2*gpx + (gpy&1))*1.5f; hex_area=3*sqrt(3)/2, box_area=2*sqrt(3)  hex_area/box_area=3/4

					stacked_cell_count++;
				}
			}
		}

		/* store statistics */
		p_program->stacked_cell_count = stacked_cell_count;
		p_program->substrate_area     = stacked_cell_count ? ((2 + max_y - min_y)*(uint64_t)(4 + 3*max_x - 3*min_x)) : 0; /* scale by for box area SQRT3_4*0.5  */
	}

	/* no nodes, no problems. */
	if (p_program->stacked_cell_count == 0)
		return 0;

#if PROGRAM_DEBUG
	printf("added %llu cells\n", (unsigned long long)num_cells);
#endif

	/* 3) sort the list into groups sharing a common net. reassign the net
	 * ids to be actual net ids. this will set all the compute bits. in this
	 * step, the GRIDCELL_PROGRAM_BUSY_BIT effectively idenfies if the net_id
	 * bits of the cell are an actual net id as opposed to the index of the
	 * cell in the list. At the end of this, all cells will have the
	 * GRIDCELL_PROGRAM_BUSY_BIT bit set. */
	{
		uint32_t group_pos;

		/* note to future self - DO NOT CALL program_stack_push after this
		 * it could change the base pointer of pp_stack. */
		struct gridcell **pp_list_base = p_program->pp_stack;

		group_pos   = 0;
		while (group_pos < num_cells) {
			uint32_t            group_end                    = group_pos;
			uint64_t            net_id                       = p_program->net_count; /* must be read before calling program_net_push() */
			uint64_t            compute_flag_and_net_id_bits = GRIDCELL_PROGRAM_BUSY_BIT | GRIDCELL_PROGRAM_NET_ID_BITS_SET(net_id);
			struct program_net *p_net                        = program_net_push(p_program);
			p_net->net_id                     = net_id;
			p_net->b_currently_in_solve_stack = 0;
			p_net->first_cell_stack_index     = group_pos;
			p_net->b_exists_in_a_cycle        = 0;
			p_net->serial_gates               = 0;
			p_net->gate_fanout                = 0;
			p_net->nb_net_info_refs           = 0;
			p_net->p_net_description          = NULL;
			p_net->p_net_name                 = NULL;
			/* Initialise the state such that on the first increment, we will
			 * move to cell 0, edge 0. */
			p_net->current_solve_cell = (uint32_t)-1;
			p_net->current_solve_edge = EDGE_DIR_NUM-1;
			move_cell_and_layers(p_gridstate, pp_list_base, group_pos, &group_end, compute_flag_and_net_id_bits, p_net);
			do {
				int i;
				assert((pp_list_base[group_pos]->data & GRIDCELL_PROGRAM_BITS) == compute_flag_and_net_id_bits);
				for (i = 0; i < EDGE_DIR_NUM; i++) {
					int ctype = gridcell_get_edge_connection_type(pp_list_base[group_pos], i);
					if (ctype == EDGE_LAYER_CONNECTION_NET_CONNECTED) {
						struct gridcell *p_neighbour = gridcell_get_edge_neighbour(pp_list_base[group_pos], i, 0);
						if ((p_neighbour->data & GRIDCELL_PROGRAM_BUSY_BIT) == 0) {
							move_cell_and_layers(p_gridstate, pp_list_base, GRIDCELL_PROGRAM_NET_ID_BITS_GET(p_neighbour->data), &group_end, compute_flag_and_net_id_bits, p_net);
						}
					}
				}
				group_pos++;
			} while (group_pos < group_end);
			p_net->cell_count = group_pos - p_net->first_cell_stack_index;
		}
	}

	/* at this point, all cells are assigned to nets. */

	/* 4) order the nets such that they are all solvable and detect if the
	 * program is broken (has loops not broken by delays). */
	{
		size_t i;

		/* The stack array is used for two things. The part that grows up is
		 * used to track where we are in traversing the nets. The part that
		 * grows down is completely processed nets. As such, once this
		 * process ends, the nets can be evaluated from the end to the
		 * beginning of the list knowing that dependencies will be
		 * satisfied perfectly. */
		size_t end_of_stack_pointer = p_program->net_count;

		/* This is truely horrible - but seems to work. We first loop over
		 * nets - the purpose of this loop is to identify starting points
		 * which are nets that have not been processed. This could happen
		 * for a circuit which has distinct and disconnected sections. */
		for (i = 0; i < p_program->net_count; i++) {
			size_t stack_size;

			/* already solved? nothing to do here. */
			if (p_program->p_nets[i].current_solve_cell == p_program->p_nets[i].cell_count)
				continue;

			/* If it isn't fully solved, it had better be completely
			 * unsolved and never touched or this code is totally busted. */
			assert(p_program->p_nets[i].current_solve_cell == ((uint32_t)-1) && p_program->p_nets[i].current_solve_edge == (EDGE_DIR_NUM-1));

			/* If it is marked as being in the stack - something has gone
			 * completely wrong. */
			assert(p_program->p_nets[i].b_currently_in_solve_stack == 0);

			/* Okay, we have found a net that has not been seen before. We
			 * really only care about things that feed into this net, that
			 * is what we care about. If there are other nets that consume
			 * from this one, whatever, we can find it later in the outer
			 * for loop - no problem. But here, what we want to do is fully
			 * explore the nets that feed into recursively and without
			 * delay (delay always breaks cycles). Start by pushing this
			 * net onto the top of the stack and mark it as being in the
			 * stack (this is how we discover cycles and are able to mark
			 * all nets which take part in a cycle - if we have a stack
			 * of nodes poking outwards to a source and we hit a net which
			 * says it is already in the stack, there is some line of nets
			 * in the stack which form a loop). */
			stack_size                                      = 1;
			p_program->pp_netstack[0]                       = &(p_program->p_nets[i]);
			p_program->p_nets[i].b_currently_in_solve_stack = 1;

			while (stack_size) {
				/* Get the net on the very top of the stack. */
				struct program_net *p_this_net = p_program->pp_netstack[stack_size - 1];

				/* The very first thing we do is update the state to figure
				 * out what needs to happen during this update. Note that the
				 * first update of a state, these are initialised to values
				 * that will ensure that we start with edge 0 cell 0. */
				if (++p_this_net->current_solve_edge == EDGE_DIR_NUM) {
					p_this_net->current_solve_edge = 0;
					++p_this_net->current_solve_cell;
				}

				/* Then look to see if this node has now been completely
				 * traced. If it has, we can pop it to the backwards-growing
				 * end of the stack and it is completely done. This while()
				 * loop forms a bit of a state machine using the stack_size
				 * and the current_solve_cell and current_solve_edge members
				 * of the nets. Each iteration processes exactly one edge *or*
				 * discards one net from the stack (i.e. puts it in the
				 * finish list). */
				if (p_this_net->current_solve_cell == p_this_net->cell_count) {
					/* This net is finished - put at the end of the stack array */
					assert(p_this_net->current_solve_edge == 0);

					/* We're totally stuffed if somehow this got to zero. It
					 * means we visited something/somethings more than once. */
					assert(end_of_stack_pointer);

					/* Put the net at the end of the stack. Mark it as no
					 * longer in the list (other nodes can now reference this
					 * net whenever). */
					p_program->pp_netstack[--end_of_stack_pointer] = p_this_net;
					p_this_net->b_currently_in_solve_stack = 0; /* finished. */
					stack_size--;
				} else {
					/* The net on the top of the stack is not finished yet and
					 * we have more edges to process. */
					uint32_t            cell_idx;
					struct gridcell    *p_cell;
					int                 edge_id, edge_type;

					/* Either we should still be looking at edges in a valid
					 * cell within the net or we should be finished (which is
					 * handled in the block prior to the else). */
					assert(p_this_net->current_solve_cell < p_this_net->cell_count && p_this_net->current_solve_edge < EDGE_DIR_NUM);

					/* Get a pointer to the cell we are currently looking at
					 * within the current net and figure out what is going on
					 * with the current edge we are interested in. */
					cell_idx          = p_this_net->first_cell_stack_index + p_this_net->current_solve_cell;
					p_cell            = (struct gridcell *)p_program->pp_stack[cell_idx];
					edge_id           = p_this_net->current_solve_edge;
					edge_type         = gridcell_get_edge_connection_type(p_cell, edge_id);

					/* If the edge type is anything other than an undelayed,
					 * receiving mode, we don't need to do anything at all.
					 * If it is a net connection, we're going to look at it
					 * eventually (or have already looked at it). If it is a
					 * send connection, we don't care for reasons earlier
					 * mentioned. */
					if (edge_type == EDGE_LAYER_CONNECTION_RECEIVES_INVERTED) {
						/* Get the neighbouring cell and its net. */
						struct gridcell    *p_neighbour     = gridcell_get_edge_neighbour(p_cell, edge_id, 0);
						struct program_net *p_neighbour_net = &(p_program->p_nets[GRIDCELL_PROGRAM_NET_ID_BITS_GET(p_neighbour->data)]);

						/* The neighbour must exist if the edge type to it
						 * was connected. */
						assert(p_neighbour != NULL);

						/* The connection type should be send (as our cell
						 * said the edge was a receiver). */
						assert(gridcell_get_edge_connection_type(p_neighbour, get_opposing_edge_id(edge_id)) == EDGE_LAYER_CONNECTION_SENDS);

						if (p_neighbour_net->b_currently_in_solve_stack) {
							/* Go down the stack to find where the loop starts */
							size_t j = stack_size-1;
							while (p_program->pp_netstack[j] != p_neighbour_net) {
								assert(j);
								j--;
							}

							/* Go back up the stack marking broken edges along the way */
							for (; j < stack_size; j++) {
								struct program_net *p_stack_net;
								struct gridcell    *p_stack_cell;
								p_stack_net  = p_program->pp_netstack[j];
								p_stack_cell = (struct gridcell *)p_program->pp_stack[p_stack_net->first_cell_stack_index + p_stack_net->current_solve_cell];
								p_stack_cell->data |= GRIDCELL_PROGRAM_BROKEN_BIT_MASK(p_stack_net->current_solve_edge);
								gridcell_get_edge_neighbour(p_stack_cell, p_stack_net->current_solve_edge, 0)->data |= GRIDCELL_PROGRAM_BROKEN_BIT_MASK(get_opposing_edge_id(p_stack_net->current_solve_edge));
								p_stack_net->b_exists_in_a_cycle = 1;
							}
						} else if (p_neighbour_net->current_solve_cell != p_neighbour_net->cell_count) {
							/* The net is not already in the stack and has not
							 * been solved (the assert verifies everything is
							 * as expected). We need to put it on the top of
							 * the stack and start the edge exploration
							 * processes on it. */
							assert(p_neighbour_net->current_solve_cell == ((uint32_t)-1) && p_neighbour_net->current_solve_edge == (EDGE_DIR_NUM-1));
							p_program->pp_netstack[stack_size++] = p_neighbour_net;
							p_neighbour_net->b_currently_in_solve_stack = 1;
						} else {
							/* The net is not on the stack but has already
							 * been completely processed earlier. We do not
							 * need to do anything :) yay */
						}
					}
				}
			}
		}
		assert(end_of_stack_pointer == 0);
	}

	/* At this point, if there are no broken nets, we could actually run the
	 * program by evaluating the net stack backwards and examining the cells.
	 * But now what we want to do is re-number our nets to be in order of
	 * execution. The busy bit now explicitly identifies if the cell is part
	 * of a net in a cycle. */
	{
		size_t i;
		for (i = 0; i < p_program->net_count; i++) {
			struct program_net *p_net = p_program->pp_netstack[p_program->net_count-1-i];
			uint32_t j;
			uint64_t new_id_mask = GRIDCELL_PROGRAM_NET_ID_BITS_SET(i) | (p_net->b_exists_in_a_cycle ? GRIDCELL_PROGRAM_BUSY_BIT : 0);
			uint64_t multi_net_fail_mask = (p_net->nb_net_info_refs > 1) ? GRIDCELL_NET_LABEL_BIT : 0;

			/* if the net has multiple net references, trigger failure */
			if (p_net->b_exists_in_a_cycle || p_net->nb_net_info_refs > 1) {
				b_program_busted = 1;
			} else if (p_net->p_net_description != NULL || p_net->p_net_name != NULL) {
				program_labelled_net_push(p_program, p_net);
			}

			/* Ensure that all nets which are given a name have at least one cell with merged layers. */
			if (p_net->nb_net_info_refs == 1 && p_net->p_net_name != NULL) {
				for (j = 0; j < p_net->cell_count; j++) {
					uint32_t         cell_idx = p_net->first_cell_stack_index + j;
					struct gridcell *p_cell = (struct gridcell *)p_program->pp_stack[cell_idx];
					if (p_cell->data & GRIDCELL_BIT_MERGED_LAYERS)
						break;
				}
				if (j == p_net->cell_count) {
					/* There were no cells with the merged layer bit set. */
					new_id_mask |= GRIDCELL_PROGRAM_NO_MERGED_BIT;
					b_program_busted = 1;
				}
			}

			for (j = 0; j < p_net->cell_count; j++) {
				uint32_t         cell_idx = p_net->first_cell_stack_index + j;
				struct gridcell *p_cell = (struct gridcell *)p_program->pp_stack[cell_idx];
				assert(GRIDCELL_PROGRAM_NET_ID_BITS_GET(p_cell->data) == p_net->net_id);
				p_cell->data =
					(p_cell->data & ~(GRIDCELL_PROGRAM_NET_ID_BITS|GRIDCELL_PROGRAM_BUSY_BIT|GRIDCELL_PROGRAM_MULTI_NAME_BIT|GRIDCELL_PROGRAM_NO_MERGED_BIT)) |
					new_id_mask |
					((p_cell->data & multi_net_fail_mask) ? GRIDCELL_PROGRAM_MULTI_NAME_BIT : 0);
			}
			p_net->net_id = i;
		}

		/* sort the labelled nets. */
		if (p_program->labelled_net_count > 0) {
			qsort(p_program->pp_labelled_nets, p_program->labelled_net_count, sizeof(struct program_net *), net_label_compare);

			if (p_program->pp_labelled_nets[0]->p_net_name != NULL) {

				/* find any duplicate net IDs and mark those nets as busted. */
				for (i = 1; i < p_program->labelled_net_count; i++) {
					/* as soon as we hit a null, the rest of the names are all null
					* (and only descriptions are present which we don't care about). */
					if (p_program->pp_labelled_nets[i]->p_net_name == NULL)
						break;
					/* test for a common name */
					if (strcmp(p_program->pp_labelled_nets[i]->p_net_name, p_program->pp_labelled_nets[i-1]->p_net_name) == 0) {
						/* set failure bits on busted cells */
						gridstate_get_gridcell(p_gridstate, &(p_program->pp_labelled_nets[i]->net_info_cell_addr), 0)->data   |= GRIDCELL_PROGRAM_DUPLICATE_NAME_BIT;
						gridstate_get_gridcell(p_gridstate, &(p_program->pp_labelled_nets[i-1]->net_info_cell_addr), 0)->data |= GRIDCELL_PROGRAM_DUPLICATE_NAME_BIT;
						b_program_busted = 1;
					}
				}

				p_program->named_net_count = i;
			}
		}
	}

#if PROGRAM_DEBUG
	{
		size_t i;
		printf("found %llu nets\n", (unsigned long long)p_program->net_count);
		printf("  netid     cell address (x, y, layer)\n");
		for (i = 0; i < p_program->net_count; i++) {
			uint32_t j;
			struct program_net *p_net = p_program->pp_netstack[p_program->net_count-1-i];
			printf("  net %llu (id=%llu is broken=%d):\n", (unsigned long long)i, (unsigned long long)p_net->net_id, p_net->b_exists_in_a_cycle);
			for (j = 0; j < p_net->cell_count; j++) {
				struct gridaddr addr;
				uint32_t cell_idx = p_net->first_cell_stack_index + j;
				(void)gridcell_get_gridpage_and_full_addr((struct gridcell *)(p_program->pp_stack[cell_idx]), &addr);
				printf
					("    (%ld, %ld, %ld)\n"
					,(long)addr.x - (long)0x40000000
					,(long)addr.y - (long)0x40000000
					,(long)addr.z
					);

			}
		}
	}
#endif

	/* Early exit before we write our solver code. */
	if (b_program_busted)
		return b_program_busted;

	/* Fun time - build the program. At this point, everything is totally
	 * great. pp_netstack is reverse ordered by dependencies. We have no loops
	 * we can just read the stack backwards, look at the cells and dump where
	 * they get their data from. */
	{
		size_t    i;
		for (i = 0; i < p_program->net_count; i++) {
			struct program_net *p_net = p_program->pp_netstack[p_program->net_count-1-i];
			uint32_t j;
			uint32_t num_sources = 0;
			uint32_t *p_code_start = program_code_reserve(p_program);
			uint32_t fanout = 0;
			assert(p_net->net_id == i);
			for (j = 0; j < p_net->cell_count; j++) {
				uint32_t         cell_idx = p_net->first_cell_stack_index + j;
				struct gridcell *p_cell = (struct gridcell *)p_program->pp_stack[cell_idx];
				int k;
				assert(GRIDCELL_PROGRAM_NET_ID_BITS_GET(p_cell->data) == i);
				for (k = 0; k < EDGE_DIR_NUM; k++) {
					int edge_type = gridcell_get_edge_connection_type(p_cell, k);
					if (edge_type == EDGE_LAYER_CONNECTION_RECEIVES_DELAY_INVERTED || edge_type == EDGE_LAYER_CONNECTION_RECEIVES_INVERTED) {
						struct gridcell *p_neighbour         = gridcell_get_edge_neighbour(p_cell, k, 0);
						uint32_t         neighbour_net       = GRIDCELL_PROGRAM_NET_ID_BITS_GET(p_neighbour->data);
						if (edge_type == EDGE_LAYER_CONNECTION_RECEIVES_INVERTED) {
							uint32_t serial_gate_count = p_program->pp_netstack[p_program->net_count-1-neighbour_net]->serial_gates + 1;
							if (serial_gate_count > p_net->serial_gates) {
								p_net->serial_gates = serial_gate_count;
							}
							if (serial_gate_count > p_program->worst_logic_chain) {
								p_program->worst_logic_chain = serial_gate_count;
							}
						}
						assert(gridcell_get_edge_connection_type(p_neighbour, get_opposing_edge_id(k)) == EDGE_LAYER_CONNECTION_SENDS);
						if (edge_type == EDGE_LAYER_CONNECTION_RECEIVES_INVERTED) {
							assert(neighbour_net < i);
							*(program_code_reserve(p_program)) = (0ull << 24) | neighbour_net;
						} else {
							assert(edge_type == EDGE_LAYER_CONNECTION_RECEIVES_DELAY_INVERTED);
							assert(neighbour_net < p_program->net_count);
							*(program_code_reserve(p_program)) = (1ull << 24) | neighbour_net;
						}
						num_sources++;
					} else if (edge_type == EDGE_LAYER_CONNECTION_SENDS) {
						fanout++;
					}
				}
				*p_code_start      = num_sources;
			}
			p_net->gate_fanout = fanout;
		}

		/* clear states ready for processing. */
		memset(p_program->p_data, 0, sizeof(uint64_t)*((p_program->net_count + 63)/64));
		memset(p_program->p_last_data, 0, sizeof(uint64_t)*((p_program->net_count + 63)/64));
		memset(p_program->p_data_init, 0, sizeof(uint64_t)*((p_program->net_count + 63)/64));
	}

#if PROGRAM_DEBUG
	{
		size_t    i;
		uint32_t *p_code = p_program->p_code;
		printf("program stored in %llu words (%llu bytes)\n", (unsigned long long)p_program->code_count, ((unsigned long long)(p_program->code_count*4)));
		for (i = 0; i < p_program->net_count; i++) {
			size_t nb_sources = *p_code++;
			if (nb_sources == 0) {
				printf("  net %llu has zero sources\n", (unsigned long long)i);
			} else {
				printf("  net %llu has %llu sources\n", (unsigned long long)i, (unsigned long long)nb_sources);
				while (nb_sources--) {
					uint32_t  word       = *p_code++;
					uint32_t  op         = word >> 24;
					uint32_t  src_net    = word & 0xFFFFFF;
					const char *p_opname;
					if (op == 0) {
						assert(src_net < i);
						p_opname = "         INVERT";
					} else {
						assert(src_net < p_program->net_count);
						assert(op == 1);
						p_opname = "INVERT PREVIOUS";
					}
					printf("    %s %llu\n", p_opname, (unsigned long long)src_net);
				}
			}
		}
	}
#endif

	return 0;
}

#include "imgui_internal.h"

ImVec2 iv2_add(ImVec2 a, ImVec2 b) { return ImVec2(a.x + b.x, a.y + b.y); }
ImVec2 iv2_sub(ImVec2 a, ImVec2 b) { return ImVec2(a.x - b.x, a.y - b.y); }

struct plot_grid_state {
	float radius;
	int64_t bl_x; /* scaled by 16 bits */
	int64_t bl_y; /* scaled by 16 bits */
	
	int    mouse_down;
	int    b_mouse_down_pos_changed;
	ImVec2 mouse_down_pos;

#if 0
#define NUM_SNAKE_POINTS (2*2*2*2*2*3*5)
	float aa_the_snake[NUM_SNAKE_POINTS][2];
#endif

};

static void vmpyadd(float *p_x, float *p_y, float x2, float y2, float offsetx, float offsety) {
	float x1 = *p_x;
	float y1 = *p_y;
	*p_x = x1*x2 - y1*y2 + offsetx;
	*p_y = x1*y2 + y1*x2 + offsety;
}

static void vmpy(float *p_x, float *p_y, float x2, float y2) {
	vmpyadd(p_x, p_y, x2, y2, 0.0f, 0.0f);
}

static ImVec2 imvec2_cmac(float ax, float ay, float x1, float y1, float x2, float y2) {
	vmpyadd(&x1, &y1, x2, y2, ax, ay);
	return ImVec2(x1, y1);
}


static int get_next_connection_type(int old) {
	switch (old) {
	case EDGE_LAYER_CONNECTION_RECEIVES_INVERTED:
		return EDGE_LAYER_CONNECTION_RECEIVES_DELAY_INVERTED;
	case EDGE_LAYER_CONNECTION_RECEIVES_DELAY_INVERTED:
		return EDGE_LAYER_CONNECTION_NET_CONNECTED;
	case EDGE_LAYER_CONNECTION_NET_CONNECTED:
		return EDGE_LAYER_CONNECTION_UNCONNECTED;
	default:
		assert(old == EDGE_LAYER_CONNECTION_UNCONNECTED || old == EDGE_LAYER_CONNECTION_SENDS);
		return EDGE_LAYER_CONNECTION_RECEIVES_INVERTED;
	}
}

struct visible_cell_info {
	struct gridaddr addr_l0;

	float    centre_pixel_x;
	float    centre_pixel_y;

	int      b_cursor_in_cell;
	float    cursor_relative_to_centre_x;
	float    cursor_relative_to_centre_y;


};

struct visible_cell_iterator {
	float scale;
	float invscale;
	float window_width;
	float window_height;
	float min_x;
	float max_y;
	float mouse_x;
	float mouse_y;
	int64_t bl_y;
	int64_t bl_x; 

	struct visible_cell_info info;

};

void
visible_cell_iterator_init
	(struct visible_cell_iterator *p_iter
	,int64_t bl_x
	,int64_t bl_y
	,const ImRect &inner_bb
	,float scale
	,const ImVec2 &cursor_pos
	) {
	ImVec2 vMin = inner_bb.Min;
	ImVec2 vMax = inner_bb.Max;
	p_iter->bl_x           = bl_x;
	p_iter->bl_y           = bl_y;
	p_iter->scale          = scale;
	p_iter->invscale       = 1.0f / scale;
	p_iter->window_width   = vMax.x - vMin.x;
	p_iter->window_height  = vMax.y - vMin.y;
	p_iter->info.addr_l0.y = ((uint64_t)bl_y) >> 16;
	p_iter->info.addr_l0.x = (((uint64_t)bl_x) >> 16) - 1; /* compensates for pre increment */
	p_iter->info.addr_l0.z = 0;
	p_iter->min_x          = vMin.x;
	p_iter->max_y          = vMax.y;
	p_iter->mouse_x        = cursor_pos.x;
	p_iter->mouse_y        = cursor_pos.y;
}

const struct visible_cell_info *visible_cell_iterator_next(struct visible_cell_iterator *p_iter) {
	do {
		int64_t cy, cx;
		float   oy, ox;
		float   mrpx, mrpy;

		cy = ((int64_t)(int32_t)p_iter->info.addr_l0.y)*65536;
		oy = (cy - p_iter->bl_y)*p_iter->scale*0.866f/65536.0f;
		if (oy - p_iter->scale > p_iter->window_height) {
			return NULL;
		}

		p_iter->info.addr_l0.x++;
		cx = ((int64_t)(int32_t)p_iter->info.addr_l0.x)*65536;
		ox = (cx - p_iter->bl_x)*p_iter->scale*3.0f/65536.0f;
		if (p_iter->info.addr_l0.y & 0x1)
			ox += p_iter->scale*1.5f;

		if (ox - p_iter->scale > p_iter->window_width) {
			p_iter->info.addr_l0.y++;
			p_iter->info.addr_l0.x = (((uint64_t)p_iter->bl_x) >> 16) - 1; /* compensates for pre increment */
			continue;
		}

		p_iter->info.centre_pixel_x = p_iter->min_x + ox;
		p_iter->info.centre_pixel_y = p_iter->max_y - oy;

		mrpx = (p_iter->mouse_x - p_iter->info.centre_pixel_x)*p_iter->invscale;
		mrpy = (p_iter->mouse_y - p_iter->info.centre_pixel_y)*p_iter->invscale;

		p_iter->info.b_cursor_in_cell =
			(   (mrpy >= -SQRT3_4 && mrpy <= SQRT3_4)
			&&  (   (mrpx >= -0.5f && mrpx <= 0.5f)
			    ||  (mrpx < -0.5f && fabsf(mrpy) < (1.0f + mrpx)*SQRT3)
			    ||  (mrpx > 0.5f && fabsf(mrpy) < (1.0f - mrpx)*SQRT3)
			    )
			);
		
		if (p_iter->info.b_cursor_in_cell) {
			p_iter->info.cursor_relative_to_centre_x = mrpx;
			p_iter->info.cursor_relative_to_centre_y = mrpy;
		} else {
			p_iter->info.cursor_relative_to_centre_x = 0.0f;
			p_iter->info.cursor_relative_to_centre_y = 0.0f;
		}

		return &(p_iter->info);
	} while (1);

	return NULL;
}

struct layer_edge_info {
	struct gridaddr addr;
	struct gridaddr addr_other;

	/* True if the cursor is somewhere inside addr. */
	int             b_mouse_in_addr;

	/* Only valid if b_mouse_in_addr is non-zero. Gives the scaled position
	 * of the cursor relative to the centre of the cell. The scaling ensures
	 * a value between -1 and 1. */
	float           cursor_relative_to_centre_x;

	/* Only valid if b_mouse_in_addr is non-zero. Gives the scaled position
	 * of the cursor relative to the centre of the cell. The scaling ensures
	 * a value between -sqrt(3/4) and sqrt(3/4). */
	float           cursor_relative_to_centre_y;

	/* If the cursor is specifically in the clickable region for the current
	 * edge and layer, this will be non-zero. If this is non-zero and
	 * b_mouse_in_addr is zero, the mouse is in addr_other. */
	int             b_mouse_over_either;

	/* centre pixel of the cell at addr */
	float           centre_pixel_x;
	float           centre_pixel_y;

	int             layer;

	/* the edge of addr we are looking at */
	int             edge;

};

struct layer_edge_iterator {
	struct visible_cell_iterator cell_iter;
	const struct visible_cell_info *p_info;
	float mpxrc_rs;
	float mpyrc_rs;

	struct layer_edge_info info;

};

void
layer_edge_iterator_init
	(struct layer_edge_iterator *p_iter
	,int64_t bl_x
	,int64_t bl_y
	,const ImRect &inner_bb
	,float scale
	,const ImVec2 &cursor_pos
	) {
	visible_cell_iterator_init(&(p_iter->cell_iter), bl_x, bl_y, inner_bb, scale, cursor_pos);
	p_iter->info.edge  = 2;
	p_iter->info.layer = 2;

}

/* Properties of our hexagons... */

#define HEXAGON_OUTER_EDGE_LENGTH               (1.0f)
#define HEXAGON_OUTER_APOTHEM_LENGTH            (SQRT3_4)
#define HEXAGON_INNER_CENTRE_TO_VERTEX_DISTANCE ((1.0f + SQRT3)/(2.0f + SQRT3)) /* ~0.73205 */
#define HEXAGON_INNER_EDGE_LENGTH               (HEXAGON_INNER_CENTRE_TO_VERTEX_DISTANCE)
#define HEXAGON_INNER_APOTHEM_LENGTH            (HEXAGON_INNER_CENTRE_TO_VERTEX_DISTANCE*SQRT3_4) /* ~0.634 */
#define HEXAGON_INNER_TO_OUTER_APOTHEM_DISTANCE (HEXAGON_OUTER_APOTHEM_LENGTH - HEXAGON_INNER_APOTHEM_LENGTH) /* ~0.2 - maybe exact? */

/* If each inner edge contains 3 points: one in the centre and the other two
 * spaced +/- this value, this is the unique value such that circles could be
 * drawn centred on each point with a radius of half this value such that they
 * would all touch without overlapping. */
#define HEXAGON_INNER_CENTRE_EDGE_TO_OTHER_LAYER_CENTRE_DISTANCE ((SQRT3 + 3.0f)/(10.0f + 6.0f*SQRT3)) /* k = ~0.23205 */

static const float AA_INNER_EDGE_CENTRE_POINTS[3][2] =
	{{-HEXAGON_INNER_CENTRE_EDGE_TO_OTHER_LAYER_CENTRE_DISTANCE, -(HEXAGON_OUTER_APOTHEM_LENGTH - HEXAGON_INNER_TO_OUTER_APOTHEM_DISTANCE)}
	,{0.0f,                                                      -(HEXAGON_OUTER_APOTHEM_LENGTH - HEXAGON_INNER_TO_OUTER_APOTHEM_DISTANCE)}
	,{HEXAGON_INNER_CENTRE_EDGE_TO_OTHER_LAYER_CENTRE_DISTANCE,  -(HEXAGON_OUTER_APOTHEM_LENGTH - HEXAGON_INNER_TO_OUTER_APOTHEM_DISTANCE)}
	};

static const float AA_NEIGHBOUR_CENTRE_OFFSETS[3][2] = {{0.0f, -SQRT3}, {1.5f, -SQRT3_4}, {1.5f, SQRT3_4}};

static const float AA_EDGE_ROTATORS[6][2] =
	{{ 1.0f,  0.0f}
	,{ 0.5f,  SQRT3_4}
	,{-0.5f,  SQRT3_4}
	,{-1.0f,  0.0f}
	,{-0.5f, -SQRT3_4}
	,{ 0.5f, -SQRT3_4}
	};

//#define HEXAGON_OPTIMAL_GAP                                      (HEXAGON_INNER_CENTRE_EDGE_TO_OTHER_LAYER_CENTRE_DISTANCE/2.8f)

const struct layer_edge_info *layer_edge_iterator_next(struct layer_edge_iterator *p_iter) {
	do {
		if (p_iter->info.layer >= NUM_LAYERS - 1) {
			if (p_iter->info.edge >= EDGE_DIR_NUM/2 - 1) {
				if ((p_iter->p_info = visible_cell_iterator_next(&(p_iter->cell_iter))) == NULL)
					return NULL;

				p_iter->mpxrc_rs = (p_iter->cell_iter.mouse_x - p_iter->p_info->centre_pixel_x)*p_iter->cell_iter.invscale;
				p_iter->mpyrc_rs = (p_iter->cell_iter.mouse_y - p_iter->p_info->centre_pixel_y)*p_iter->cell_iter.invscale;
				p_iter->info.edge = 0;
			} else {
				p_iter->info.edge++;
				vmpy(&(p_iter->mpxrc_rs), &(p_iter->mpyrc_rs), 0.5f, -SQRT3_4);
			}
			p_iter->info.layer = 0;
		} else {
			p_iter->info.layer++;
		}

		p_iter->info.addr.x         = p_iter->p_info->addr_l0.x;
		p_iter->info.addr.y         = p_iter->p_info->addr_l0.y;
		p_iter->info.addr.z         = p_iter->info.layer;

		if (!gridaddr_edge_neighbour(&(p_iter->info.addr_other), &(p_iter->info.addr), p_iter->info.edge)) {
			float touching_radius = HEXAGON_INNER_CENTRE_EDGE_TO_OTHER_LAYER_CENTRE_DISTANCE*0.5f;
			float xrc             = p_iter->mpxrc_rs - (p_iter->info.layer - 1)*HEXAGON_INNER_CENTRE_EDGE_TO_OTHER_LAYER_CENTRE_DISTANCE;
			float yrc             = p_iter->mpyrc_rs + (0.866f - HEXAGON_INNER_CENTRE_EDGE_TO_OTHER_LAYER_CENTRE_DISTANCE);
			float yrc2            = p_iter->mpyrc_rs + (0.866f + HEXAGON_INNER_CENTRE_EDGE_TO_OTHER_LAYER_CENTRE_DISTANCE);

			p_iter->info.centre_pixel_x              = p_iter->p_info->centre_pixel_x;
			p_iter->info.centre_pixel_y              = p_iter->p_info->centre_pixel_y;
			p_iter->info.b_mouse_in_addr             = p_iter->p_info->b_cursor_in_cell;
			p_iter->info.cursor_relative_to_centre_x = p_iter->p_info->cursor_relative_to_centre_x;
			p_iter->info.cursor_relative_to_centre_y = p_iter->p_info->cursor_relative_to_centre_y;
			p_iter->info.b_mouse_over_either =
				(   xrc > -touching_radius
				&&  xrc < touching_radius
				&&  (  (yrc > -HEXAGON_INNER_CENTRE_EDGE_TO_OTHER_LAYER_CENTRE_DISTANCE && yrc < 0.0f) /* in the square between the inner and outer hexagon edges */
					||  xrc*xrc + yrc*yrc < touching_radius*touching_radius /* in the circular landing shape at the end of the hexagon */
					|| (yrc2 > 0.0 && yrc2 < HEXAGON_INNER_CENTRE_EDGE_TO_OTHER_LAYER_CENTRE_DISTANCE) /* in the square between the inner and outer hexagon edges */
					||  xrc*xrc + yrc2*yrc2 < touching_radius*touching_radius /* in the circular landing shape at the end of the hexagon */
					)
				);

			return &(p_iter->info);
		}
	} while (1);
	return NULL;
}

static
void
draw_inverted_delayed_arrow
	(float centre_x, float centre_y, float pre_offset_x, float scale_rotate_x, float scale_rotate_y, ImU32 colour) {
	float       ay = -(HEXAGON_OUTER_APOTHEM_LENGTH - HEXAGON_INNER_TO_OUTER_APOTHEM_DISTANCE);
	float       by = -(HEXAGON_OUTER_APOTHEM_LENGTH + HEXAGON_INNER_TO_OUTER_APOTHEM_DISTANCE);
	float       tr = (HEXAGON_INNER_CENTRE_EDGE_TO_OTHER_LAYER_CENTRE_DISTANCE/2.8f);
	ImVec2      a_points[19];
	int         i;
	ImDrawList *p_list = ImGui::GetBackgroundDrawList();
	a_points[0] = imvec2_cmac(centre_x, centre_y, pre_offset_x + (HEXAGON_INNER_CENTRE_EDGE_TO_OTHER_LAYER_CENTRE_DISTANCE*0.5f/2.8f), -HEXAGON_OUTER_APOTHEM_LENGTH+HEXAGON_INNER_CENTRE_EDGE_TO_OTHER_LAYER_CENTRE_DISTANCE*0.25f, scale_rotate_x, scale_rotate_y);
	for (i = 0; i < 17; i++) {
		float arch_cos = (HEXAGON_INNER_CENTRE_EDGE_TO_OTHER_LAYER_CENTRE_DISTANCE*0.5f/2.8f)*cosf(i * ((float)M_PI) / 16.0f);
		float arch_sin = (HEXAGON_INNER_CENTRE_EDGE_TO_OTHER_LAYER_CENTRE_DISTANCE*0.5f/2.8f)*sinf(i * ((float)M_PI) / 16.0f);
		a_points[i+1]  = imvec2_cmac(centre_x, centre_y, pre_offset_x + arch_cos, ay + arch_sin, scale_rotate_x, scale_rotate_y);
	}
	a_points[i+1] = imvec2_cmac(centre_x, centre_y, pre_offset_x - (HEXAGON_INNER_CENTRE_EDGE_TO_OTHER_LAYER_CENTRE_DISTANCE*0.5f/2.8f), -HEXAGON_OUTER_APOTHEM_LENGTH+HEXAGON_INNER_CENTRE_EDGE_TO_OTHER_LAYER_CENTRE_DISTANCE*0.25f, scale_rotate_x, scale_rotate_y);
	p_list->AddConvexPolyFilled(a_points, 19, colour);
	a_points[0] = imvec2_cmac(centre_x, centre_y, pre_offset_x,      by - tr,      scale_rotate_x, scale_rotate_y);
	a_points[1] = imvec2_cmac(centre_x, centre_y, pre_offset_x + tr, by + (SQRT3 - 1)*tr, scale_rotate_x, scale_rotate_y);
	a_points[2] = imvec2_cmac(centre_x, centre_y, pre_offset_x - tr, by + (SQRT3 - 1)*tr, scale_rotate_x, scale_rotate_y);
	p_list->AddConvexPolyFilled(a_points, 3, colour);
	a_points[0] = imvec2_cmac(centre_x, centre_y, pre_offset_x + (HEXAGON_INNER_CENTRE_EDGE_TO_OTHER_LAYER_CENTRE_DISTANCE*0.5f/2.8f), by, scale_rotate_x, scale_rotate_y);
	a_points[1] = imvec2_cmac(centre_x, centre_y, pre_offset_x + (HEXAGON_INNER_CENTRE_EDGE_TO_OTHER_LAYER_CENTRE_DISTANCE*0.5f/2.8f), -HEXAGON_OUTER_APOTHEM_LENGTH-HEXAGON_INNER_CENTRE_EDGE_TO_OTHER_LAYER_CENTRE_DISTANCE*0.25f, scale_rotate_x, scale_rotate_y);
	a_points[2] = imvec2_cmac(centre_x, centre_y, pre_offset_x - (HEXAGON_INNER_CENTRE_EDGE_TO_OTHER_LAYER_CENTRE_DISTANCE*0.5f/2.8f), -HEXAGON_OUTER_APOTHEM_LENGTH-HEXAGON_INNER_CENTRE_EDGE_TO_OTHER_LAYER_CENTRE_DISTANCE*0.25f, scale_rotate_x, scale_rotate_y);
	a_points[3] = imvec2_cmac(centre_x, centre_y, pre_offset_x - (HEXAGON_INNER_CENTRE_EDGE_TO_OTHER_LAYER_CENTRE_DISTANCE*0.5f/2.8f), by, scale_rotate_x, scale_rotate_y);
	p_list->AddConvexPolyFilled(a_points, 4, colour);
}


static
void
draw_inverted_arrow(float centre_x, float centre_y, float pre_offset_x, float scale_rotate_x, float scale_rotate_y, ImU32 colour) {
	float       ay = -(HEXAGON_OUTER_APOTHEM_LENGTH - HEXAGON_INNER_TO_OUTER_APOTHEM_DISTANCE);
	float       by = -(HEXAGON_OUTER_APOTHEM_LENGTH + HEXAGON_INNER_TO_OUTER_APOTHEM_DISTANCE);
	float       tr = (HEXAGON_INNER_CENTRE_EDGE_TO_OTHER_LAYER_CENTRE_DISTANCE/2.8f);
	ImVec2      a_points[19];
	int         i;
	ImDrawList *p_list = ImGui::GetBackgroundDrawList();
	a_points[0] = imvec2_cmac(centre_x, centre_y, pre_offset_x + (HEXAGON_INNER_CENTRE_EDGE_TO_OTHER_LAYER_CENTRE_DISTANCE*0.5f/2.8f), by, scale_rotate_x, scale_rotate_y);
	for (i = 0; i < 17; i++) {
		float arch_cos = (HEXAGON_INNER_CENTRE_EDGE_TO_OTHER_LAYER_CENTRE_DISTANCE*0.5f/2.8f)*cosf(i * ((float)M_PI) / 16.0f);
		float arch_sin = (HEXAGON_INNER_CENTRE_EDGE_TO_OTHER_LAYER_CENTRE_DISTANCE*0.5f/2.8f)*sinf(i * ((float)M_PI) / 16.0f);
		a_points[i+1]  = imvec2_cmac(centre_x, centre_y, pre_offset_x + arch_cos, ay + arch_sin, scale_rotate_x, scale_rotate_y);
	}
	a_points[i+1] = imvec2_cmac(centre_x, centre_y, pre_offset_x - (HEXAGON_INNER_CENTRE_EDGE_TO_OTHER_LAYER_CENTRE_DISTANCE*0.5f/2.8f), by, scale_rotate_x, scale_rotate_y);
	p_list->AddConvexPolyFilled(a_points, 19, colour);
	a_points[0] = imvec2_cmac(centre_x, centre_y, pre_offset_x,      by - tr,      scale_rotate_x, scale_rotate_y);
	a_points[1] = imvec2_cmac(centre_x, centre_y, pre_offset_x + tr, by + (SQRT3 - 1)*tr, scale_rotate_x, scale_rotate_y);
	a_points[2] = imvec2_cmac(centre_x, centre_y, pre_offset_x - tr, by + (SQRT3 - 1)*tr, scale_rotate_x, scale_rotate_y);
	p_list->AddConvexPolyFilled(a_points, 3, colour);
}

struct prop_window_state {
	bool            b_show_prop_window;
	struct gridaddr prop_addr_l0;

};

/* returns number of layer errors */
static int glue_error_names(char *p_buf, struct gridcell *p_l0, uint64_t error_bit) {
	int num_errors = 0;
	const char *ap_layers[NUM_LAYERS];
	int i;

	for (i = 0; i < NUM_LAYERS; i++) {
		if (p_l0[i*PAGE_CELLS_PER_LAYER].data & error_bit) {
			ap_layers[num_errors++] = AP_LAYER_NAMES[i];
		}
	}

	if (num_errors == NUM_LAYERS) {
		strcpy(p_buf, "All layers");
	} else {
		for (i = 0; i < num_errors; i++) {
			size_t nl = strlen(ap_layers[i]);
			if (i) {
				if (i == num_errors-1) {
					*p_buf++ = ' ';
					*p_buf++ = 'a';
					*p_buf++ = 'n';
					*p_buf++ = 'd';
					*p_buf++ = ' ';
				} else {
					*p_buf++ = ',';
					*p_buf++ = ' ';
				}
			}
			memcpy(p_buf, ap_layers[i], nl);
			p_buf += nl;
		}
		strcpy(p_buf, (num_errors > 1) ? " layers" : " layer");
	}

	return num_errors;
}

void plot_grid(struct gridstate *p_st, ImVec2 graph_size, struct plot_grid_state *p_state, struct program *p_prog, struct prop_window_state *p_prop_window) {
#if 0
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return;

    ImGuiContext &g = *GImGui;
    const ImGuiStyle &style = g.Style;
#endif
	float radius = p_state->radius;

	const ImRect inner_bb(ImVec2(0, 0), graph_size);

    ImGuiContext &g = *GImGui;
	ImGuiIO& io = ImGui::GetIO();
    const bool hovered = !io.WantCaptureMouse;

	ImVec2 mprel = ImVec2(io.MousePos.x - inner_bb.Min.x, inner_bb.Max.y - io.MousePos.y);

	int64_t bl_x = p_state->bl_x;
	int64_t bl_y = p_state->bl_y;

	struct visible_cell_iterator    iter;
	const struct visible_cell_info *p_info;
	struct layer_edge_iterator      edge_iter;
	const struct layer_edge_info   *p_edge_info;

	/* three cases:
	 *
	 *  1. p_hovered_cell==NULL and b_hovered_on_edge==0
	 *     hovered_cell_addr contains an address where z=0 of a cell which
	 *     is not attached to any net and the mouse is not over an edge
	 *     detail.
	 * 
	 *  2. p_hovered_cell==NULL and b_hovered_on_edge==1
	 *     hovered_cell_addr contains an address where the mouse is over an
	 *     edge detail (and z is valid) but the cell is not attached to any
	 *     net.
	 *
	 *  3. p_hovered_cell!=NULL and b_hovered_on_edge==1
	 *     hovered_cell_addr contains an address where the mouse is over an
	 *     edge detail (and z is valid) and the cell is attached to a net. */
	struct gridaddr                 hovered_cell_addr;
	const struct gridcell          *p_hovered_cell = NULL;
	int                             b_hovered_on_edge = 0;

	int b_real_click_occured = 0;
	
	/* goes non-zero when a click occurs that is not handled by any other
	 * handler in the cell. */
	int             b_non_salient_position_click = 0;
	struct gridaddr non_salient_click_addr_l0;

	uint64_t glfw_ticks = glfwGetTimerValue();
	uint64_t glfw_ticks_per_sec = glfwGetTimerFrequency();

	uint32_t animation_frame = ((glfw_ticks % glfw_ticks_per_sec) * 256 / glfw_ticks_per_sec);
	float    animation_frame_sin = sinf(animation_frame * (float)(2.0*M_PI/256));
	float    animation_frame_cos = cosf(animation_frame * (float)(2.0*M_PI/256));

	int detail_alpha   = (int)((radius - 6.0f)*25.0f); /* at 16 it's fully visible, at 6 it's fully gone */
	int overview_alpha = (int)((12 - radius)*43.0f);    /* at 6 it's fully visible, at 12 it's fully gone */

	hovered_cell_addr.x = 0x40000000;
	hovered_cell_addr.y = 0x40000000;
	hovered_cell_addr.z = 0x40000000;

	if (detail_alpha < 0)
		detail_alpha = 0;
	if (detail_alpha > 255)
		detail_alpha = 255;
	if (overview_alpha < 0)
		overview_alpha = 0;
	if (overview_alpha > 255)
		overview_alpha = 255;

	/* Figure out if the mouse has come down over this component. If so,
	 * record the down position and clear the position-changed flag. We use
	 * this to detect if we have been dragging the grid around to avoid
	 * clicking on a control. */
	if (hovered && g.IO.MouseDown[0] && !p_state->mouse_down) {
		p_state->mouse_down_pos           = mprel;
		p_state->mouse_down               = 1;
		p_state->b_mouse_down_pos_changed = 0;
	}

	if (hovered && g.IO.MouseReleased[1]) {
		ImGui::OpenPopup("CellRightClick", 0);
	}

	/* If the mouse is down, implement dragging the grid around. */
	if (p_state->mouse_down) {
		ImVec2 drag = iv2_sub(mprel, p_state->mouse_down_pos);

		if (drag.x != 0.0f || drag.y != 0.0f) {
			p_state->b_mouse_down_pos_changed = 1;
		}

		bl_x -= (int64_t)(drag.x/(radius*3.0f)*65536.0f);
		bl_y -= (int64_t)(drag.y/(radius*0.866f)*65536.0f);

		if (bl_x < 0)
			bl_x = 0;
		if (bl_y < 0)
			bl_y = 0;

		if (!g.IO.MouseDown[0]) {
			p_state->mouse_down = 0;
			p_state->bl_x = bl_x;
			p_state->bl_y = bl_y;

			/* note that this implies that the cursor is still hovered. */
			b_real_click_occured = !p_state->b_mouse_down_pos_changed;
			b_non_salient_position_click = b_real_click_occured;
		}
	}

	if (hovered) {
		if (io.MouseWheel != 0.0f) {
			bl_x -= (int64_t)(-mprel.x/(radius*3.0f)*65536.0f);
			bl_y -= (int64_t)(-mprel.y/(radius*0.866f)*65536.0f);

			radius *= powf(2.0, io.MouseWheel/40.0f);
			if (radius < 3.0f)
				radius = 3.0f;
			if (radius > 200.0f)
				radius = 200.0f;
			p_state->radius = radius;

			bl_x -= (int64_t)(+mprel.x/(radius*3.0f)*65536.0f);
			bl_y -= (int64_t)(+mprel.y/(radius*0.866f)*65536.0f);

			if (bl_x < 0)
				bl_x = 0;
			if (bl_y < 0)
				bl_y = 0;

			p_state->bl_x = bl_x;
			p_state->bl_y = bl_y;
		}
	}

	/* Handle edge clicks. */
	layer_edge_iterator_init(&edge_iter, bl_x, bl_y, inner_bb, radius, io.MousePos);
	while ((p_edge_info = layer_edge_iterator_next(&edge_iter)) != NULL) {
		if (b_real_click_occured && p_edge_info->b_mouse_over_either) {
			const struct gridaddr *p_addr = (p_edge_info->b_mouse_in_addr) ? &(p_edge_info->addr) : &(p_edge_info->addr_other);
			int                    edge   = (p_edge_info->b_mouse_in_addr) ? p_edge_info->edge : get_opposing_edge_id(p_edge_info->edge);
			struct gridcell       *p_cell;
			if ((p_cell = gridstate_get_gridcell(p_st, p_addr, 1)) != NULL) {
				if (!gridcell_set_neighbour_edge_connection_type(p_cell, edge, get_next_connection_type(gridcell_get_neighbour_edge_connection_type(p_cell, edge)))) {
					program_compile(p_prog, p_st);
				}
			}
			b_non_salient_position_click = 0;
		}
	}

	/* Handle centre clicks and double clicks. */
	visible_cell_iterator_init(&iter, bl_x, bl_y, inner_bb, radius, io.MousePos);
	while ((p_info = visible_cell_iterator_next(&iter)) != NULL) {
		if (p_info->b_cursor_in_cell) {
			non_salient_click_addr_l0 = p_info->addr_l0;
		}
		if (p_info->b_cursor_in_cell && b_real_click_occured) {
			if (g.IO.KeyShift) {
				/* Clear all cell connections. */
				struct gridcell *p_cell_l0 = gridstate_get_gridcell(p_st, &(p_info->addr_l0), 0);
				if (p_cell_l0 != NULL) {
					int i, j;
					gridcell_are_layers_fused_set(p_cell_l0, 0);
					for (j = 0; j < NUM_LAYERS; j++) {
						for (i = 0; i < EDGE_DIR_NUM; i++) {
							if (gridcell_get_edge_neighbour(p_cell_l0 + PAGE_CELLS_PER_LAYER*j, i, 0) != NULL) {
								gridcell_set_neighbour_edge_connection_type(p_cell_l0 + PAGE_CELLS_PER_LAYER*j, i, EDGE_LAYER_CONNECTION_UNCONNECTED);
							}
						}
					}
					/* todo - only do this if something changed. */
					program_compile(p_prog, p_st);
				}
				b_non_salient_position_click = 0;
			} else {
				/* Toggle layer fusing. */
				float x = p_info->cursor_relative_to_centre_x;
				float y = p_info->cursor_relative_to_centre_y;
				if (x*x + y*y < 0.433*0.433) {
					struct gridcell *p_cell = gridstate_get_gridcell(p_st, &(p_info->addr_l0), 1);
					if (p_cell != NULL) {
						gridcell_are_layers_fused_toggle(p_cell);
						program_compile(p_prog, p_st);
					}
					b_non_salient_position_click = 0;
				}
			}
		}
	}

	if (b_non_salient_position_click) {
		assert(non_salient_click_addr_l0.z == 0);
		p_prop_window->b_show_prop_window = true;
		p_prop_window->prop_addr_l0       = non_salient_click_addr_l0;

	}

	if (program_is_valid(p_prog)) {
		program_run(p_prog);
	}

	ImDrawList *p_list = ImGui::GetBackgroundDrawList();

	p_list->AddRectFilled(inner_bb.Min, inner_bb.Max, ImColor(128, 128, 128, 255));

	/* 1) Draw hexagons */
	visible_cell_iterator_init(&iter, bl_x, bl_y, inner_bb, radius, io.MousePos);
	while ((p_info = visible_cell_iterator_next(&iter)) != NULL) {
		float            inner_radius = radius * 0.95f;
		struct gridcell *p_cell_l0 = gridstate_get_gridcell(p_st, &(p_info->addr_l0), 0);
		ImVec2           a_points[6];
		int b_labelled =
			(  (p_cell_l0 != NULL)
			&& (  (p_cell_l0[0*PAGE_CELLS_PER_LAYER].data & GRIDCELL_NET_LABEL_BIT)
			   || (p_cell_l0[1*PAGE_CELLS_PER_LAYER].data & GRIDCELL_NET_LABEL_BIT)
			   || (p_cell_l0[2*PAGE_CELLS_PER_LAYER].data & GRIDCELL_NET_LABEL_BIT)
			   )
			);
		int b_broken_labels =
			(  (p_cell_l0 != NULL)
			&& (  (p_cell_l0[0*PAGE_CELLS_PER_LAYER].data & (GRIDCELL_PROGRAM_MULTI_NAME_BIT|GRIDCELL_PROGRAM_NO_MERGED_BIT|GRIDCELL_PROGRAM_DUPLICATE_NAME_BIT))
			   || (p_cell_l0[1*PAGE_CELLS_PER_LAYER].data & (GRIDCELL_PROGRAM_MULTI_NAME_BIT|GRIDCELL_PROGRAM_NO_MERGED_BIT|GRIDCELL_PROGRAM_DUPLICATE_NAME_BIT))
			   || (p_cell_l0[2*PAGE_CELLS_PER_LAYER].data & (GRIDCELL_PROGRAM_MULTI_NAME_BIT|GRIDCELL_PROGRAM_NO_MERGED_BIT|GRIDCELL_PROGRAM_DUPLICATE_NAME_BIT))
			   )
			);
		int b_cell_in_labelled_net =
			(  (p_cell_l0 != NULL)
			&& program_is_valid(p_prog)
			&& (  (CELL_WILL_GET_A_NET(p_cell_l0[0*PAGE_CELLS_PER_LAYER].data) && p_prog->pp_netstack[p_prog->net_count-1-GRIDCELL_PROGRAM_NET_ID_BITS_GET(p_cell_l0[0*PAGE_CELLS_PER_LAYER].data)]->nb_net_info_refs > 0)
			   || (CELL_WILL_GET_A_NET(p_cell_l0[1*PAGE_CELLS_PER_LAYER].data) && p_prog->pp_netstack[p_prog->net_count-1-GRIDCELL_PROGRAM_NET_ID_BITS_GET(p_cell_l0[1*PAGE_CELLS_PER_LAYER].data)]->nb_net_info_refs > 0)
			   || (CELL_WILL_GET_A_NET(p_cell_l0[2*PAGE_CELLS_PER_LAYER].data) && p_prog->pp_netstack[p_prog->net_count-1-GRIDCELL_PROGRAM_NET_ID_BITS_GET(p_cell_l0[2*PAGE_CELLS_PER_LAYER].data)]->nb_net_info_refs > 0)
			   )
			);


		a_points[0] = ImVec2(p_info->centre_pixel_x - inner_radius,      p_info->centre_pixel_y);
		a_points[1] = ImVec2(p_info->centre_pixel_x - 0.5f*inner_radius, p_info->centre_pixel_y - SQRT3_4*inner_radius);
		a_points[2] = ImVec2(p_info->centre_pixel_x + 0.5f*inner_radius, p_info->centre_pixel_y - SQRT3_4*inner_radius);
		a_points[3] = ImVec2(p_info->centre_pixel_x + inner_radius,      p_info->centre_pixel_y);
		a_points[4] = ImVec2(p_info->centre_pixel_x + 0.5f*inner_radius, p_info->centre_pixel_y + SQRT3_4*inner_radius);
		a_points[5] = ImVec2(p_info->centre_pixel_x - 0.5f*inner_radius, p_info->centre_pixel_y + SQRT3_4*inner_radius);
		p_list->AddConvexPolyFilled(a_points, 6, ImColor(255, 255, 255, 255));

		inner_radius *= 0.8f;

		if (b_broken_labels) {
			a_points[0] = ImVec2(p_info->centre_pixel_x - inner_radius,      p_info->centre_pixel_y);
			a_points[1] = ImVec2(p_info->centre_pixel_x - 0.5f*inner_radius, p_info->centre_pixel_y - SQRT3_4*inner_radius);
			a_points[2] = ImVec2(p_info->centre_pixel_x + 0.5f*inner_radius, p_info->centre_pixel_y - SQRT3_4*inner_radius);
			a_points[3] = ImVec2(p_info->centre_pixel_x + inner_radius,      p_info->centre_pixel_y);
			a_points[4] = ImVec2(p_info->centre_pixel_x + 0.5f*inner_radius, p_info->centre_pixel_y + SQRT3_4*inner_radius);
			a_points[5] = ImVec2(p_info->centre_pixel_x - 0.5f*inner_radius, p_info->centre_pixel_y + SQRT3_4*inner_radius);
			p_list->AddConvexPolyFilled(a_points, 6, ImColor((int)(192 - animation_frame_sin*48), (int)(192 - animation_frame_sin*48), (int)(96 - animation_frame_sin*48), 255));
		} else if (b_labelled) {
			a_points[0] = ImVec2(p_info->centre_pixel_x - inner_radius,      p_info->centre_pixel_y);
			a_points[1] = ImVec2(p_info->centre_pixel_x - 0.5f*inner_radius, p_info->centre_pixel_y - SQRT3_4*inner_radius);
			a_points[2] = ImVec2(p_info->centre_pixel_x + 0.5f*inner_radius, p_info->centre_pixel_y - SQRT3_4*inner_radius);
			a_points[3] = ImVec2(p_info->centre_pixel_x + inner_radius,      p_info->centre_pixel_y);
			a_points[4] = ImVec2(p_info->centre_pixel_x + 0.5f*inner_radius, p_info->centre_pixel_y + SQRT3_4*inner_radius);
			a_points[5] = ImVec2(p_info->centre_pixel_x - 0.5f*inner_radius, p_info->centre_pixel_y + SQRT3_4*inner_radius);
			p_list->AddConvexPolyFilled(a_points, 6, ImColor(255, 255, 96, 255));
		} else if (b_cell_in_labelled_net) {
			a_points[0] = ImVec2(p_info->centre_pixel_x - inner_radius,      p_info->centre_pixel_y);
			a_points[1] = ImVec2(p_info->centre_pixel_x - 0.5f*inner_radius, p_info->centre_pixel_y - SQRT3_4*inner_radius);
			a_points[2] = ImVec2(p_info->centre_pixel_x + 0.5f*inner_radius, p_info->centre_pixel_y - SQRT3_4*inner_radius);
			a_points[3] = ImVec2(p_info->centre_pixel_x + inner_radius,      p_info->centre_pixel_y);
			a_points[4] = ImVec2(p_info->centre_pixel_x + 0.5f*inner_radius, p_info->centre_pixel_y + SQRT3_4*inner_radius);
			a_points[5] = ImVec2(p_info->centre_pixel_x - 0.5f*inner_radius, p_info->centre_pixel_y + SQRT3_4*inner_radius);
			p_list->AddConvexPolyFilled(a_points, 6, ImColor(255, 255, 192, 255));
		}

		if (p_cell_l0 != NULL && !program_is_valid(p_prog)) {
			int i;
			int sp;
			for (i = 0, sp = 0; i < NUM_LAYERS; i++) {
				int j;
				if (p_cell_l0[i*PAGE_CELLS_PER_LAYER].data & GRIDCELL_PROGRAM_BUSY_BIT) {
					float sx, sy;
					for (j = 0; j < 4; j++) {
						sx = cosf((j+6*sp)*(float)(2*M_PI/18))*0.533f*radius;
						sy = sinf((j+6*sp)*(float)(2*M_PI/18))*0.533f*radius;
						p_list->AddCircleFilled
							(imvec2_cmac(p_info->centre_pixel_x, p_info->centre_pixel_y, sx, sy, animation_frame_cos, animation_frame_sin)
							,radius*0.09f
							,ImColor
								(192 - ((i==1 || i==2) ? 64 : 0)
								,192 - ((i==0 || i==2) ? 64 : 0)
								,192 - ((i==0 || i==1) ? 64 : 0)
								,detail_alpha
								)
							,17
							);
					}
					sp++;
				}
			}
		}

		/* ensure the address gets initialised to something. This way, if
		 * p_hovered_cell==NULL, we still have an x, y address. */
		if (p_info->b_cursor_in_cell) {
			hovered_cell_addr = p_info->addr_l0;
		}
	}

	/* 2) Draw edges */
	if (detail_alpha > 0) {
		layer_edge_iterator_init(&edge_iter, bl_x, bl_y, inner_bb, radius, io.MousePos);
		while ((p_edge_info = layer_edge_iterator_next(&edge_iter)) != NULL) {
			struct gridcell *p_cell      = gridstate_get_gridcell(p_st, &(p_edge_info->addr), 0);
			struct gridcell *p_neighbour = gridstate_get_gridcell(p_st, &(p_edge_info->addr_other), 0);
			if (p_cell && p_neighbour) {
				int              ctype         = gridcell_get_edge_connection_type(p_cell, p_edge_info->edge);
				int              ntype         = gridcell_get_edge_connection_type(p_neighbour, get_opposing_edge_id(p_edge_info->edge));
				int              b_busted_edge = ((p_cell->data & GRIDCELL_PROGRAM_BROKEN_BIT_MASK(p_edge_info->edge)) || (p_neighbour->data & GRIDCELL_PROGRAM_BROKEN_BIT_MASK(get_opposing_edge_id(p_edge_info->edge))));
				float            ax            = AA_INNER_EDGE_CENTRE_POINTS[p_edge_info->layer][0];
				float            ay            = AA_INNER_EDGE_CENTRE_POINTS[p_edge_info->layer][1];
				float            bx            = ax;
				float            by            = ay - HEXAGON_INNER_TO_OUTER_APOTHEM_DISTANCE*2;
				float            rx            = AA_EDGE_ROTATORS[p_edge_info->edge][0]*radius;
				float            ry            = AA_EDGE_ROTATORS[p_edge_info->edge][1]*radius;

				float busted_selected_intensity = 255 - (b_busted_edge ? (int)(0.5f + (animation_frame_sin + 1.0)*48.0f) : 0);
				int   selected_intensity        = b_busted_edge ? busted_selected_intensity : 192;
				int   unselected_intensity      = b_busted_edge ? 96                        : 128;

				ImU32 layer_colour = ImColor
					((p_edge_info->layer == 0) ? selected_intensity : unselected_intensity
					,(p_edge_info->layer == 1) ? selected_intensity : unselected_intensity
					,(p_edge_info->layer == 2) ? selected_intensity : unselected_intensity
					,detail_alpha
					);

				if (ctype == EDGE_LAYER_CONNECTION_NET_CONNECTED) {
					ImVec2 a_points[34];
					int i;
					for (i = 0; i < 17; i++) {
						float arch_cos = (HEXAGON_INNER_CENTRE_EDGE_TO_OTHER_LAYER_CENTRE_DISTANCE/2.8f)*cosf(i * ((float)M_PI) / 16.0f);
						float arch_sin = (HEXAGON_INNER_CENTRE_EDGE_TO_OTHER_LAYER_CENTRE_DISTANCE/2.8f)*sinf(i * ((float)M_PI) / 16.0f);
						a_points[i]    = imvec2_cmac(p_edge_info->centre_pixel_x, p_edge_info->centre_pixel_y, ax + arch_cos, ay + arch_sin, rx, ry);
						a_points[i+17] = imvec2_cmac(p_edge_info->centre_pixel_x, p_edge_info->centre_pixel_y, bx - arch_cos, by - arch_sin, rx, ry);
					}
					p_list->AddConvexPolyFilled(a_points, 34, layer_colour);
				} else if (ctype == EDGE_LAYER_CONNECTION_RECEIVES_DELAY_INVERTED) {
					assert(ntype == EDGE_LAYER_CONNECTION_SENDS);
					draw_inverted_delayed_arrow
						(p_edge_info->centre_pixel_x + radius*AA_NEIGHBOUR_CENTRE_OFFSETS[p_edge_info->edge][0]
						,p_edge_info->centre_pixel_y + radius*AA_NEIGHBOUR_CENTRE_OFFSETS[p_edge_info->edge][1]
						,AA_INNER_EDGE_CENTRE_POINTS[2-p_edge_info->layer][0]
						,-rx
						,-ry
						,layer_colour
						);
				} else if (ctype == EDGE_LAYER_CONNECTION_RECEIVES_INVERTED) {
					assert(ntype == EDGE_LAYER_CONNECTION_SENDS);
					draw_inverted_arrow
						(p_edge_info->centre_pixel_x + radius*AA_NEIGHBOUR_CENTRE_OFFSETS[p_edge_info->edge][0]
						,p_edge_info->centre_pixel_y + radius*AA_NEIGHBOUR_CENTRE_OFFSETS[p_edge_info->edge][1]
						,AA_INNER_EDGE_CENTRE_POINTS[2-p_edge_info->layer][0]
						,-rx
						,-ry
						,layer_colour
						);
				} else if (ntype == EDGE_LAYER_CONNECTION_RECEIVES_DELAY_INVERTED) {
					assert(ctype == EDGE_LAYER_CONNECTION_SENDS);
					draw_inverted_delayed_arrow
						(p_edge_info->centre_pixel_x
						,p_edge_info->centre_pixel_y
						,AA_INNER_EDGE_CENTRE_POINTS[p_edge_info->layer][0]
						,rx
						,ry
						,layer_colour
						);
				} else if (ntype == EDGE_LAYER_CONNECTION_RECEIVES_INVERTED) {
					assert(ctype == EDGE_LAYER_CONNECTION_SENDS);
					draw_inverted_arrow
						(p_edge_info->centre_pixel_x
						,p_edge_info->centre_pixel_y
						,AA_INNER_EDGE_CENTRE_POINTS[p_edge_info->layer][0]
						,rx
						,ry
						,layer_colour
						);
				} else {
					assert(ntype == EDGE_LAYER_CONNECTION_UNCONNECTED);
					assert(ctype == EDGE_LAYER_CONNECTION_UNCONNECTED);
				}
			}
		}
	}

	/* 3) Draw status and cursor over stuff */
	layer_edge_iterator_init(&edge_iter, bl_x, bl_y, inner_bb, radius, io.MousePos);
	while ((p_edge_info = layer_edge_iterator_next(&edge_iter)) != NULL) {
		/* Render edge connector hovers */
		if (hovered && p_edge_info->b_mouse_over_either) {
			if (detail_alpha > 0) {
				float ax = AA_INNER_EDGE_CENTRE_POINTS[p_edge_info->layer][0];
				float ay = AA_INNER_EDGE_CENTRE_POINTS[p_edge_info->layer][1]; /* small negative numbers */
				float bx = ax;
				float by = ay - HEXAGON_INNER_TO_OUTER_APOTHEM_DISTANCE*2;
				float rx = AA_EDGE_ROTATORS[p_edge_info->edge][0]*radius;
				float ry = AA_EDGE_ROTATORS[p_edge_info->edge][1]*radius;
				ImU32 layer_colour = ImColor
					(((p_edge_info->layer == 0) ? 150 : 100)
					,((p_edge_info->layer == 1) ? 150 : 100)
					,((p_edge_info->layer == 2) ? 150 : 100)
					,(detail_alpha*64)/256
					);
				int i;
				ImVec2 a_points[34];
				for (i = 0; i < 17; i++) {
					float arch_cos = HEXAGON_INNER_CENTRE_EDGE_TO_OTHER_LAYER_CENTRE_DISTANCE*0.5f*cosf(i*((float)M_PI)/16.0f);
					float arch_sin = HEXAGON_INNER_CENTRE_EDGE_TO_OTHER_LAYER_CENTRE_DISTANCE*0.5f*sinf(i*((float)M_PI)/16.0f);
					a_points[i]    = imvec2_cmac(p_edge_info->centre_pixel_x, p_edge_info->centre_pixel_y, ax + arch_cos, ay + arch_sin, rx, ry);
					a_points[i+17] = imvec2_cmac(p_edge_info->centre_pixel_x, p_edge_info->centre_pixel_y, bx - arch_cos, by - arch_sin, rx, ry);
				}
				p_list->AddConvexPolyFilled(a_points, 34, layer_colour);
			}

			if (p_edge_info->b_mouse_in_addr) {
				p_hovered_cell    = gridstate_get_gridcell(p_st, &(p_edge_info->addr), 0);
				hovered_cell_addr = p_edge_info->addr;
				b_hovered_on_edge = 1;
			} else {
				p_hovered_cell    = gridstate_get_gridcell(p_st, &(p_edge_info->addr_other), 0);
				hovered_cell_addr = p_edge_info->addr_other;
				b_hovered_on_edge = 1;
			}
		}

		if (p_edge_info->edge == 0) {
			struct gridcell *p_cell = gridstate_get_gridcell(p_st, &(p_edge_info->addr), 0);

			/* Render active states. */
			if (p_cell != NULL && detail_alpha > 0 && CELL_WILL_GET_A_NET(p_cell->data) && program_is_valid(p_prog)) {
				uint32_t net_id   = GRIDCELL_PROGRAM_NET_ID_BITS_GET(p_cell->data);
				int      b_active = (p_prog->p_last_data[net_id>>6] & BIT_MASKS[net_id & 0x3F]) != 0;

				ImU32 layer_colour = ImColor
					(((p_edge_info->layer == 0) ? 224-32*b_active : 196-96*b_active)
					,((p_edge_info->layer == 1) ? 224-32*b_active : 196-96*b_active)
					,((p_edge_info->layer == 2) ? 224-32*b_active : 196-96*b_active)
					,detail_alpha
					);
				float px = p_edge_info->centre_pixel_x - sinf(p_edge_info->layer*(float)(2*M_PI/3))*radius*(3*0.433f/4);
				float py = p_edge_info->centre_pixel_y - cosf(p_edge_info->layer*(float)(2*M_PI/3))*radius*(3*0.433f/4);
				p_list->AddCircleFilled(ImVec2(px, py), radius*0.07, layer_colour, 17);
			}

			if (p_edge_info->layer == 0 && detail_alpha > 0) {
				/* Render the layer fusing pad. */
				if (p_cell != NULL && gridcell_are_layers_fused_get(p_cell)) {
					p_list->AddCircleFilled(ImVec2(p_edge_info->centre_pixel_x, p_edge_info->centre_pixel_y), radius*0.2, ImColor(128, 128, 128, detail_alpha), 17);
				}

				/* Render the hover over the layer fusing pad. */
				if (hovered && p_edge_info->b_mouse_in_addr) {
					float dx = p_edge_info->cursor_relative_to_centre_x;
					float dy = p_edge_info->cursor_relative_to_centre_y;
					if (dx*dx + dy*dy < 0.433f*0.433f)
						p_list->AddCircleFilled(ImVec2(p_edge_info->centre_pixel_x, p_edge_info->centre_pixel_y), radius*0.433f, ImColor(160, 160, 160, (detail_alpha*64)/256), 17);
				}
			}
		}

		/* only do this on the very last edge and layer of this address. We're
		* pooing pixels on top of everything. */
		if (overview_alpha > 0 && p_edge_info->edge == 2 && p_edge_info->layer == 2) {
			struct gridcell *p_cell = gridstate_get_gridcell(p_st, &(p_edge_info->addr), 0);
			if (p_cell != NULL) {
				int i;
				int b_busted;
				int b_interesting;

				for (i = 0; i < NUM_LAYERS && ((p_cell - i*PAGE_CELLS_PER_LAYER)->data & (GRIDCELL_PROGRAM_BUSY_BIT|GRIDCELL_PROGRAM_DUPLICATE_NAME_BIT|GRIDCELL_PROGRAM_MULTI_NAME_BIT|GRIDCELL_PROGRAM_NO_MERGED_BIT)) == 0; i++);
				b_busted      = (i != NUM_LAYERS);

				for (i = 0; i < NUM_LAYERS && CELL_WILL_GET_A_NET((p_cell - i*PAGE_CELLS_PER_LAYER)->data) == 0; i++);
				b_interesting = (i != NUM_LAYERS);

				if (b_interesting || b_busted) {
					float            inner_radius = radius * 0.95f;
					ImVec2           a_points[6];
					a_points[0] = ImVec2(p_edge_info->centre_pixel_x - inner_radius,      p_edge_info->centre_pixel_y);
					a_points[1] = ImVec2(p_edge_info->centre_pixel_x - 0.5f*inner_radius, p_edge_info->centre_pixel_y - SQRT3_4*inner_radius);
					a_points[2] = ImVec2(p_edge_info->centre_pixel_x + 0.5f*inner_radius, p_edge_info->centre_pixel_y - SQRT3_4*inner_radius);
					a_points[3] = ImVec2(p_edge_info->centre_pixel_x + inner_radius,      p_edge_info->centre_pixel_y);
					a_points[4] = ImVec2(p_edge_info->centre_pixel_x + 0.5f*inner_radius, p_edge_info->centre_pixel_y + SQRT3_4*inner_radius);
					a_points[5] = ImVec2(p_edge_info->centre_pixel_x - 0.5f*inner_radius, p_edge_info->centre_pixel_y + SQRT3_4*inner_radius);
					p_list->AddConvexPolyFilled(a_points, 6, b_busted ? ImColor(192, 64, 64, overview_alpha) : ImColor(160, 160, 160, overview_alpha));
				}
			}
		}
	}

	if (hovered && g.IO.KeyCtrl) {
		ImGui::BeginTooltip();
		ImGui::PushTextWrapPos(ImGui::GetFontSize()*35.0f);

		ImGui::Text("Address           (%ld, %ld)", (long)((int32_t)hovered_cell_addr.x) - (long)0x40000000, (long)((int32_t)hovered_cell_addr.y) - (long)0x40000000);
		ImGui::Text("Layer             %s\n", b_hovered_on_edge ? AP_LAYER_NAMES[hovered_cell_addr.z] : "Unspecified");

		if (program_is_valid(p_prog)) {
			if (p_hovered_cell != NULL && CELL_WILL_GET_A_NET(p_hovered_cell->data)) {
				uint32_t            net_id = GRIDCELL_PROGRAM_NET_ID_BITS_GET(p_hovered_cell->data);
				struct program_net *p_net = (assert(net_id < p_prog->net_count), p_prog->pp_netstack[p_prog->net_count-1-net_id]);
				if (p_net->p_net_name == NULL) {
					ImGui::Text("Net ID            %lu", (unsigned long)p_net->net_id);
				} else {
					ImGui::Text("Net ID            %lu (%s)", (unsigned long)p_net->net_id, p_net->p_net_name);
				}
				if (p_net->p_net_description != NULL) {
					ImGui::Text("Net Description   %s", p_net->p_net_description);
				}
				ImGui::Text("Net serial factor %lu", (unsigned long)p_net->serial_gates);
				ImGui::Text("Net gate fanout   %lu", (unsigned long)p_net->gate_fanout);
			}
		} else {
			struct gridcell *p_cell_l0;
			hovered_cell_addr.z = 0;
			p_cell_l0 = gridstate_get_gridcell(p_st, &hovered_cell_addr, 0);
			if (p_cell_l0 != NULL) {
				char a_buf[128];
				int num_errors;
				if ((num_errors = glue_error_names(a_buf, p_cell_l0, GRIDCELL_PROGRAM_BUSY_BIT)) > 0)
					ImGui::Text("%s %s", a_buf, (num_errors > 1) ? "exist in net cycles" : "exists in a net cycle");
				if ((num_errors = glue_error_names(a_buf, p_cell_l0, GRIDCELL_PROGRAM_MULTI_NAME_BIT)) > 0)
					ImGui::Text("%s %s", a_buf, (num_errors > 1) ? "define names for nets which have already been named" : "defines a name for a net which has already been named");
				if ((num_errors = glue_error_names(a_buf, p_cell_l0, GRIDCELL_PROGRAM_NO_MERGED_BIT)) > 0)
					ImGui::Text("%s %s", a_buf, (num_errors > 1) ? "are part of named nets that do not contain merged cells" : "is part of a named net that does not contain any merged cells");
				if ((num_errors = glue_error_names(a_buf, p_cell_l0, GRIDCELL_PROGRAM_DUPLICATE_NAME_BIT)) > 0)
					ImGui::Text("%s %s", a_buf, (num_errors > 1) ? "define net names already used by other nets" : "defines a net name used by another net");
			}
		}

		ImGui::PopTextWrapPos();
		ImGui::EndTooltip();
	}

	if (ImGui::BeginPopup("CellRightClick", 0)) {
		ImGui::Text("popup ahhahaha");
		if (ImGui::MenuItem("Close"))
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}



}

#if 0
/* We have tests and tests have grids associated with them that are their accepted solutions.
 * 
 * The test is executed against a grid and if the test passes, the grid is copied into the
 * accepted solutions.
 * 
 * The solution may be duplicated into a sandbox grid.
 * 
 * Sandbox grids may also be duplicated.*/

struct test {
	const char *p_name;


};
#endif

static void update_cell_net_labels(struct gridstate *p_gs, struct program *p_prog, const struct gridaddr *p_addr, const char *p_name, const char *p_description) {
	struct gridcell *p_cell;
	if (p_name[0] == '\0' && p_description[0] == '\0') {
		if ((p_cell = gridstate_get_gridcell(p_gs, p_addr, 0)) != NULL) {
			p_cell->data &= ~(uint64_t)GRIDCELL_NET_LABEL_BIT;
		}
	} else {
		if ((p_cell = gridstate_get_gridcell(p_gs, p_addr, 1)) != NULL) {
			p_cell->data |= GRIDCELL_NET_LABEL_BIT;
		}
	}
	program_compile(p_prog, p_gs);
}

int main(int argc, char **argv) {
	struct gridstate         gs;
	struct prop_window_state pws;

	gridstate_init(&gs);

	pws.b_show_prop_window = false;
	pws.prop_addr_l0.x = 0x40000000;
	pws.prop_addr_l0.y = 0x40000000;
	pws.prop_addr_l0.z = 0;

	// Setup window
	glfwSetErrorCallback(glfw_error_callback);
	if (!glfwInit())
		return 1;

	// Decide GL+GLSL versions
#if defined(IMGUI_IMPL_OPENGL_ES2)
	// GL ES 2.0 + GLSL 100
	const char* glsl_version = "#version 100";
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
	glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
#elif defined(__APPLE__)
	// GL 3.2 + GLSL 150
	const char* glsl_version = "#version 150";
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);  // 3.2+ only
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);            // Required on Mac
#else
	// GL 3.0 + GLSL 130
	const char* glsl_version = "#version 130";
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
	//glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);  // 3.2+ only
	//glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);            // 3.0+ only
#endif

	// Create window with graphics context
	GLFWwindow* window = glfwCreateWindow(1280, 720, "Trihexor", NULL, NULL);
	if (window == NULL)
		return 1;
	glfwMakeContextCurrent(window);
	glfwSwapInterval(1); // Enable vsync

	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	//io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
	//io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

	// Setup Dear ImGui style
	ImGui::StyleColorsDark();
	//ImGui::StyleColorsClassic();

	// Setup Platform/Renderer backends
	ImGui_ImplGlfw_InitForOpenGL(window, true);
	ImGui_ImplOpenGL3_Init(glsl_version);

	// Our state
	ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

	struct plot_grid_state plot_state;
	plot_state.radius = 40.0f;
	plot_state.bl_x = 0x40000000ull*65536;
	plot_state.bl_y = 0x40000000ull*65536;
	plot_state.mouse_down = 0;

	struct program prog;
	program_init(&prog);

	bool b_show_program_disassembly = false;
	bool b_show_challenges_window = false;
	bool b_show_net_probes_window = false;

	while (!glfwWindowShouldClose(window))
	{
		// Poll and handle events (inputs, window resize, etc.)
		// You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
		// - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
		// - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
		// Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
		glfwPollEvents();
		int window_w, window_h;
		glfwGetWindowSize(window, &window_w, &window_h);


		// Start the Dear ImGui frame
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();

#if 0
		// 2. Show a simple window that we create ourselves. We use a Begin/End pair to created a named window.
		{
			ImGui::Begin("FPS");                          // Create a window called "Hello, world!" and append into it.
			ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f/ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
			ImGui::End();
		}
#endif
		plot_grid(&gs, ImVec2(window_w, window_h), &plot_state, &prog, &pws);

		if (ImGui::BeginMainMenuBar()) {
			if (ImGui::BeginMenu("File")) {
				if (ImGui::MenuItem("New session")) {
				}
				if (ImGui::MenuItem("Open session")) {
				}
				if (ImGui::MenuItem("Save session")) {
				}
				if (ImGui::MenuItem("Save session as..")) {
				}
				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("Game")) {
				if (ImGui::MenuItem("Show challenges")) {
					b_show_challenges_window = true;
				}
				if (ImGui::MenuItem("Show net probes")) {
					b_show_net_probes_window = true;
				}
				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("Debug")) {
				if (ImGui::MenuItem("Show program disassembly")) {
					b_show_program_disassembly = true;
				}
				ImGui::EndMenu();
			}

			ImGui::EndMainMenuBar();
		}

		if (b_show_program_disassembly && ImGui::Begin("Debug - Program Disassembly", &b_show_program_disassembly)) {
			if (!program_is_valid(&prog)) {
				ImGui::Text("The grid is currently invalid");
			} else if (prog.net_count == 0) {
				ImGui::Text("The grid has no nets assigned to it");
			} else {
				size_t    i;
				uint32_t *p_code = prog.p_code;
				ImGui::Text("The grid contains %lu nets", (unsigned long)prog.net_count);
				ImGui::Text("The longest chain of operations is %lu", (unsigned long)prog.worst_logic_chain);
				ImGui::Text("Program Disassembly (%llu words)", (unsigned long long)prog.code_count);
				for (i = 0; i < prog.net_count; i++) {
					size_t nb_sources = *p_code++;
					ImGui::Text("  NET %llu SOURCES", (unsigned long long)i);
					while (nb_sources--) {
						uint32_t  word       = *p_code++;
						uint32_t  op         = word >> 24;
						uint32_t  src_net    = word & 0xFFFFFF;
						const char *p_opname;
						if (op == 0) {
							assert(src_net < i);
							p_opname = "    INVERT      ";
						} else {
							assert(src_net < prog.net_count);
							assert(op == 1);
							p_opname = "    DELAYINVERT ";
						}
						ImGui::Text("    %s %lu", p_opname, (unsigned long)src_net);
					}
				}
			}
			ImGui::End();
		}

		if (pws.b_show_prop_window) {
			struct cellnetinfo *p_cinfo = gridstate_get_cellnetinfo(&gs, &(pws.prop_addr_l0), 1);
			if (p_cinfo != NULL && ImGui::Begin("Cell Properties", &(pws.b_show_prop_window))) {
				struct gridaddr  tmp_addr = pws.prop_addr_l0;
				int b_text_changed;

				ImGui::Text("Cell address (%ld, %ld)", ((long)pws.prop_addr_l0.x) - (long)0x40000000, ((long)pws.prop_addr_l0.y) - (long)0x40000000);
				
				b_text_changed  = ImGui::InputText("L0 net name", p_cinfo->aa_net_name[0], sizeof(p_cinfo->aa_net_name[0]));
				b_text_changed |= ImGui::InputTextMultiline("L0 net description", p_cinfo->aa_net_description[0], sizeof(p_cinfo->aa_net_description[0]));

				if (b_text_changed) {
					tmp_addr.z = 0;
					update_cell_net_labels(&gs, &prog, &tmp_addr, p_cinfo->aa_net_name[0], p_cinfo->aa_net_description[0]);
				}

				b_text_changed  = ImGui::InputText("L1 net name", p_cinfo->aa_net_name[1], sizeof(p_cinfo->aa_net_name[1]));
				b_text_changed |= ImGui::InputTextMultiline("L1 net description", p_cinfo->aa_net_description[1], sizeof(p_cinfo->aa_net_description[1]));
				
				if (b_text_changed) {
					tmp_addr.z = 1;
					update_cell_net_labels(&gs, &prog, &tmp_addr, p_cinfo->aa_net_name[1], p_cinfo->aa_net_description[1]);
				}

				b_text_changed  = ImGui::InputText("L2 net name", p_cinfo->aa_net_name[2], sizeof(p_cinfo->aa_net_name[2]));
				b_text_changed |= ImGui::InputTextMultiline("L2 net description", p_cinfo->aa_net_description[2], sizeof(p_cinfo->aa_net_description[2]));

				if (b_text_changed) {
					tmp_addr.z = 2;
					update_cell_net_labels(&gs, &prog, &tmp_addr, p_cinfo->aa_net_name[2], p_cinfo->aa_net_description[2]);
				}

				ImGui::End();
			}
		}

		if (b_show_net_probes_window && ImGui::Begin("Net Probes", &(b_show_net_probes_window))) {
			if (!program_is_valid(&prog)) {
				ImGui::Text("The circuit is currently invalid and states cannot be probed.");
			} else if (prog.labelled_net_count == 0) {
				ImGui::Text("The circuit is valid but has no labelled nets.");
			} else if (ImGui::BeginTable("Available Nets", 4)) {
				uint32_t idx;
				ImGui::TableSetupColumn("Net Name");
				ImGui::TableSetupColumn("Force Active");
				ImGui::TableSetupColumn("Is Active?");
				ImGui::TableSetupColumn("Net Description");
				ImGui::TableHeadersRow();
				for (idx = 0; idx < prog.labelled_net_count; idx++) {
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					if (prog.pp_labelled_nets[idx]->p_net_name)
						ImGui::Text("%s", prog.pp_labelled_nets[idx]->p_net_name);
					ImGui::TableNextColumn();
					if (prog.pp_labelled_nets[idx]->p_net_name) {
						uint32_t net_id = prog.pp_labelled_nets[idx]->net_id;
						bool b_force_active = (prog.p_data_init[net_id >> 6] & BIT_MASKS[net_id & 0x3F]) != 0;
						ImGui::PushID(&(prog.pp_labelled_nets[idx]));
						ImGui::Checkbox("##netidbox", &b_force_active);
						ImGui::PopID();
						if (b_force_active) {
							prog.p_data_init[net_id >> 6] |= BIT_MASKS[net_id & 0x3F];
						} else {
							prog.p_data_init[net_id >> 6] &= ~BIT_MASKS[net_id & 0x3F];
						}
					}
					ImGui::TableNextColumn();
					{
						uint32_t net_id = prog.pp_labelled_nets[idx]->net_id;
						bool     b_was_active = (prog.p_last_data[net_id >> 6] & BIT_MASKS[net_id & 0x3F]) != 0;
						ImGui::PushID(&(prog.pp_labelled_nets[idx]));
						ImGui::BeginDisabled();
						ImGui::Checkbox("##statusbox", &b_was_active);
						ImGui::EndDisabled();
						ImGui::PopID();
					}
					ImGui::TableNextColumn();
					if (prog.pp_labelled_nets[idx]->p_net_description)
						ImGui::TextWrapped("%s", prog.pp_labelled_nets[idx]->p_net_description);
				}
				ImGui::EndTable();
			}
			ImGui::End();
		}


#if 0
		if (ImGui::BeginChild("##remainder", child_size, 0, ImGuiWindowFlags_NoSavedSettings)) {
			/* Level selection */
			const char* items[] = {"Sandbox", "1. Tutorial - Inverter", "2. Tutorial - Delay Inverter", "3. Tutorial - Merging Layers" };
			static int item_current_idx = 0; // Here we store our selection data as an index.
			ImGui::BeginGroup();
			ImGui::Text("Goal selection");
			if (ImGui::BeginListBox("##levelselector")) {
				for (int n = 0; n < IM_ARRAYSIZE(items); n++) {
					const bool is_selected = (item_current_idx == n);
					if (ImGui::Selectable(items[n], is_selected))
						item_current_idx = n;
					// Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
					if (is_selected)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndListBox();
			}

			ImGui::EndGroup();
			ImGui::SameLine();
			ImGui::BeginGroup();
			ImGui::Text("The grid contains %lu used cells", (unsigned long)prog.stacked_cell_count);
			if (prog.stacked_cell_count) {
				float box_area = prog.substrate_area*(SQRT3_4*0.5f);
				float hex_area = prog.stacked_cell_count*(3*SQRT3_4);
				ImGui::Text("Substrate area   %f pm", (float)box_area);
				ImGui::Text("Used cell area   %f pm", (float)hex_area);
				ImGui::Text("Cell utilisation %f %%", (float)(prog.stacked_cell_count*600.0f/prog.substrate_area));
			}


			ImGui::EndGroup();
		}
		ImGui::End();
#endif

		// Rendering
		ImGui::Render();

		int display_w, display_h;
		glfwGetFramebufferSize(window, &display_w, &display_h);
		glViewport(0, 0, display_w, display_h);
		glClearColor(clear_color.x*clear_color.w, clear_color.y*clear_color.w, clear_color.z*clear_color.w, clear_color.w);
		glClear(GL_COLOR_BUFFER_BIT);
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

		glfwSwapBuffers(window);
	}

#if 0
	if (argc > 1) {
		if (grid_save(&gs, argv[1])) {
			printf("failed to save grid to %s\n", argv[1]);
		} else {
			printf("saved grid to %s\n", argv[1]);
		}
	}
#endif

	// Cleanup
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();

	glfwDestroyWindow(window);
	glfwTerminate();

	return 0;
}

